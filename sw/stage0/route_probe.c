#include "vesta.h"
#include "vega.h"

#define VIDEO_PROBE_ROW 2u
#define VIDEO_PROBE_COL 2u
#define VIDEO_PROBE_OFFSET (VIDEO_PROBE_ROW * VEGA_POST_COLS + VIDEO_PROBE_COL)

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

static void video_probe(void)
{
    static const char banner[] = "ASTRA VIDEO PROBE";
    uint32_t id = VEGA->ID;
    uint32_t caps = VEGA->CAPS;
    uint32_t ctrl = VEGA->CTRL;
    uint32_t before = VEGA_POST_TEXT[VIDEO_PROBE_OFFSET];

    // Force the rescue text plane, then exercise the CPU-visible byte aperture.
    VEGA->CTRL = 0u;
    for (uint32_t index = 0; index < sizeof(banner) - 1u; ++index)
        VEGA_POST_TEXT[VIDEO_PROBE_OFFSET + index] = (uint8_t)banner[index];

    serial_puts("ASTRA VIDEO PROBE id=");
    serial_hex32(id);
    serial_puts(" caps=");
    serial_hex32(caps);
    serial_puts(" ctrl=");
    serial_hex32(ctrl);
    serial_puts(" before=");
    serial_hex32(before);
    serial_puts(" first=");
    serial_hex32(VEGA_POST_TEXT[VIDEO_PROBE_OFFSET]);
    serial_puts(" last=");
    serial_hex32(VEGA_POST_TEXT[VIDEO_PROBE_OFFSET + sizeof(banner) - 2u]);
    serial_putc('\n');
}

void stage0_main(void)
{
    for (;;) {
        video_probe();
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
