// Focused hardware diagnostic for Astraea command capture and range validation.
#include "vesta.h"
#include "vega.h"
#include "astraea.h"

#define PANEL_BASE      0xfff01000u
#define PANEL_LED_DATA  (*(volatile uint32_t *)(PANEL_BASE + 0x18u))
#define PANEL_LED_OWNER (*(volatile uint32_t *)(PANEL_BASE + 0x1cu))
#define TIMEOUT         20000000u

static uint32_t next_fence = 1u;

struct command_result {
    char tag;
    uint32_t dst;
    uint32_t pitch;
    uint32_t dim;
    uint32_t op;
    uint32_t status;
    uint32_t fence;
    uint8_t result;
};

static void serial_putc(char value)
{
    while ((VESTA->UART_STATUS & UART_TX_READY) == 0u) {}
    VESTA->UART_DATA = (uint8_t)value;
}

static void serial_puts(const char *text)
{
    while (*text != '\0')
        serial_putc(*text++);
}

static void serial_hex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";
    int shift;

    for (shift = 28; shift >= 0; shift -= 4)
        serial_putc(hex[(value >> shift) & 0x0fu]);
}

static void report_result(uint8_t code)
{
    static const char hex[] = "0123456789ABCDEF";

    serial_puts(code == 0u ? "GFX PASS" : "GFX F");
    if (code != 0u) {
        serial_putc(hex[code >> 4]);
        serial_putc(hex[code & 0x0fu]);
    }
    serial_putc('\n');
}

static void set_leds(uint8_t value)
{
    PANEL_LED_DATA = value;
    PANEL_LED_OWNER = 0xffu;
}

static int wait_sdram(void)
{
    uint32_t timeout = TIMEOUT;

    while ((VESTA->SYS_STATUS & SYS_SDRAM_READY) == 0u && timeout != 0u)
        --timeout;
    return timeout != 0u;
}

static void snapshot_command(struct command_result *result, char tag,
                             uint8_t code)
{
    result->tag = tag;
    result->dst = ASTRAEA->BLIT_DST;
    result->pitch = ASTRAEA->BLIT_DST_PITCH;
    result->dim = ASTRAEA->BLIT_DIM;
    result->op = ASTRAEA->BLIT_OP;
    result->status = ASTRAEA->BLIT_STATUS;
    result->fence = ASTRAEA->BLIT_FENCE;
    result->result = code;
}

static void report_command(const struct command_result *result)
{
    serial_puts("GFX ");
    serial_putc(result->tag);
    serial_puts(" dst=");
    serial_hex32(result->dst);
    serial_puts(" pitch=");
    serial_hex32(result->pitch);
    serial_puts(" dim=");
    serial_hex32(result->dim);
    serial_puts(" op=");
    serial_hex32(result->op);
    serial_puts(" status=");
    serial_hex32(result->status);
    serial_puts(" fence=");
    serial_hex32(result->fence);
    serial_puts(" result=");
    serial_hex32(result->result);
    serial_putc('\n');
}

