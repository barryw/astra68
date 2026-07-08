// Astra 68 — Harte vector test harness ROM.
//
// Reuses the CPU selftest's proven scaffolding (../boot/crt0.S + ../boot/astra_st.ld):
// a working reset vector table, SSP setup, and an RTE _default_handler that survives
// spurious early exceptions. The bare-asm harness froze on its first peripheral read
// because its catch-all vector was STOP #0x2700, not RTE.
//
// Phase 0 (DONE, verified on silicon): 'R' boot marker + raw byte echo — proved C
// boots + TX + RX end-to-end.  Phase 1 (here): the host framing protocol.
//
// Wire protocol (host side = host/proto.py, the source of truth):
//   PING  host->dev : 0x55 0x03 0x02 <b> (0x02+b)&0xFF
//   PONG  dev->host : 0xAA 0x03 0x80 <b> (0x80+b)&0xFF
//   LEN = bytes after LEN = CMD + payload + cksum = 3.  cksum = sum(CMD..payload)&0xFF.
#include "vesta.h"

#define SYNC_RX   0x55u   // host -> device sync byte
#define SYNC_TX   0xAAu   // device -> host sync byte
#define CMD_PING  0x02u
#define CMD_PONG  0x80u

static void putc(uint8_t c) {
    while (!(VESTA->UART_STATUS & UART_TX_READY)) { }
    VESTA->UART_DATA = c;
}

static uint8_t getc(void) {
    while (!(UART_RXSTATUS & UART_RX_READY)) { }
    return (uint8_t)UART_RXDATA;   // read consumes the byte (clears RX_READY)
}

void kmain(void) {
    putc('R');                                  // boot marker: C harness booted + TX works
    for (;;) {
        if (getc() != SYNC_RX) continue;        // resync: read until the SYNC byte
        getc();                                 // LEN (fixed-format frames; consume)
        if (getc() == CMD_PING) {               // CMD
            uint8_t b = getc();                 // payload
            getc();                             // cksum (trusted link; verify if it ever flakes)
            putc(SYNC_TX);                      // PONG: 0xAA 0x03 0x80 <b> cksum
            putc(0x03);
            putc(CMD_PONG);
            putc(b);
            putc((uint8_t)(CMD_PONG + b));
        }
        // Unknown CMD: fall through; the resync-on-SYNC above self-heals the frame boundary.
        // RUN (0x01) = Task 5 per-case execution, added next.
    }
}
