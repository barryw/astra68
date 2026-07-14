// Astra 68 — Harte vector test harness ROM.
//
// Reuses the CPU selftest's proven scaffolding (../boot/crt0.S + ../boot/astra_st.ld):
// a working reset vector table, SSP setup, and an RTE _default_handler that survives
// spurious early exceptions. (The bare-asm harness froze on its first peripheral read
// because its catch-all vector was STOP #0x2700, not RTE.)
//
// Verified on silicon, in order: Phase 0 'R' boot marker + byte echo; Phase 1 PING/PONG;
// Phase 2 (here) RUN — per-case register load + single-step + result dump.
//
// Wire protocol (host side = host/proto.py + the spec §5, the source of truth):
//   PING    host->dev : 0x55 LEN 0x02 <b> cksum          -> PONG 0xAA 0x03 0x80 <b> cksum
//   RUN     host->dev : 0x55 LEN 0x01 [d0..d7:32][a0..a6:28][ccr:1][ilen:1][instr:ilen] cksum
//   RESULT  dev->host : 0xAA 0x3F 0x81 [d0..d7:32][a0..a6:28][ccr:1] cksum
//   cksum = sum(CMD..last payload byte) & 0xFF.  LEN = bytes after LEN (= CMD+payload+cksum).
#include "vesta.h"
#include "build_id.h"       // generated: #define BUILD_ID 0x........u (hash of RTL+firmware source)

#define SYNC_RX     0x55u   // host -> device sync byte
#define SYNC_TX     0xAAu   // device -> host sync byte
#define CMD_RUN     0x01u
#define CMD_PING    0x02u
#define CMD_ID      0x03u   // host asks "what build are you?"
#define CMD_INFO    0x04u
#define CMD_PONG    0x80u
#define CMD_RESULT  0x81u
#define CMD_IDR     0x83u   // reply carries BUILD_ID
#define CMD_INFOR   0x84u
#define CMD_ERROR   0xFFu

#define PROTOCOL_MAJOR 2u
#define PROTOCOL_MINOR 0u
#define MAX_INSTRUCTION_BYTES 30u
#define RX_BODY_BYTES 254u

#define ERROR_BAD_LENGTH   1u
#define ERROR_BAD_CHECKSUM 2u
#define ERROR_BAD_COMMAND  3u
#define ERROR_RX_OVERRUN   4u

// Per-case buffers. NON-static on purpose: the exec_case() inline asm references these
// by symbol name so the assembler emits absolute (abs.L) addressing — the only movem
// addressing mode that is safe once a0..a6 hold the test result (no base register survives).
// aligned(4): m68k defaults uint32_t[] to 2-byte alignment, which put these at 0x..E/0x..2
// offsets — making `movem.l` a MISALIGNED longword access that spans two words. This SoC's
// bus interface does not support misaligned/dynamic-bus-sizing: the cycle stalls (AS held,
// no completion) and the CPU hangs forever (no bus-error watchdog). 4-byte alignment keeps
// every movem transfer a clean aligned longword. (Verified on silicon via LED bus-state read.)
uint32_t regtable[15]  __attribute__((aligned(4))); // d0..d7, a0..a6 (big-endian byte order == m68k)
uint32_t resultbuf[15] __attribute__((aligned(4))); // same layout, captured after the test instruction
uint8_t  ccr_in;                                   // initial CCR for the case
uint8_t  result_ccr;                               // captured CCR (low byte of SR)
uint8_t  codebuf[32] __attribute__((aligned(4)));  // relocated test instruction + trailing RTS
uint8_t  rx_body[RX_BODY_BYTES];
uint8_t  tx_result[61];

static void putc(uint8_t c) {
    while (!(VESTA->UART_STATUS & UART_TX_READY)) { }
    VESTA->UART_DATA = c;
}

static uint32_t getc(void) {
    while (!(UART_RXSTATUS & UART_RX_READY)) { }
    return UART_RXDATA & 0xFFu;    // read consumes the byte (clears RX_READY)
}

static void send_frame(uint8_t cmd, const uint8_t *payload, uint32_t payload_len) {
    uint32_t checksum = cmd;
    putc(SYNC_TX);
    putc((uint8_t)(payload_len + 2u));
    putc(cmd);
    for (uint32_t i = 0; i < payload_len; i++) {
        putc(payload[i]);
        checksum += payload[i];
    }
    putc((uint8_t)checksum);
}

static void send_error(uint8_t code, uint8_t command) {
    uint8_t payload[2];
    payload[0] = code;
    payload[1] = command;
    send_frame(CMD_ERROR, payload, sizeof(payload));
}

