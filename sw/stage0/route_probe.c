#include "vesta.h"

static void serial_putc(char value)
{
    while ((VESTA->UART_STATUS & 1u) == 0u) {}
    VESTA->UART_DATA = (uint8_t)value;
}

static void serial_puts(const char *text)
{
    while (*text != '\0') {
        if (*text == '\n') serial_putc('\r');
        serial_putc(*text++);
    }
}

static void serial_hex32(uint32_t value)
{
    for (int shift = 28; shift >= 0; shift -= 4) {
        uint32_t digit = (value >> shift) & 0xfu;
        serial_putc((char)(digit < 10u ? '0' + digit : 'A' + digit - 10u));
    }
}

void stage0_main(void)
{
    for (;;) {
        serial_puts("ASTRA ROUTE PROBE id=");
        serial_hex32(VESTA->ID);
        serial_puts(" sys=");
        serial_hex32(VESTA->SYS_STATUS);
        serial_puts(" mem=");
        serial_hex32(VESTA->MEMTEST_STATUS);
        serial_puts(" err=");
        serial_hex32(VESTA->MEMTEST_ERRORS);
        serial_puts(" host=");
        serial_hex32(VESTA->HOST_STATUS);
        serial_puts(" cycles=");
        serial_hex32(VESTA->CPU_CYCLES_LO);
        serial_putc('\n');

        for (volatile uint32_t delay = 0; delay < 250000u; ++delay) {}
    }
}
