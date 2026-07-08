// Astra 68 — SIM-only harness. Boots, sets up ONE Harte case in RAM, calls harte_exec,
// and marks progress over UART TX. No UART RX / protocol — the point is to reproduce the
// RAM-instruction-fetch hang in simulation and watch the bus, not to talk to a host.
//
// Expected TX: 'R' (boot) 'B' (before harte_exec) then either 'A' (executor returned) +
// endless '.', OR silence (the hang we're chasing). Reuses harness_exec.S (harte_exec).
#include "vesta.h"

// Same symbols harness_exec.S references (abs.L addressing), non-static.
uint32_t regtable[15];
uint32_t resultbuf[15];
uint8_t  ccr_in;
uint8_t  result_ccr;
uint8_t  codebuf[32] __attribute__((aligned(4)));
extern void harte_exec(void);

static void putc(uint8_t c) {
    while (!(VESTA->UART_STATUS & UART_TX_READY)) { }
    VESTA->UART_DATA = c;
}

void kmain(void) {
    putc('R');                                   // boot marker
    uint8_t *rt = (uint8_t *)regtable;
    for (int i = 0; i < 60; i++) rt[i] = 0;
    rt[3] = 1;                                    // d0 = 1 (big-endian low byte of long 0)
    rt[7] = 2;                                    // d1 = 2
    ccr_in = 0;
    codebuf[0] = 0xD0; codebuf[1] = 0x41;         // ADD.W D1,D0
    codebuf[2] = 0x4E; codebuf[3] = 0x75;         // trailing RTS
    putc('B');                                    // before executor
    harte_exec();
    putc('A');                                    // executor returned (no hang)
    putc('0' + (result_ccr & 7));                 // some result evidence
    for (;;) putc('.');
}