static uint8_t run_fill(struct command_result *result, char tag,
                        uint16_t pitch, uint16_t height, int read_barrier)
{
    uint32_t timeout = TIMEOUT;
    uint32_t status;
    uint32_t fence = next_fence++;

    while ((ASTRAEA->BLIT_STATUS & BLIT_BUSY) != 0u && timeout != 0u)
        --timeout;
    if (timeout == 0u) {
        snapshot_command(result, tag, 0x0du);
        return 0x0du;
    }

    ASTRAEA->BLIT_SRC = 0u;
    ASTRAEA->BLIT_DST = 0u;
    ASTRAEA->BLIT_MASK = 0u;
    ASTRAEA->BLIT_SRC_PITCH = 0u;
    ASTRAEA->BLIT_DST_PITCH = pitch;
    ASTRAEA->BLIT_MASK_PITCH = 0u;
    ASTRAEA->BLIT_DIM = BLIT_DIM_(720u, height);
    ASTRAEA->BLIT_OP = BLIT_MODE_FILL | BLIT_ELEM8;
    ASTRAEA->BLIT_COLOR = 0u;
    ASTRAEA->BLIT_KEY = 0u;
    ASTRAEA->BLIT_FENCE = fence;

    if (read_barrier &&
        (ASTRAEA->BLIT_DST != 0u ||
         ASTRAEA->BLIT_DST_PITCH != pitch ||
         ASTRAEA->BLIT_DIM != BLIT_DIM_(720u, height) ||
         ASTRAEA->BLIT_OP != (BLIT_MODE_FILL | BLIT_ELEM8))) {
        snapshot_command(result, tag, 0x0cu);
        return 0x0cu;
    }

    ASTRAEA->BLIT_CTRL = BLIT_START;
    timeout = TIMEOUT;
    while ((((ASTRAEA->BLIT_STATUS & BLIT_DONE) == 0u) ||
            ASTRAEA->BLIT_FENCE != fence) && timeout != 0u)
        --timeout;
    if (timeout == 0u) {
        snapshot_command(result, tag, 0x0eu);
        return 0x0eu;
    }

    timeout = TIMEOUT;
    while ((ASTRAEA->BLIT_STATUS & BLIT_BUSY) != 0u && timeout != 0u)
        --timeout;
    status = ASTRAEA->BLIT_STATUS;
    if (timeout == 0u) {
        snapshot_command(result, tag, 0x0du);
        return 0x0du;
    }
    if (BLIT_ERROR_CODE(status) != 0u) {
        uint8_t code = (uint8_t)BLIT_ERROR_CODE(status);

        snapshot_command(result, tag, code);
        return code;
    }
    if (ASTRAEA->BLIT_FENCE != fence) {
        snapshot_command(result, tag, 0x0fu);
        return 0x0fu;
    }
    snapshot_command(result, tag, 0u);
    return 0u;
}

static void finish(uint8_t code, const struct command_result *results,
                   unsigned int result_count)
{
    unsigned int i;

    VEGA->BACKDROP = code == 0u ? VEGA_RGB(0, 96, 0) : VEGA_RGB(255, 0, 0);
    VEGA->CTRL = VEGA_CTRL_DISPLAY_EN | VEGA_CTRL_BACKDROP_EN;
    set_leds(code == 0u ? 0x5au : (uint8_t)(0x80u | (code & 0x7fu)));
    for (;;) {
        for (i = 0; i < result_count; ++i)
            report_command(&results[i]);
        report_result(code);
    }
}

void kmain(void)
{
    uint8_t baseline;
    uint8_t tall;
    uint8_t tall_barrier;
    uint8_t pitched;
    uint8_t final_code = 0u;
    struct command_result results[4];

    set_leds(0x40u);
    if (VESTA->ID != VESTA_ID_MAGIC || VEGA->ID != VEGA_ID_MAGIC ||
        ASTRAEA->ID != ASTRAEA_ID_MAGIC || !wait_sdram())
        finish(0x11u, (const struct command_result *)0, 0u);

    baseline = run_fill(&results[0], 'A', 0u, 1u, 0);
    tall = run_fill(&results[1], 'B', 0u, 480u, 0);
    tall_barrier = run_fill(&results[2], 'C', 0u, 480u, 1);
    pitched = run_fill(&results[3], 'D', 720u, 480u, 1);

    if (baseline != 0u)
        final_code = (uint8_t)(0xa0u | baseline);
    else if (tall != 0u) {
        if (tall_barrier == 0u)
            final_code = 0xe1u;
        else
            final_code = (uint8_t)(0xb0u | tall);
    } else if (tall_barrier != 0u)
        final_code = (uint8_t)(0xc0u | tall_barrier);
    else if (pitched != 0u)
        final_code = (uint8_t)(0xd0u | pitched);

    finish(final_code, results, 4u);
}
