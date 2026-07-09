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
#define CMD_PONG    0x80u
#define CMD_RESULT  0x81u
#define CMD_IDR     0x83u   // reply carries BUILD_ID

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

static void putc(uint8_t c) {
    while (!(VESTA->UART_STATUS & UART_TX_READY)) { }
    VESTA->UART_DATA = c;
}

static uint32_t getc(void) {
    while (!(UART_RXSTATUS & UART_RX_READY)) { }
    return UART_RXDATA & 0xFFu;    // read consumes the byte (clears RX_READY)
}

// Per-case executor in harness_exec.S. harte_exec loads regs+CCR, JSRs into the relocated
// instruction in codebuf (which C terminates with a trailing RTS), and captures the result
// regs+CCR into resultbuf/result_ccr.
extern void harte_exec(void);

static void run_case(void) {
    // Read+checksum the payload. cksum covers CMD + payload (spec §5). A corrupt/partial
    // frame (flaky FTDI drops bytes) MUST be dropped, never executed: garbage in codebuf
    // would send `jmp codebuf` into the weeds and hang the CPU. On mismatch we return, and
    // kmain's resync-on-SYNC recovers; the host times out and retries.
    uint32_t ck = CMD_RUN;
    uint8_t *rt = (uint8_t *)regtable;
    for (uint32_t i = 0; i < 60; i++) { uint32_t b = getc(); rt[i] = (uint8_t)b; ck += b; } // d0..d7, a0..a6
    uint32_t ccr = getc();  ck += ccr;               // initial CCR
    ccr_in = (uint8_t)ccr;
    uint32_t ilen = getc(); ck += ilen;              // instruction length in bytes
    uint8_t *cp = codebuf;
    for (uint32_t i = 0; i < ilen; i++) {            // read ALL instr bytes (keep frame in sync)...
        uint32_t b = getc(); ck += b;
        if (i < sizeof(codebuf) - 2) *cp++ = (uint8_t)b; // ...but store only what fits
    }
    uint32_t rxck = getc();
    if (((uint8_t)ck) != (uint8_t)rxck) return;      // bad checksum: drop frame, resync
    if (ilen > sizeof(codebuf) - 2) return;          // instruction too long for our scope: drop

    *cp++ = 0x4E;                                    // trailing RTS (0x4E75): returns to harte_exec.
    *cp   = 0x75;                                    // one word — never a multi-word fetch from RAM.
    harte_exec();

    // TX RESULT: 0xAA 0x3F 0x81 <60 reg bytes> <ccr> cksum
    uint8_t *rb = (uint8_t *)resultbuf;
    ck = 0;
    putc(SYNC_TX);
    putc(63);                                        // LEN = CMD + 61 payload + cksum
    putc(CMD_RESULT);                    ck += CMD_RESULT;
    for (int i = 0; i < 60; i++) { putc(rb[i]); ck += rb[i]; }
    putc(result_ccr);                    ck += result_ccr;
    putc(ck);
}

void kmain(void) {
    putc('R');                                       // boot marker: C harness booted + TX works
    for (;;) {
        if (getc() != SYNC_RX) continue;             // resync: read until the SYNC byte
        getc();                                      // LEN (fixed-format frames; consume)
        uint8_t cmd = getc();
        if (cmd == CMD_RUN) {
            run_case();
        } else if (cmd == CMD_PING) {
            uint8_t b = getc();                      // payload
            getc();                                  // cksum (ignored)
            putc(SYNC_TX); putc(0x03); putc(CMD_PONG); putc(b); putc((uint8_t)(CMD_PONG + b));
        } else if (cmd == CMD_ID) {                  // report BUILD_ID: proves what firmware is running
            getc(); getc();                          // payload byte + cksum (ignored)
            uint32_t id = BUILD_ID;
            uint8_t b3 = id >> 24, b2 = id >> 16, b1 = id >> 8, b0 = id;
            putc(SYNC_TX); putc(6); putc(CMD_IDR);   // LEN = CMD + 4 payload + cksum
            putc(b3); putc(b2); putc(b1); putc(b0);
            putc((uint8_t)(CMD_IDR + b3 + b2 + b1 + b0));
        }
        // Unknown CMD self-heals via the resync-on-SYNC above.
    }
}
