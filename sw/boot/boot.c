// Astra 68 — minimal boot ROM: banner over the Vesta UART.
// First software milestone (SPEC §17.2). Uses the verified chipset headers.
#include "vesta.h"

static void uart_putc(char c) {
    while (!(VESTA->UART_STATUS & UART_TX_READY)) { }
    VESTA->UART_DATA = (uint8_t)c;
}

static void uart_puts(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s);
    }
}

void kmain(void) {
    // Default UART divisor (host is 115200; sysclk/baud set by the RTL).
    // Bring-up: repeat the banner so a serial reader attached after the FPGA
    // has already configured still catches it (the config port can't be shared
    // with the flasher, so a one-shot banner is unobservable remotely).
    for (;;) {
        uart_puts("\nASTRA 68 SYSTEM ROM v0.1\n\n");
        uart_puts("CPU:    68030-class (WF68K30L)\n");
        uart_puts("RAM:    32768 KB\n");
        uart_puts("VIDEO:  RGB565 framebuffer (Vega)\n");
        uart_puts("AUDIO:  16 PCM + 16 wavetable (Lyra)\n");
        uart_puts("MMU:    region protection (Vesta)\n\n");
        uart_puts("READY.\n");
        for (volatile unsigned i = 0; i < 800000u; i++) { }  // ~1-2 s at 10 MHz
    }
}
