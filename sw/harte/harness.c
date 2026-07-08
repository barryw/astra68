// Astra 68 — Harte vector test harness ROM.
//
// Phase 0: prove the C harness boots and does bidirectional UART on silicon.
// Built with the SAME proven scaffolding as the CPU selftest (../boot/crt0.S +
// ../boot/astra_st.ld) — a working reset vector table, SSP setup, and an RTE
// _default_handler that survives spurious early exceptions. The bare-asm harness
// froze on the first peripheral read because its catch-all vector was STOP, not RTE.
//
// Oracle: host sends bytes, expects a leading 'R' then each byte echoed back.
// If 'R' streams and bytes echo, C boots + TX + RX all work — proceed to the
// PING/PONG protocol (proto.py / ping.py already exist host-side).
#include "vesta.h"

static void putc(char c) {
    while (!(VESTA->UART_STATUS & UART_TX_READY)) { }
    VESTA->UART_DATA = (uint8_t)c;
}

static uint8_t getc(void) {
    while (!(UART_RXSTATUS & UART_RX_READY)) { }
    return (uint8_t)UART_RXDATA;   // read consumes the byte (clears RX_READY)
}

void kmain(void) {
    putc('R');                     // boot marker: C harness booted + TX works
    for (;;) {
        putc(getc());              // echo: proves RX + the bidirectional path
    }
}
