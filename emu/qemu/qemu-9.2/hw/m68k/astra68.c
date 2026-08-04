/*
 * Astra 68 reference machine for QEMU 9.2.
 *
 * This machine intentionally mirrors the physical Astra memory map.  Device
 * policy remains in Astra OS; these models provide only the hardware contract
 * required to run the unchanged boot ROM and Axiom K1-K10 suite.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"

#define ASTRA_BRAM_BASE          0x01ff8000u
#define ASTRA_BRAM_SIZE          (32 * KiB)
#define ASTRA_SDRAM_BASE         0x02000000u
#define ASTRA_SDRAM_SIZE         (32 * MiB)
#define ASTRA_ROM_BASE           0xffe00000u
#define ASTRA_ROM_SIZE           (256 * KiB)
#define ASTRA_VESTA_BASE         0xfff00000u
#define ASTRA_VESTA_SIZE         0x800u
#define ASTRA_PANEL_BASE         0xfff01000u
#define ASTRA_PANEL_SIZE         0x100u
#define ASTRA_ASTRAEA_BASE       0xfff10000u
#define ASTRA_ASTRAEA_SIZE       0x8000u
#define ASTRA_VEGA_BASE          0xfff20000u
#define ASTRA_VEGA_SIZE          0x2000u
#define ASTRA_TEXT_BASE          0xfff22000u
#define ASTRA_TEXT_SIZE          0x1000u

#define ASTRA_CPU_HZ             12500000ull
#define ASTRA_BUILD_ID           0x18ebe2e1u
#define ASTRA_KERNEL_READY       0x4b314f4bu
#define ASTRA_KERNEL_SOAK        0x4b31534bu
#define ASTRA_KERNEL_PANIC       0x4b50414eu

#define TIMER_ENABLE             (1u << 0)
#define TIMER_PERIODIC           (1u << 1)
#define TIMER_IRQ_ENABLE         (1u << 2)
#define TIMER_EXPIRED            (1u << 0)
#define VEGA_IRQ_VBLANK          (1u << 0)
#define ASTRAEA_IRQ_BLIT_DONE    (1u << 0)
#define ASTRAEA_IRQ_DRAW_DONE    (1u << 3)
#define IRQ_SOURCE_VEGA          8
#define IRQ_SOURCE_ASTRAEA       9
#define IRQ_VALID                (1u << 31)

typedef struct Astra68State Astra68State;

typedef struct AstraTimer {
    Astra68State *machine;
    QEMUTimer *qemu_timer;
    uint32_t load;
    uint32_t control;
    uint32_t status;
    int index;
} AstraTimer;

typedef struct AstraeaState {
    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t src;
    uint32_t dst;
    uint32_t mask;
    uint32_t src_pitch;
    uint32_t dst_pitch;
    uint32_t mask_pitch;
    uint32_t dim;
    uint32_t op;
    uint32_t color;
    uint32_t key;
    uint32_t status;
    uint32_t fence;
    uint32_t draw_status;
    uint32_t draw_fence;
} AstraeaState;

typedef struct VegaState {
    QEMUTimer *vblank_timer;
    uint32_t irq_enable;
    uint32_t irq_status;
    uint32_t frame_counter;
    uint32_t regs[ASTRA_VEGA_SIZE / 4];
} VegaState;

struct Astra68State {
    M68kCPU *cpu;
    MemoryRegion bram;
    MemoryRegion rom;
    MemoryRegion rom_alias;
    MemoryRegion text;
    MemoryRegion vesta_io;
    MemoryRegion panel_io;
    MemoryRegion astraea_io;
    MemoryRegion vega_io;
    uint8_t *sdram;
    uint64_t reset_clock_ns;
    uint32_t initial_sp;
    uint32_t initial_pc;
    uint32_t scratch;
    uint32_t irq_enable;
    uint32_t irq_soft;
    uint32_t irq_config[32];
    AstraTimer timers[2];
    AstraeaState astraea;
    VegaState vega;
    uint8_t panel_led_data;
    uint8_t panel_led_ownership;
    bool trace_timers;
};

static uint64_t astra_now_cycles(Astra68State *s)
{
    uint64_t elapsed = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - s->reset_clock_ns;
    return muldiv64(elapsed, ASTRA_CPU_HZ, NANOSECONDS_PER_SECOND);
}

static uint64_t astra_timer_scale(const AstraTimer *timer)
{
    return 1ull << ((timer->control >> 4) & 0xf);
}

static uint64_t astra_timer_period_ns(const AstraTimer *timer)
{
    uint64_t cycles = MAX(timer->load, 1u) * astra_timer_scale(timer);
    return muldiv64(cycles, NANOSECONDS_PER_SECOND, ASTRA_CPU_HZ);
}

static uint32_t astra_pending_raw(Astra68State *s)
{
    uint32_t pending = s->irq_soft;
    int i;

    for (i = 0; i < 2; i++) {
        AstraTimer *timer = &s->timers[i];
        if ((timer->status & TIMER_EXPIRED) &&
            (timer->control & TIMER_IRQ_ENABLE)) {
            pending |= 1u << i;
        }
    }
    if (s->vega.irq_status & s->vega.irq_enable) {
        pending |= 1u << IRQ_SOURCE_VEGA;
    }
    if (s->astraea.irq_status & s->astraea.irq_enable) {
        pending |= 1u << IRQ_SOURCE_ASTRAEA;
    }
    return pending;
}

static void astra_update_irq(Astra68State *s)
{
    uint32_t pending = astra_pending_raw(s) & s->irq_enable;
    int best_level = 0;
    int best_source = -1;
    int source;

    for (source = 0; source < 32; source++) {
        int level;
        if (!(pending & (1u << source))) {
            continue;
        }
        level = s->irq_config[source] & 7;
        if (level > best_level) {
            best_level = level;
            best_source = source;
        }
    }

    if (best_source >= 0) {
        m68k_set_irq_level(s->cpu, best_level,
                           (s->irq_config[best_source] >> 8) & 0xff);
    } else {
        m68k_set_irq_level(s->cpu, 0, 0);
    }
}

static void astra_timer_expired(void *opaque)
{
    AstraTimer *timer = opaque;
    Astra68State *s = timer->machine;

    timer->status |= TIMER_EXPIRED;
    if (s->trace_timers) {
        fprintf(stderr, "ASTRA68 timer expired load=%" PRIu32
                " control=%08" PRIx32 " now=%" PRIu64 "\n",
                timer->load, timer->control,
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    }
    if (timer->control & TIMER_PERIODIC) {
        timer_mod_ns(timer->qemu_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     astra_timer_period_ns(timer));
    } else {
        timer->control &= ~TIMER_ENABLE;
    }
    astra_update_irq(s);
}

static void astra_vblank(void *opaque)
{
    Astra68State *s = opaque;

    s->vega.frame_counter++;
    s->vega.irq_status |= VEGA_IRQ_VBLANK;
    timer_mod_ns(s->vega.vblank_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                 NANOSECONDS_PER_SECOND / 60);
    astra_update_irq(s);
}

static void astra_bist_materialize(Astra68State *s)
{
    uint32_t address;

    for (address = 0; address < ASTRA_SDRAM_SIZE; address++) {
        uint8_t pattern = (address & 0xff) ^ ((address >> 8) & 0xff) ^
                          ((address >> 16) & 0xff) ^ ((address >> 24) & 1) ^
                          0xa5;
        s->sdram[address] = ~pattern;
    }
}

static uint32_t astra_irq_current(Astra68State *s)
{
    uint32_t pending = astra_pending_raw(s) & s->irq_enable;
    int level;
    int source;

    for (level = 7; level > 0; level--) {
        for (source = 0; source < 32; source++) {
            uint32_t config = s->irq_config[source];
            if ((pending & (1u << source)) && (config & 7) == level) {
                s->irq_enable &= ~(1u << source);
                astra_update_irq(s);
                return IRQ_VALID | (((config >> 8) & 0xff) << 16) |
                       (source << 8) | level;
            }
        }
    }
    return 0;
}

static uint32_t astra_vesta_read32(Astra68State *s, hwaddr offset)
{
    uint64_t cycles;
    int timer_index;
    AstraTimer *timer;

    switch (offset) {
    case 0x000: return 0x56535441; /* VSTA */
    case 0x004: return 0x00010000;
    case 0x008: return 0x41363801;
    case 0x010: return 0x0000000f;
    case 0x018: return s->scratch;
    case 0x01c: return 0x00068030;
    case 0x020: return 0x54474d32;
    case 0x024: return 0x0000000d;
    case 0x028: return ASTRA_CPU_HZ;
    case 0x02c: return ASTRA_SDRAM_BASE;
    case 0x030: return ASTRA_SDRAM_SIZE;
    case 0x034: return ASTRA_ROM_BASE;
    case 0x038: return ASTRA_ROM_SIZE;
    case 0x03c: return ASTRA_BUILD_ID;
    case 0x040: return 3;
    case 0x044: return 16;
    case 0x050: return 0x56535441;
    case 0x054: return 0x00010000;
    case 0x058: return ASTRA_VESTA_BASE;
    case 0x05c: return 0x00010000;
    case 0x060: return 0x41535452;
    case 0x064: return 0x00010000;
    case 0x068: return ASTRA_ASTRAEA_BASE;
    case 0x06c: return 0x00010000;
    case 0x070: return 0x56454741;
    case 0x074: return 0x00010000;
    case 0x078: return ASTRA_VEGA_BASE;
    case 0x07c: return 0x00010000;
    case 0x0d4: return 0x00000302; /* completed, phase done */
    case 0x0d8: return ASTRA_SDRAM_SIZE - 4;
    case 0x0ec:
        cycles = astra_now_cycles(s);
        return cycles;
    case 0x0f0:
        cycles = astra_now_cycles(s);
        return cycles >> 32;
    case 0x300: return astra_pending_raw(s);
    case 0x304: return s->irq_enable;
    case 0x308: return s->irq_soft;
    case 0x310: return astra_irq_current(s);
    case 0x504: return 1;
    default:
        if (offset >= 0x380 && offset <= 0x3fc && !(offset & 3)) {
            return s->irq_config[(offset - 0x380) / 4];
        }
        if (offset >= 0x400 && offset <= 0x41c && !(offset & 3)) {
            timer_index = (offset - 0x400) / 0x10;
            timer = &s->timers[timer_index];
            switch (offset & 0xf) {
            case 0x0: return timer->load;
            case 0x4:
                if (!(timer->control & TIMER_ENABLE) ||
                    !timer_pending(timer->qemu_timer)) {
                    return 0;
                }
                return muldiv64(timer_expire_time_ns(timer->qemu_timer) -
                                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL),
                                ASTRA_CPU_HZ, NANOSECONDS_PER_SECOND) /
                       astra_timer_scale(timer);
            case 0x8: return timer->control;
            case 0xc: return timer->status;
            }
        }
        return 0;
    }
}