static int receive_frame(uint8_t *command, uint8_t **payload, uint32_t *payload_len) {
    while (getc() != SYNC_RX) { }

    UART_RXSTATUS = UART_RX_FIFO_OVERRUN;
    uint32_t length = getc();
    if (length < 2u) {
        send_error(ERROR_BAD_LENGTH, 0u);
        return 0;
    }

    uint32_t body_len = length - 1u;
    uint32_t checksum = 0u;
    for (uint32_t i = 0; i < body_len; i++) {
        rx_body[i] = (uint8_t)getc();
        checksum += rx_body[i];
    }
    uint32_t received_checksum = getc();
    uint8_t frame_command = rx_body[0];

    if (UART_RXSTATUS & UART_RX_FIFO_OVERRUN) {
        UART_RXSTATUS = UART_RX_FIFO_OVERRUN;
        send_error(ERROR_RX_OVERRUN, frame_command);
        return 0;
    }
    if ((uint8_t)checksum != (uint8_t)received_checksum) {
        send_error(ERROR_BAD_CHECKSUM, frame_command);
        return 0;
    }

    *command = frame_command;
    *payload = &rx_body[1];
    *payload_len = body_len - 1u;
    return 1;
}

// Per-case executor in harness_exec.S. harte_exec loads regs+CCR, JSRs into the relocated
// instruction in codebuf (which C terminates with a trailing RTS), and captures the result
// regs+CCR into resultbuf/result_ccr.
extern void harte_exec(void);

static void run_case(const uint8_t *payload, uint32_t payload_len) {
    if (payload_len < 62u) {
        send_error(ERROR_BAD_LENGTH, CMD_RUN);
        return;
    }

    uint32_t ilen = payload[61];
    if (ilen > MAX_INSTRUCTION_BYTES || payload_len != 62u + ilen) {
        send_error(ERROR_BAD_LENGTH, CMD_RUN);
        return;
    }

    uint8_t *rt = (uint8_t *)regtable;
    for (uint32_t i = 0; i < 60u; i++)
        rt[i] = payload[i];
    ccr_in = payload[60];

    uint8_t *cp = codebuf;
    for (uint32_t i = 0; i < ilen; i++)
        *cp++ = payload[62u + i];

    *cp++ = 0x4E;                                    // trailing RTS (0x4E75): returns to harte_exec.
    *cp   = 0x75;                                    // one word — never a multi-word fetch from RAM.
    harte_exec();

    uint8_t *rb = (uint8_t *)resultbuf;
    for (uint32_t i = 0; i < 60u; i++)
        tx_result[i] = rb[i];
    tx_result[60] = result_ccr;
    send_frame(CMD_RESULT, tx_result, sizeof(tx_result));
}

void kmain(void) {
    putc('R');                                       // boot marker: C harness booted + TX works
    for (;;) {
        uint8_t cmd;
        uint8_t *payload;
        uint32_t payload_len;
        if (!receive_frame(&cmd, &payload, &payload_len))
            continue;

        if (cmd == CMD_RUN) {
            run_case(payload, payload_len);
        } else if (cmd == CMD_PING) {
            if (payload_len != 1u)
                send_error(ERROR_BAD_LENGTH, cmd);
            else
                send_frame(CMD_PONG, payload, 1u);
        } else if (cmd == CMD_ID) {                  // report BUILD_ID: proves what firmware is running
            uint32_t id = BUILD_ID;
            uint8_t reply[4];
            reply[0] = (uint8_t)(id >> 24);
            reply[1] = (uint8_t)(id >> 16);
            reply[2] = (uint8_t)(id >> 8);
            reply[3] = (uint8_t)id;
            send_frame(CMD_IDR, reply, sizeof(reply));
        } else if (cmd == CMD_INFO) {
            uint32_t id = BUILD_ID;
            uint32_t baud = ASTRA_HARTE_UART_BAUD;
            uint8_t reply[12];
            reply[0] = PROTOCOL_MAJOR;
            reply[1] = PROTOCOL_MINOR;
            reply[2] = MAX_INSTRUCTION_BYTES;
            reply[3] = 0x03u; // bit 0: RX FIFO, bit 1: strict framing
            reply[4] = (uint8_t)(id >> 24);
            reply[5] = (uint8_t)(id >> 16);
            reply[6] = (uint8_t)(id >> 8);
            reply[7] = (uint8_t)id;
            reply[8] = (uint8_t)(baud >> 24);
            reply[9] = (uint8_t)(baud >> 16);
            reply[10] = (uint8_t)(baud >> 8);
            reply[11] = (uint8_t)baud;
            send_frame(CMD_INFOR, reply, sizeof(reply));
        } else {
            send_error(ERROR_BAD_COMMAND, cmd);
        }
    }
}