static void astra_finish(Astra68State *s, uint32_t value)
{
    const char *result;

    if (value == ASTRA_KERNEL_PANIC) {
        result = "PANIC";
    } else if (value == ASTRA_KERNEL_SOAK) {
        result = "SOAK";
    } else {
        result = "READY";
    }
    fprintf(stderr, "\nASTRA68-QEMU %s cycles=%" PRIu64
            " pc=%08x scratch=%08x\n", result, astra_now_cycles(s),
            s->cpu->env.pc, value);
    qemu_system_shutdown_request(value == ASTRA_KERNEL_PANIC ?
                                 SHUTDOWN_CAUSE_GUEST_PANIC :
                                 SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
}

static void astra_vesta_write32(Astra68State *s, hwaddr offset,
                                uint32_t value)
{
    int timer_index;
    AstraTimer *timer;

    switch (offset) {
    case 0x018:
        s->scratch = value;
        if (value == ASTRA_KERNEL_READY || value == ASTRA_KERNEL_SOAK ||
            value == ASTRA_KERNEL_PANIC) {
            astra_finish(s, value);
        }
        break;
    case 0x0d0:
        if (value & 1) {
            astra_bist_materialize(s);
        }
        break;
    case 0x304:
        s->irq_enable = value;
        astra_update_irq(s);
        break;
    case 0x308:
        s->irq_soft = value;
        astra_update_irq(s);
        break;
    case 0x30c:
        s->irq_soft &= ~value;
        astra_update_irq(s);
        break;
    case 0x500:
        fputc(value & 0xff, stdout);
        fflush(stdout);
        break;
    default:
        if (offset >= 0x380 && offset <= 0x3fc && !(offset & 3)) {
            s->irq_config[(offset - 0x380) / 4] = value & 0x0001ffff;
            astra_update_irq(s);
        } else if (offset >= 0x400 && offset <= 0x41c && !(offset & 3)) {
            timer_index = (offset - 0x400) / 0x10;
            timer = &s->timers[timer_index];
            switch (offset & 0xf) {
            case 0x0:
                timer->load = value;
                break;
            case 0x8:
                timer->control = value & 0xf7;
                timer_del(timer->qemu_timer);
                if (timer->control & TIMER_ENABLE) {
                    if (s->trace_timers) {
                        fprintf(stderr, "ASTRA68 timer arm index=%d load=%" PRIu32
                                " control=%08" PRIx32 " period_ns=%" PRIu64
                                " now=%" PRIu64 "\n",
                                timer_index, timer->load, timer->control,
                                astra_timer_period_ns(timer),
                                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
                    }
                    timer_mod_ns(timer->qemu_timer,
                                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                 astra_timer_period_ns(timer));
                }
                astra_update_irq(s);
                break;
            case 0xc:
                timer->status &= ~value;
                astra_update_irq(s);
                break;
            }
        }
        break;
    }
}

static uint32_t astra_panel_read32(Astra68State *s, hwaddr offset)
{
    switch (offset) {
    case 0x00: return 0x504e4c30;
    case 0x04: return 0x00010000;
    case 0x08: return 0x0f040608;
    case 0x18: return s->panel_led_data;
    case 0x1c: return s->panel_led_ownership;
    default: return 0;
    }
}

static void astra_panel_write32(Astra68State *s, hwaddr offset,
                                uint32_t value)
{
    switch (offset) {
    case 0x18: s->panel_led_data = value; break;
    case 0x1c: s->panel_led_ownership = value; break;
    case 0x20: s->panel_led_data |= value; break;
    case 0x24: s->panel_led_data &= ~value; break;
    case 0x28: s->panel_led_data ^= value; break;
    }
}

static bool astra_sdram_span(uint32_t offset, uint32_t bytes)
{
    return offset <= ASTRA_SDRAM_SIZE && bytes <= ASTRA_SDRAM_SIZE - offset;
}

static void astra_blit(Astra68State *s)
{
    AstraeaState *a = &s->astraea;
    uint32_t width = a->dim & 0xffff;
    uint32_t height = a->dim >> 16;
    uint32_t elem = 1u << ((a->op >> 4) & 3);
    uint32_t row_bytes;
    uint32_t row;

    if (elem > 4 || __builtin_mul_overflow(width, elem, &row_bytes)) {
        a->status = 2 | (1 << 8);
        goto done;
    }
    for (row = 0; row < height; row++) {
        uint32_t dst = a->dst + row * a->dst_pitch;
        uint32_t src = a->src + row * a->src_pitch;
        if (!astra_sdram_span(dst, row_bytes) ||
            ((a->op & 0xf) == 0 && !astra_sdram_span(src, row_bytes))) {
            a->status = 2 | (2 << 8);
            goto done;
        }
        if ((a->op & 0xf) == 0) {
            memmove(s->sdram + dst, s->sdram + src, row_bytes);
        } else if ((a->op & 0xf) == 1) {
            uint8_t color[4];
            uint32_t x;
            stl_be_p(color, a->color);
            for (x = 0; x < row_bytes; x += elem) {
                memcpy(s->sdram + dst + x, color + 4 - elem, elem);
            }
        } else {
            a->status = 2 | (1 << 8);
            goto done;
        }
    }
    a->status = 2;
done:
    a->irq_status |= ASTRAEA_IRQ_BLIT_DONE;
    astra_update_irq(s);
}

static uint32_t astra_astraea_read32(Astra68State *s, hwaddr offset)
{
    AstraeaState *a = &s->astraea;
    switch (offset) {
    case 0x000: return 0x41535452;
    case 0x004: return 0x00040000;
    case 0x010: return a->irq_enable;
    case 0x014: return a->irq_status;
    case 0x018: return 0x000000ff;
    case 0x040: return a->src;
    case 0x044: return a->dst;
    case 0x048: return a->mask;
    case 0x04c: return a->src_pitch;
    case 0x050: return a->dst_pitch;
    case 0x054: return a->mask_pitch;
    case 0x058: return a->dim;
    case 0x05c: return a->op;
    case 0x060: return a->color;
    case 0x064: return a->key;
    case 0x06c: return a->status;
    case 0x070: return a->fence;
    case 0x154: return a->draw_status;
    case 0x158: return a->draw_fence;
    default: return 0;
    }
}

static void astra_astraea_write32(Astra68State *s, hwaddr offset,
                                  uint32_t value)
{
    AstraeaState *a = &s->astraea;
    switch (offset) {
    case 0x010: a->irq_enable = value & 9; astra_update_irq(s); break;
    case 0x014: a->irq_status &= ~value; astra_update_irq(s); break;
    case 0x040: a->src = value; break;
    case 0x044: a->dst = value; break;
    case 0x048: a->mask = value; break;
    case 0x04c: a->src_pitch = value; break;
    case 0x050: a->dst_pitch = value; break;
    case 0x054: a->mask_pitch = value; break;
    case 0x058: a->dim = value; break;
    case 0x05c: a->op = value; break;
    case 0x060: a->color = value; break;
    case 0x064: a->key = value; break;
    case 0x068: if (value & 1) astra_blit(s); break;
    case 0x070: a->fence = value; break;
    case 0x150:
        if (value & 1) {
            a->draw_status = 2;
            a->irq_status |= ASTRAEA_IRQ_DRAW_DONE;
            astra_update_irq(s);
        }
        break;
    case 0x158: a->draw_fence = value; break;
    }
}

static uint32_t astra_vega_read32(Astra68State *s, hwaddr offset)
{
    switch (offset) {
    case 0x000: return 0x56454741;
    case 0x004: return 0x00050000;
    case 0x010: return s->vega.irq_enable;
    case 0x014: return s->vega.irq_status;
    case 0x01c: return 0x00000077;
    case 0x054: return s->vega.regs[0x54 / 4];
    case 0x064: return s->vega.frame_counter;
    default: return s->vega.regs[offset / 4];
    }
}

static void astra_vega_write32(Astra68State *s, hwaddr offset, uint32_t value)
{
    switch (offset) {
    case 0x010:
        s->vega.irq_enable = value & 7;
        break;
    case 0x014:
        s->vega.irq_status &= ~value;
        break;
    case 0x050:
        if (value & 1) {
            s->vega.regs[0x54 / 4] = 4;
            s->vega.regs[0x58 / 4] = s->vega.regs[0x34 / 4];
            s->vega.regs[0x5c / 4] = s->vega.frame_counter;
        }
        break;
    case 0x054:
        s->vega.regs[0x54 / 4] &= ~value;
        break;
    default:
        s->vega.regs[offset / 4] = value;
        break;
    }
    astra_update_irq(s);
}

typedef uint32_t (*AstraRead32)(Astra68State *, hwaddr);
typedef void (*AstraWrite32)(Astra68State *, hwaddr, uint32_t);

static uint64_t astra_mmio_read(void *opaque, hwaddr offset, unsigned size,
                                AstraRead32 read32)
{
    Astra68State *s = opaque;
    hwaddr aligned = offset & ~3;
    uint32_t value = read32(s, aligned);
    unsigned shift = (4 - size - (offset & 3)) * 8;
    return value >> shift;
}

static void astra_mmio_write(void *opaque, hwaddr offset, uint64_t value,
                             unsigned size, AstraWrite32 write32)
{
    Astra68State *s = opaque;
    hwaddr aligned = offset & ~3;

    if (size == 4) {
        write32(s, aligned, value);
    } else if (aligned == 0x500 || aligned == 0x0d0) {
        write32(s, aligned, value);
    }
}

#define ASTRA_MMIO_WRAPPERS(name)                                           \
    static uint64_t name##_read(void *opaque, hwaddr offset, unsigned size) \
    { return astra_mmio_read(opaque, offset, size, name##_read32); }         \
    static void name##_write(void *opaque, hwaddr offset, uint64_t value,    \
                             unsigned size)                                  \
    { astra_mmio_write(opaque, offset, value, size, name##_write32); }

static uint32_t astra_vesta_read32_wrap(Astra68State *s, hwaddr o)
{ return astra_vesta_read32(s, o); }
static void astra_vesta_write32_wrap(Astra68State *s, hwaddr o, uint32_t v)
{ astra_vesta_write32(s, o, v); }
static uint32_t astra_panel_read32_wrap(Astra68State *s, hwaddr o)
{ return astra_panel_read32(s, o); }
static void astra_panel_write32_wrap(Astra68State *s, hwaddr o, uint32_t v)
{ astra_panel_write32(s, o, v); }
static uint32_t astra_astraea_read32_wrap(Astra68State *s, hwaddr o)
{ return astra_astraea_read32(s, o); }
static void astra_astraea_write32_wrap(Astra68State *s, hwaddr o, uint32_t v)
{ astra_astraea_write32(s, o, v); }
static uint32_t astra_vega_read32_wrap(Astra68State *s, hwaddr o)
{ return astra_vega_read32(s, o); }
static void astra_vega_write32_wrap(Astra68State *s, hwaddr o, uint32_t v)
{ astra_vega_write32(s, o, v); }

#define astra_vesta_read32 astra_vesta_read32_wrap
#define astra_vesta_write32 astra_vesta_write32_wrap
ASTRA_MMIO_WRAPPERS(astra_vesta)
#undef astra_vesta_read32
#undef astra_vesta_write32
#define astra_panel_read32 astra_panel_read32_wrap
#define astra_panel_write32 astra_panel_write32_wrap
ASTRA_MMIO_WRAPPERS(astra_panel)
#undef astra_panel_read32
#undef astra_panel_write32
#define astra_astraea_read32 astra_astraea_read32_wrap
#define astra_astraea_write32 astra_astraea_write32_wrap
ASTRA_MMIO_WRAPPERS(astra_astraea)
#undef astra_astraea_read32
#undef astra_astraea_write32
#define astra_vega_read32 astra_vega_read32_wrap
#define astra_vega_write32 astra_vega_write32_wrap
ASTRA_MMIO_WRAPPERS(astra_vega)
#undef astra_vega_read32
#undef astra_vega_write32

#define ASTRA_OPS(name) {                         \
    .read = name##_read,                          \
    .write = name##_write,                        \
    .endianness = DEVICE_BIG_ENDIAN,              \
    .valid.min_access_size = 1,                   \
    .valid.max_access_size = 4,                   \
    .impl.min_access_size = 1,                    \
    .impl.max_access_size = 4,                    \
}

static const MemoryRegionOps astra_vesta_ops = ASTRA_OPS(astra_vesta);
static const MemoryRegionOps astra_panel_ops = ASTRA_OPS(astra_panel);
static const MemoryRegionOps astra_astraea_ops = ASTRA_OPS(astra_astraea);
static const MemoryRegionOps astra_vega_ops = ASTRA_OPS(astra_vega);

static void astra_cpu_reset(void *opaque)
{
    Astra68State *s = opaque;

    cpu_reset(CPU(s->cpu));
    s->cpu->env.aregs[7] = s->initial_sp;
    s->cpu->env.pc = s->initial_pc;
    s->reset_clock_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
}

static void astra68_init(MachineState *machine)
{
    Astra68State *s = g_new0(Astra68State, 1);
    MemoryRegion *sysmem = get_system_memory();
    const char *firmware = machine->firmware;
    char *filename;
    char *contents;
    gsize firmware_size;
    GError *gerror = NULL;
    int i;

    s->trace_timers = g_getenv("ASTRA_QEMU_TIMER_TRACE") != NULL;

    if (machine->ram_size != ASTRA_SDRAM_SIZE) {
        error_report("Astra68 requires exactly 32 MiB for the K1-K10 profile");
        exit(EXIT_FAILURE);
    }
    if (!firmware) {
        error_report("Astra68 requires -bios <astra_boot.bin>");
        exit(EXIT_FAILURE);
    }

    s->cpu = M68K_CPU(cpu_create(machine->cpu_type));
    qemu_register_reset(astra_cpu_reset, s);

    memory_region_add_subregion(sysmem, ASTRA_SDRAM_BASE, machine->ram);
    s->sdram = memory_region_get_ram_ptr(machine->ram);
    memory_region_init_ram(&s->bram, NULL, "astra68.bram", ASTRA_BRAM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, ASTRA_BRAM_BASE, &s->bram);

    memory_region_init_rom(&s->rom, NULL, "astra68.rom", ASTRA_ROM_SIZE,
                           &error_fatal);
    memory_region_add_subregion(sysmem, ASTRA_ROM_BASE, &s->rom);
    memory_region_init_alias(&s->rom_alias, NULL, "astra68.rom-alias",
                             &s->rom, 0, ASTRA_ROM_SIZE);
    memory_region_add_subregion(sysmem, 0, &s->rom_alias);

    filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, firmware);
    if (!filename || load_image_mr(filename, &s->rom) < 8 ||
        !g_file_get_contents(filename, &contents, &firmware_size, &gerror)) {
        error_report("cannot load Astra68 ROM '%s'%s%s", firmware,
                     gerror ? ": " : "", gerror ? gerror->message : "");
        exit(EXIT_FAILURE);
    }
    if (firmware_size > ASTRA_ROM_SIZE) {
        error_report("Astra68 ROM is too large: %zu bytes", firmware_size);
        exit(EXIT_FAILURE);
    }
    s->initial_sp = ldl_be_p(contents);
    s->initial_pc = ldl_be_p(contents + 4);
    g_free(contents);
    g_free(filename);

    memory_region_init_ram(&s->text, NULL, "astra68.post-text",
                           ASTRA_TEXT_SIZE, &error_fatal);
    memory_region_add_subregion(sysmem, ASTRA_TEXT_BASE, &s->text);

    memory_region_init_io(&s->vesta_io, NULL, &astra_vesta_ops, s,
                          "astra68.vesta", ASTRA_VESTA_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_VESTA_BASE, &s->vesta_io);
    memory_region_init_io(&s->panel_io, NULL, &astra_panel_ops, s,
                          "astra68.panel", ASTRA_PANEL_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_PANEL_BASE, &s->panel_io);
    memory_region_init_io(&s->astraea_io, NULL, &astra_astraea_ops, s,
                          "astra68.astraea", ASTRA_ASTRAEA_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_ASTRAEA_BASE, &s->astraea_io);
    memory_region_init_io(&s->vega_io, NULL, &astra_vega_ops, s,
                          "astra68.vega", ASTRA_VEGA_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_VEGA_BASE, &s->vega_io);

    for (i = 0; i < 2; i++) {
        s->timers[i].machine = s;
        s->timers[i].index = i;
        s->timers[i].qemu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                               astra_timer_expired,
                                               &s->timers[i]);
    }
    s->vega.vblank_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, astra_vblank, s);
    timer_mod_ns(s->vega.vblank_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                 NANOSECONDS_PER_SECOND / 60);

    astra_cpu_reset(s);
}

static void astra68_machine_init(MachineClass *mc)
{
    mc->desc = "Astra 68 reference machine";
    mc->init = astra68_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68030");
    mc->default_ram_size = ASTRA_SDRAM_SIZE;
    mc->default_ram_id = "astra68.sdram";
    mc->max_cpus = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

DEFINE_MACHINE("astra68", astra68_machine_init)
