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
#include "qemu/atomic.h"
#ifdef CONFIG_LINUX
#include "qemu/futex.h"
#endif
#include "qemu/iov.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/m68k/astra_input.h"
#include "hw/m68k/astra_display_mailbox.h"
#include "hw/m68k/astra_render_batch.h"
#include "hw/m68k/astra_render_protocol.h"
#include "astra/display.h"
#include "sysemu/block-backend.h"
#include "sysemu/block-backend-io.h"
#include "sysemu/blockdev.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "ui/input.h"

#define ASTRA_BRAM_BASE          0x01ff8000u
#define ASTRA_BRAM_SIZE          (32 * KiB)
#define ASTRA_SDRAM_BASE         0x02000000u
#define ASTRA_SDRAM_PHYSICAL_SIZE (32 * MiB)
#define ASTRA_SDRAM_HOSTED_SIZE  (128 * MiB)
#define ASTRA_ROM_BASE           0xffe00000u
#define ASTRA_ROM_SIZE           (256 * KiB)
#define ASTRA_VESTA_BASE         0xfff00000u
#define ASTRA_VESTA_SIZE         0x800u
#define ASTRA_PANEL_BASE         0xfff01000u
#define ASTRA_PANEL_SIZE         0x100u
#define ASTRA_PANEL_ACTIVITY     0x2cu
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
#define IRQ_SOURCE_INPUT         5
#define IRQ_SOURCE_STORAGE       4
#define IRQ_VALID                (1u << 31)

#define SYS_STATUS_BASE          0x0000000fu
#define SYS_STATUS_ASTRA_HOST    (1u << 5)

/*
 * AstraHost runtime block service, Vesta offsets 0x150..0x1b0. The register
 * contract and 512-byte sector are defined by sw/include/vesta.h and
 * docs/ASTRAHOST.md. The maximum transfer is reported at runtime, allowing
 * this hosted backend to batch more sectors than the physical SPI service.
 */
#define BLOCK_ID_MAGIC           0x484f5354u /* "HOST" */
#define BLOCK_VERSION_1_0        0x00010000u
#define BLOCK_SECTOR_SIZE        512u
#define BLOCK_MAX_SECTORS        128u
#define BLOCK_CAP_READ           (1u << 0)
#define BLOCK_CAP_WRITE          (1u << 1)
#define BLOCK_CAP_FLUSH          (1u << 2)
#define BLOCK_STATE_LINK_UP      (1u << 0)
#define BLOCK_STATE_MEDIA_PRESENT (1u << 1)
#define BLOCK_STATE_WRITE_ENABLE (1u << 2)
#define BLOCK_QUEUE_COMPLETION_VALID (1u << 20)
#define BLOCK_QUEUE_COMPLETION_SHIFT 12
#define BLOCK_QUEUE_REQUEST_READY (1u << 8)
#define BLOCK_OP_READ            1u
#define BLOCK_OP_WRITE           2u
#define BLOCK_OP_FLUSH           3u
#define BLOCK_SUBMIT             (1u << 0)
#define BLOCK_CPL_POP_BIT        (1u << 0)
#define BLOCK_STATE_ACK_BIT      (1u << 0)
#define BLOCK_ERROR_BAD_OP       (1u << 0)
#define BLOCK_ERROR_BAD_COUNT    (1u << 1)
#define BLOCK_ERROR_BAD_BUFFER   (1u << 2)
#define BLOCK_ERROR_NO_MEDIA     (1u << 3)
#define BLOCK_ERROR_WRITE_PROTECT (1u << 4)
#define BLOCK_ERROR_LBA_RANGE    (1u << 5)
#define BLOCK_ERROR_QUEUE_FULL   (1u << 6)
#define BLOCK_ERROR_BAD_ID       (1u << 7)
#define BLOCK_ERROR_BAD_FLAGS    (1u << 8)

#define BLOCK_COMPLETION_OK      0u
#define BLOCK_COMPLETION_IO_ERROR 1u

#define BLOCK_COMPLETION_QUEUE_SIZE 4u
#define BLOCK_COMPLETION_QUEUE_MASK (BLOCK_COMPLETION_QUEUE_SIZE - 1u)

/*
 * The physical service completes one transfer at a time over SPI. Completing
 * after a short virtual delay keeps the guest on the interrupt path it will
 * use on hardware instead of letting a store to BLOCK_REQ_SUBMIT finish the
 * transfer before the next instruction retires.
 */
#define BLOCK_SERVICE_DELAY_NS   20000ull
#define DISPLAY_SERVICE_DELAY_NS 1000000ull

#define ASTRA_INPUT_QUEUE_SIZE   32u
#define ASTRA_INPUT_QUEUE_MASK   (ASTRA_INPUT_QUEUE_SIZE - 1u)
#define ASTRA_INPUT_DEVICE_KEYBOARD 1u
#define ASTRA_INPUT_DEVICE_POINTER  2u

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

typedef struct AstraBlockCompletion {
    uint32_t id;
    uint32_t status;
    uint32_t sectors;
    uint32_t detail;
    uint32_t media_generation;
    uint32_t host_generation;
} AstraBlockCompletion;

typedef struct AstraBlockRequest {
    Astra68State *machine;
    BlockAIOCB *aiocb;
    QEMUIOVector qiov;
    bool qiov_initialized;
} AstraBlockRequest;

typedef struct AstraBlockState {
    BlockBackend *blk;
    QEMUTimer *service_timer;
    AstraBlockCompletion completion[BLOCK_COMPLETION_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    /* Registers the guest programs before writing BLOCK_REQ_SUBMIT. */
    uint32_t req_id;
    uint32_t req_op;
    uint32_t req_lba_hi;
    uint32_t req_lba_lo;
    uint32_t req_sectors;
    uint32_t req_buffer;
    /* The one transfer the service is executing. */
    uint32_t active_id;
    uint32_t active_op;
    uint32_t active_sectors;
    uint32_t active_buffer;
    uint64_t active_lba;
    AstraBlockRequest *active_request;
    bool busy;
    uint32_t error;
    uint32_t host_generation;
    uint32_t media_generation;
    uint64_t media_sectors;
    uint64_t read_requests;
    uint64_t read_sectors;
    bool write_enable;
    bool state_change;
} AstraBlockState;

typedef struct AstraInputState {
    AstraInputEvent queue[ASTRA_INPUT_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint16_t keyboard_sequence;
    uint16_t pointer_sequence;
    uint32_t host_generation;
    uint32_t dropped;
    bool overflow;
    QemuInputHandlerState *handler;
} AstraInputState;

typedef struct AstraDisplayState {
    MemoryRegion mailbox_region;
    AstraDisplayMailbox *mailbox;
    QEMUTimer *service_timer;
    uint32_t request_id;
    uint32_t request_op;
    uint32_t request_source;
    uint32_t completion_id;
    uint32_t completion_status;
    uint32_t completion_generation;
    uint32_t mailbox_sequence;
    uint64_t submissions;
    uint64_t completions;
    uint64_t generation;
    uint64_t submit_cycle;
    uint64_t completion_cycle;
    uint64_t collect_cycle;
    uint64_t operation;
    uint64_t batch_submissions;
    uint64_t batch_commands;
    uint64_t fill_commands;
    uint64_t blit_commands;
    uint64_t glyph_commands;
    uint64_t cursor_x;
    uint64_t cursor_y;
    uint64_t cursor_visible;
    uint64_t cursor_updates;
    uint64_t cursor_submit_cycle;
    uint64_t cursor_completion_cycle;
    uint64_t cursor_collect_cycle;
    bool mailbox_enabled;
    bool busy;
    bool completion_valid;
} AstraDisplayState;

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
    uint32_t ram_size;
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
    AstraInputState input;
    AstraBlockState block;
    AstraDisplayState display;
    uint8_t panel_led_data;
    uint8_t panel_led_ownership;
#ifdef CONFIG_POSIX
    volatile uint32_t *host_panel;
#endif
    bool trace_timers;
};

static Astra68State *astra_input_machine;

static void astra_update_irq(Astra68State *s);
static uint64_t astra_now_cycles(Astra68State *s);
static void astra_panel_write32(Astra68State *s, hwaddr offset,
                                uint32_t value);

static uint32_t astra_display_queue(const AstraDisplayState *display)
{
    return (display->busy ? ASTRA_DISPLAY_HOST_QUEUE_BUSY : 0u) |
           (!display->busy && !display->completion_valid ?
                ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY : 0u) |
           (display->completion_valid ?
                ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID : 0u);
}

static void astra_display_count_batch(Astra68State *s, uint32_t source,
                                      uint32_t byte_size)
{
    AstraDisplayState *display = &s->display;
    const uint8_t *batch = s->sdram + (source - ASTRA_SDRAM_BASE);
    uint32_t count = ldl_be_p(batch + 12u);
    uint32_t records = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                       ASTRA_RENDER_BATCH_ARENA_OFFSET;

    if (ldl_be_p(batch) != ASTRA_RENDER_BATCH_MAGIC ||
        ldl_be_p(batch + 4u) != ASTRA_RENDER_BATCH_VERSION_1_0 ||
        ldl_be_p(batch + 8u) != byte_size ||
        ldl_be_p(batch + 16u) != ASTRA_RENDER_BATCH_SUBMISSION_OFFSET ||
        records > byte_size || count > ASTRA_RENDER_RING_ENTRIES ||
        count > (byte_size - records) / ASTRA_RENDER_COMMAND_BYTES)
        return;
    ++display->batch_submissions;
    display->batch_commands += count;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t opcode = ldl_be_p(batch + records +
                                   index * ASTRA_RENDER_COMMAND_BYTES + 4u) >>
                          16;

        if (opcode == ASTRA_RENDER_OP_FILL)
            ++display->fill_commands;
        else if (opcode == ASTRA_RENDER_OP_BLIT)
            ++display->blit_commands;
        else if (opcode == ASTRA_RENDER_OP_GLYPH_RUN)
            ++display->glyph_commands;
    }
}

static void astra_display_complete(Astra68State *s, uint32_t status,
                                   uint32_t generation)
{
    AstraDisplayState *display = &s->display;

    if (!display->busy)
        return;
    display->busy = false;
    display->completion_valid = true;
    display->completion_id = display->request_id;
    display->completion_status = status;
    display->completion_generation = generation;
    display->generation = generation;
    display->completion_cycle = astra_now_cycles(s);
    if (display->operation == ASTRA_DISPLAY_CURSOR_UPDATE)
        display->cursor_completion_cycle = display->completion_cycle;
    ++display->completions;
    s->astraea.irq_status |= ASTRAEA_IRQ_DRAW_DONE;
    astra_update_irq(s);
}

static void astra_display_service(void *opaque)
{
    Astra68State *s = opaque;
    AstraDisplayState *display = &s->display;

    if (!display->busy)
        return;
    if (!display->mailbox_enabled) {
        astra_display_complete(s, ASTRA_DISPLAY_COMPLETION_OK,
                               (uint32_t)display->generation + 1u);
        return;
    }
    if (qatomic_read(&display->mailbox->completion_sequence) !=
            display->mailbox_sequence) {
        timer_mod_ns(display->service_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT) +
                         DISPLAY_SERVICE_DELAY_NS);
        return;
    }
    smp_rmb();
    astra_display_complete(
        s,
        qatomic_read(&display->mailbox->completion_id) ==
                display->request_id ?
            qatomic_read(&display->mailbox->completion_status) :
            ASTRA_DISPLAY_COMPLETION_BAD_REQUEST,
        qatomic_read(&display->mailbox->completion_generation));
}

static void astra_display_submit(Astra68State *s)
{
    AstraDisplayState *display = &s->display;
    uint32_t operation = display->request_op &
                         ASTRA_DISPLAY_HOST_OPERATION_MASK;
    uint32_t byte_size = display->request_op >>
                         ASTRA_DISPLAY_HOST_BYTE_SIZE_SHIFT;
    uint64_t frame_end = (uint64_t)display->request_source +
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ?
             ASTRA_DISPLAY_MAILBOX_FRAME_BYTES : byte_size);

    if (display->busy || display->completion_valid ||
        display->request_id == 0u ||
        (operation != ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         operation != ASTRA_DISPLAY_FRAME_PRESENT_RGB565 &&
         operation != ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
         operation != ASTRA_DISPLAY_CURSOR_UPDATE) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         (display->request_source & 0xffff0000u) != 0u) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
         (byte_size < ASTRA_RENDER_BATCH_MIN_BYTES ||
          byte_size > ASTRA_RENDER_BATCH_MAX_BYTES)) ||
        (operation == ASTRA_DISPLAY_CURSOR_UPDATE &&
         ((byte_size &
           ~(ASTRA_DISPLAY_CURSOR_VISIBLE |
             ASTRA_DISPLAY_CURSOR_DEFER_COMMIT)) != 0u ||
          (display->request_source & ASTRA_DISPLAY_HOST_CURSOR_X_MASK) >=
              ASTRA_DISPLAY_WIDTH ||
          ((display->request_source & ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) >>
               ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT) >= ASTRA_DISPLAY_HEIGHT)) ||
        (operation != ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         operation != ASTRA_DISPLAY_CURSOR_UPDATE &&
         (display->request_source < ASTRA_SDRAM_BASE ||
          frame_end > (uint64_t)ASTRA_SDRAM_BASE + s->ram_size)))
        return;
    display->busy = true;
    display->operation = operation;
    if (operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH)
        astra_display_count_batch(s, display->request_source, byte_size);
    if (operation == ASTRA_DISPLAY_CURSOR_UPDATE) {
        display->cursor_x = display->request_source &
                            ASTRA_DISPLAY_HOST_CURSOR_X_MASK;
        display->cursor_y = (display->request_source &
                             ASTRA_DISPLAY_HOST_CURSOR_Y_MASK) >>
                            ASTRA_DISPLAY_HOST_CURSOR_Y_SHIFT;
        display->cursor_visible =
            (display->request_source & ASTRA_DISPLAY_HOST_CURSOR_VISIBLE) != 0u;
        ++display->cursor_updates;
    }
    display->submit_cycle = astra_now_cycles(s);
    if (operation == ASTRA_DISPLAY_CURSOR_UPDATE)
        display->cursor_submit_cycle = display->submit_cycle;
    ++display->submissions;
    if (display->mailbox_enabled) {
        if (++display->mailbox_sequence == 0u)
            ++display->mailbox_sequence;
        qatomic_set(&display->mailbox->magic, ASTRA_DISPLAY_MAILBOX_MAGIC);
        qatomic_set(&display->mailbox->version,
#ifdef CONFIG_LINUX
                    ASTRA_DISPLAY_MAILBOX_VERSION_1_4);
#else
                    ASTRA_DISPLAY_MAILBOX_VERSION_1_3);
#endif
        qatomic_set(&display->mailbox->request_id, display->request_id);
        qatomic_set(&display->mailbox->operation, operation);
        qatomic_set(&display->mailbox->color_rgb565,
                    display->request_source);
        qatomic_set(&display->mailbox->frame_pitch,
                    operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ?
                        ASTRA_DISPLAY_WIDTH * 2u : 0u);
        qatomic_set(&display->mailbox->frame_bytes,
                    operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ?
                        ASTRA_DISPLAY_MAILBOX_FRAME_BYTES :
                    operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH ?
                        byte_size :
                    operation == ASTRA_DISPLAY_CURSOR_UPDATE ? byte_size : 0u);
        if (operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ||
            operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH) {
            memcpy((uint8_t *)display->mailbox +
                       ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
                   s->sdram + (display->request_source - ASTRA_SDRAM_BASE),
                   operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ?
                       ASTRA_DISPLAY_MAILBOX_FRAME_BYTES : byte_size);
        }
        smp_wmb();
        qatomic_set(&display->mailbox->request_sequence,
                    display->mailbox_sequence);
#ifdef CONFIG_LINUX
        qemu_futex_wake((void *)&display->mailbox->request_sequence, 1);
#endif
    }
    timer_mod_ns(display->service_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT) +
                     DISPLAY_SERVICE_DELAY_NS);
}

static void astra_display_reset(Astra68State *s)
{
    AstraDisplayState *display = &s->display;

    if (display->service_timer)
        timer_del(display->service_timer);
    display->busy = false;
    display->completion_valid = false;
    display->request_id = 0u;
    display->request_op = 0u;
    display->request_source = 0u;
    display->completion_id = 0u;
    display->completion_status = 0u;
    display->completion_generation = 0u;
    display->operation = 0u;
    display->cursor_x = 0u;
    display->cursor_y = 0u;
    display->cursor_visible = 0u;
    s->astraea.irq_status &= ~ASTRAEA_IRQ_DRAW_DONE;
    astra_update_irq(s);
}

static void astra_display_panic_text(Astra68State *s)
{
    AstraDisplayState *display = &s->display;

    if (!display->mailbox_enabled)
        return;
    if (++display->mailbox_sequence == 0u)
        ++display->mailbox_sequence;
    qatomic_set(&display->mailbox->magic, ASTRA_DISPLAY_MAILBOX_MAGIC);
    qatomic_set(&display->mailbox->version,
                ASTRA_DISPLAY_MAILBOX_VERSION_1_4);
    qatomic_set(&display->mailbox->request_id, UINT32_MAX);
    qatomic_set(&display->mailbox->operation, ASTRA_DISPLAY_PANIC_TEXT);
    qatomic_set(&display->mailbox->color_rgb565, 0u);
    qatomic_set(&display->mailbox->frame_pitch, 0u);
    qatomic_set(&display->mailbox->frame_bytes, 0u);
    smp_wmb();
    qatomic_set(&display->mailbox->request_sequence,
                display->mailbox_sequence);
#ifdef CONFIG_LINUX
    qemu_futex_wake((void *)&display->mailbox->request_sequence, 1);
#endif
}

static uint32_t astra_input_level(const AstraInputState *input)
{
    return (input->tail - input->head) & ASTRA_INPUT_QUEUE_MASK;
}

static uint32_t astra_input_status(const AstraInputState *input)
{
    uint32_t level = astra_input_level(input);
    return level | (level != 0 ? ASTRA_INPUT_STATUS_VALID : 0) |
           (input->overflow ? ASTRA_INPUT_STATUS_OVERFLOW : 0);
}

static bool astra_input_push(Astra68State *s, uint8_t event_class,
                             uint8_t kind, uint16_t flags, uint32_t value,
                             uint16_t device, uint16_t *sequence)
{
    AstraInputState *input = &s->input;
    uint8_t next = (input->tail + 1u) & ASTRA_INPUT_QUEUE_MASK;
    AstraInputEvent *event;

    if (next == input->head) {
        input->overflow = true;
        input->dropped++;
        astra_update_irq(s);
        return false;
    }
    ++*sequence;
    event = &input->queue[input->tail];
    event->header = ((uint32_t)event_class << 24) |
                    ((uint32_t)kind << 16) | flags;
    event->value = value;
    event->timestamp_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
    event->device_sequence = (uint32_t)device << 16 | *sequence;
    event->host_generation = input->host_generation;
    input->tail = next;
    return true;
}

static uint16_t astra_usb_usage_for_qcode(QKeyCode qcode)
{
    guint usage;

    for (usage = 0; usage < qemu_input_map_usb_to_qcode_len; ++usage) {
        if (qemu_input_map_usb_to_qcode[usage] == qcode) {
            return usage;
        }
    }
    return 0;
}

static uint32_t astra_pointer_button(InputButton button)
{
    switch (button) {
    case INPUT_BUTTON_LEFT: return 1;
    case INPUT_BUTTON_MIDDLE: return 2;
    case INPUT_BUTTON_RIGHT: return 3;
    case INPUT_BUTTON_WHEEL_UP: return 4;
    case INPUT_BUTTON_WHEEL_DOWN: return 5;
    case INPUT_BUTTON_SIDE: return 6;
    case INPUT_BUTTON_EXTRA: return 7;
    case INPUT_BUTTON_WHEEL_LEFT: return 8;
    case INPUT_BUTTON_WHEEL_RIGHT: return 9;
    default: return 0;
    }
}

static void astra_input_event(DeviceState *device, QemuConsole *console,
                              InputEvent *event)
{
    Astra68State *s = astra_input_machine;
    uint16_t flags;
    uint16_t usage;
    uint32_t button;
    InputMoveEvent *move;

    (void)device;
    (void)console;
    if (s == NULL) {
        return;
    }
    switch (event->type) {
    case INPUT_EVENT_KIND_KEY:
        usage = astra_usb_usage_for_qcode(
            qemu_input_key_value_to_qcode(event->u.key.data->key));
        if (usage != 0) {
            flags = event->u.key.data->down ? ASTRA_INPUT_FLAG_DOWN : 0;
            astra_input_push(s, ASTRA_INPUT_CLASS_KEYBOARD,
                             ASTRA_INPUT_KEY_PHYSICAL, flags, usage,
                             ASTRA_INPUT_DEVICE_KEYBOARD,
                             &s->input.keyboard_sequence);
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        button = astra_pointer_button(event->u.btn.data->button);
        if (button != 0) {
            flags = event->u.btn.data->down ? ASTRA_INPUT_FLAG_DOWN : 0;
            astra_input_push(s, ASTRA_INPUT_CLASS_POINTER,
                             ASTRA_INPUT_POINTER_BUTTON, flags, button,
                             ASTRA_INPUT_DEVICE_POINTER,
                             &s->input.pointer_sequence);
        }
        break;
    case INPUT_EVENT_KIND_REL:
    case INPUT_EVENT_KIND_ABS:
        move = event->type == INPUT_EVENT_KIND_REL ?
               event->u.rel.data : event->u.abs.data;
        flags = move->axis == INPUT_AXIS_Y ? ASTRA_INPUT_FLAG_AXIS_Y : 0;
        astra_input_push(s, ASTRA_INPUT_CLASS_POINTER,
                         event->type == INPUT_EVENT_KIND_REL ?
                         ASTRA_INPUT_POINTER_RELATIVE :
                         ASTRA_INPUT_POINTER_ABSOLUTE,
                         flags, (uint32_t)move->value,
                         ASTRA_INPUT_DEVICE_POINTER,
                         &s->input.pointer_sequence);
        break;
    case INPUT_EVENT_KIND_MTT:
    case INPUT_EVENT_KIND__MAX:
        break;
    }
}

static void astra_input_sync(DeviceState *device)
{
    (void)device;
    if (astra_input_machine != NULL)
        astra_update_irq(astra_input_machine);
}

static const QemuInputHandler astra_input_handler = {
    .name = "Astra 68 keyboard and pointer",
    .mask = INPUT_EVENT_MASK_KEY | INPUT_EVENT_MASK_BTN |
            INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_ABS,
    .event = astra_input_event,
    .sync = astra_input_sync,
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

static bool astra_block_present(const Astra68State *s)
{
    return s->block.blk != NULL;
}

static uint32_t astra_block_completion_level(const AstraBlockState *block)
{
    return (block->tail - block->head) & BLOCK_COMPLETION_QUEUE_MASK;
}

static uint32_t astra_block_state_flags(const Astra68State *s)
{
    uint32_t flags;

    if (!astra_block_present(s)) {
        return 0;
    }
    flags = BLOCK_STATE_LINK_UP;
    if (s->block.media_sectors != 0) {
        flags |= BLOCK_STATE_MEDIA_PRESENT;
    }
    if (s->block.write_enable) {
        flags |= BLOCK_STATE_WRITE_ENABLE;
    }
    return flags;
}

static uint32_t astra_block_queue(const Astra68State *s)
{
    const AstraBlockState *block = &s->block;
    uint32_t level = astra_block_completion_level(block);
    uint32_t queue = level << BLOCK_QUEUE_COMPLETION_SHIFT;

    if (level != 0) {
        queue |= BLOCK_QUEUE_COMPLETION_VALID;
    }
    if (block->busy) {
        queue |= 1u;
    } else if (astra_block_present(s) &&
               level < BLOCK_COMPLETION_QUEUE_MASK) {
        /*
         * One transfer is active at a time, and a request is only accepted
         * while the completion queue can still take its result.
         */
        queue |= BLOCK_QUEUE_REQUEST_READY;
    }
    return queue;
}

static void astra_block_push_completion(Astra68State *s, uint32_t status,
                                        uint32_t sectors, uint32_t detail)
{
    AstraBlockState *block = &s->block;
    AstraBlockCompletion *completion = &block->completion[block->tail];

    completion->id = block->active_id;
    completion->status = status;
    completion->sectors = sectors;
    completion->detail = detail;
    completion->media_generation = block->media_generation;
    completion->host_generation = block->host_generation;
    block->tail = (block->tail + 1u) & BLOCK_COMPLETION_QUEUE_MASK;
}

static void astra_block_complete(void *opaque, int rc)
{
    AstraBlockRequest *request = opaque;
    Astra68State *s = request->machine;
    AstraBlockState *block = &s->block;
    uint32_t sectors = block->active_sectors;

    if (request->qiov_initialized) {
        qemu_iovec_destroy(&request->qiov);
    }
    if (block->active_request != request) {
        g_free(request);
        return;
    }
    block->active_request = NULL;
    if (block->active_op == BLOCK_OP_FLUSH) {
        sectors = 0;
    }
    if (rc < 0) {
        astra_block_push_completion(s, BLOCK_COMPLETION_IO_ERROR, 0,
                                    (uint32_t)-rc);
    } else {
        astra_block_push_completion(s, BLOCK_COMPLETION_OK, sectors, 0);
    }
    block->busy = false;
    astra_update_irq(s);
    g_free(request);
}

static void astra_block_service(void *opaque)
{
    Astra68State *s = opaque;
    AstraBlockState *block = &s->block;
    AstraBlockRequest *request;
    uint8_t *buffer;
    size_t bytes;

    if (!block->busy || block->active_request != NULL) {
        return;
    }
    request = g_new0(AstraBlockRequest, 1);
    request->machine = s;
    block->active_request = request;
    bytes = (size_t)block->active_sectors * BLOCK_SECTOR_SIZE;
    buffer = s->sdram + (block->active_buffer - ASTRA_SDRAM_BASE);

    switch (block->active_op) {
    case BLOCK_OP_READ:
        ++block->read_requests;
        block->read_sectors += block->active_sectors;
        qemu_iovec_init_buf(&request->qiov, buffer, bytes);
        request->qiov_initialized = true;
        request->aiocb = blk_aio_preadv(
            block->blk, (int64_t)block->active_lba * BLOCK_SECTOR_SIZE,
            &request->qiov, 0, astra_block_complete, request);
        break;
    case BLOCK_OP_WRITE:
        qemu_iovec_init_buf(&request->qiov, buffer, bytes);
        request->qiov_initialized = true;
        request->aiocb = blk_aio_pwritev(
            block->blk, (int64_t)block->active_lba * BLOCK_SECTOR_SIZE,
            &request->qiov, 0, astra_block_complete, request);
        break;
    default:
        request->aiocb = blk_aio_flush(block->blk, astra_block_complete,
                                       request);
        break;
    }
}

static uint32_t astra_block_validate(Astra68State *s)
{
    const AstraBlockState *block = &s->block;
    uint32_t operation = block->req_op & 0xffu;
    uint32_t flags = (block->req_op >> 8) & 0xffu;
    uint32_t sectors = block->req_sectors & 0xffffu;
    uint64_t lba = ((uint64_t)block->req_lba_hi << 32) | block->req_lba_lo;
    uint64_t bytes;
    uint32_t error = 0;

    if (!astra_block_present(s) || block->media_sectors == 0) {
        return BLOCK_ERROR_NO_MEDIA;
    }
    if (block->req_id == 0) {
        error |= BLOCK_ERROR_BAD_ID;
    }
    if (flags != 0) {
        error |= BLOCK_ERROR_BAD_FLAGS;
    }
    if (block->busy) {
        error |= BLOCK_ERROR_QUEUE_FULL;
    }

    if (operation == BLOCK_OP_FLUSH) {
        if (sectors != 0) {
            error |= BLOCK_ERROR_BAD_COUNT;
        }
        return error;
    }
    if (operation != BLOCK_OP_READ && operation != BLOCK_OP_WRITE) {
        return error | BLOCK_ERROR_BAD_OP;
    }

    if (sectors == 0 || sectors > BLOCK_MAX_SECTORS) {
        error |= BLOCK_ERROR_BAD_COUNT;
        return error;
    }
    if (operation == BLOCK_OP_WRITE && !block->write_enable) {
        error |= BLOCK_ERROR_WRITE_PROTECT;
    }
    if (lba >= block->media_sectors ||
        block->media_sectors - lba < sectors) {
        error |= BLOCK_ERROR_LBA_RANGE;
    }

    bytes = (uint64_t)sectors * BLOCK_SECTOR_SIZE;
    if ((block->req_buffer & 3u) != 0 ||
        block->req_buffer < ASTRA_SDRAM_BASE ||
        block->req_buffer - ASTRA_SDRAM_BASE > s->ram_size ||
        s->ram_size - (block->req_buffer - ASTRA_SDRAM_BASE) < bytes) {
        error |= BLOCK_ERROR_BAD_BUFFER;
    }
    return error;
}

static void astra_block_submit(Astra68State *s)
{
    AstraBlockState *block = &s->block;
    uint32_t error = astra_block_validate(s);

    if (error != 0) {
        block->error |= error;
        return;
    }

    block->active_id = block->req_id;
    block->active_op = block->req_op & 0xffu;
    block->active_sectors = block->req_sectors & 0xffffu;
    block->active_buffer = block->req_buffer;
    block->active_lba = ((uint64_t)block->req_lba_hi << 32) |
                        block->req_lba_lo;
    block->busy = true;
    astra_panel_write32(s, ASTRA_PANEL_ACTIVITY, 1u);
    timer_mod_ns(block->service_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                 BLOCK_SERVICE_DELAY_NS);
}

static void astra_block_pop_completion(Astra68State *s)
{
    AstraBlockState *block = &s->block;

    if (astra_block_completion_level(block) == 0) {
        return;
    }
    block->head = (block->head + 1u) & BLOCK_COMPLETION_QUEUE_MASK;
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
    if (astra_input_level(&s->input) != 0) {
        pending |= 1u << IRQ_SOURCE_INPUT;
    }
    if (astra_block_present(s) &&
        (astra_block_completion_level(&s->block) != 0 ||
         s->block.state_change)) {
        pending |= 1u << IRQ_SOURCE_STORAGE;
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

    for (address = 0; address < s->ram_size; address++) {
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
    case 0x010:
        return SYS_STATUS_BASE |
               (astra_block_present(s) ? SYS_STATUS_ASTRA_HOST : 0);
    case 0x018: return s->scratch;
    case 0x01c: return 0x00068030;
    case 0x020: return 0x54474d32;
    case 0x024: return 0x0000001d;
    case 0x028: return ASTRA_CPU_HZ;
    case 0x02c: return ASTRA_SDRAM_BASE;
    case 0x030: return s->ram_size;
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
    case 0x0d8: return s->ram_size - 4;
    case 0x0ec:
        cycles = astra_now_cycles(s);
        return cycles;
    case 0x0f0:
        cycles = astra_now_cycles(s);
        return cycles >> 32;
    case 0x12c:
        return qemu_clock_get_ns(QEMU_CLOCK_REALTIME) / 1000u;
    case 0x150:
        return astra_block_present(s) ? BLOCK_ID_MAGIC : 0;
    case 0x154:
        return astra_block_present(s) ? BLOCK_VERSION_1_0 : 0;
    case 0x158:
        if (!astra_block_present(s)) {
            return 0;
        }
        return BLOCK_CAP_READ | BLOCK_CAP_FLUSH |
               (s->block.write_enable ? BLOCK_CAP_WRITE : 0);
    case 0x15c: return astra_block_state_flags(s);
    case 0x160: return s->block.media_generation;
    case 0x164: return (uint32_t)(s->block.media_sectors >> 32);
    case 0x168: return (uint32_t)s->block.media_sectors;
    case 0x16c: return astra_block_queue(s);
    case 0x170: return s->block.req_id;
    case 0x174: return s->block.req_op;
    case 0x178: return s->block.req_lba_hi;
    case 0x17c: return s->block.req_lba_lo;
    case 0x180: return s->block.req_sectors;
    case 0x184: return s->block.req_buffer;
    case 0x18c:
        return astra_block_completion_level(&s->block) != 0 ?
               s->block.completion[s->block.head].id : 0;
    case 0x190:
        return astra_block_completion_level(&s->block) != 0 ?
               (s->block.completion[s->block.head].status << 16) |
               (s->block.completion[s->block.head].sectors & 0xffffu) : 0;
    case 0x194:
        return astra_block_completion_level(&s->block) != 0 ?
               s->block.completion[s->block.head].detail : 0;
    case 0x198:
        return astra_block_completion_level(&s->block) != 0 ?
               s->block.completion[s->block.head].media_generation : 0;
    case 0x19c:
        return astra_block_completion_level(&s->block) != 0 ?
               s->block.completion[s->block.head].host_generation : 0;
    case 0x1a4: return s->block.error;
    case 0x1a8: return s->block.host_generation;
    case 0x1ac: return s->block.state_change ? BLOCK_STATE_ACK_BIT : 0;
    case 0x1b0:
        return astra_block_present(s) ? BLOCK_MAX_SECTORS : 0;
    case 0x1d4: return ASTRA_DISPLAY_HOST_ID_MAGIC;
    case 0x1d8: return ASTRA_DISPLAY_HOST_VERSION_1_0;
    case 0x1dc:
        return ASTRA_DISPLAY_HOST_CAP_SOLID_FRAME |
               ASTRA_DISPLAY_HOST_CAP_FENCED_PRESENT |
               ASTRA_DISPLAY_HOST_CAP_RENDER_BATCH |
               ASTRA_DISPLAY_HOST_CAP_HARDWARE_CURSOR;
    case 0x1e0: return astra_display_queue(&s->display);
    case 0x1e4: return s->display.request_id;
    case 0x1e8: return s->display.request_op;
    case 0x1ec: return s->display.request_source;
    case 0x1f4:
        return s->display.completion_valid ?
               s->display.completion_id : 0u;
    case 0x1f8:
        return s->display.completion_valid ?
               s->display.completion_status : 0u;
    case 0x1fc:
        return s->display.completion_valid ?
               s->display.completion_generation : 0u;
    case 0x300: return astra_pending_raw(s);
    case 0x304: return s->irq_enable;
    case 0x308: return s->irq_soft;
    case 0x310: return astra_irq_current(s);
    case 0x504: return 1;
    case 0x700: return ASTRA_DEVICE_CLASS_INPUT;
    case 0x704: return ASTRA_INPUT_VERSION_1_1;
    case 0x708:
        return ASTRA_INPUT_CAP_KEYBOARD | ASTRA_INPUT_CAP_POINTER;
    case 0x70c: return astra_input_status(&s->input);
    case 0x710:
        return astra_input_level(&s->input) != 0 ?
               s->input.queue[s->input.head].header : 0;
    case 0x714:
        return astra_input_level(&s->input) != 0 ?
               s->input.queue[s->input.head].value : 0;
    case 0x718:
        return astra_input_level(&s->input) != 0 ?
               s->input.queue[s->input.head].timestamp_ms : 0;
    case 0x71c:
        return astra_input_level(&s->input) != 0 ?
               s->input.queue[s->input.head].device_sequence : 0;
    case 0x720:
        return astra_input_level(&s->input) != 0 ?
               s->input.queue[s->input.head].host_generation :
               s->input.host_generation;
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
        astra_display_panic_text(s);
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
    case 0x170: s->block.req_id = value; break;
    case 0x174: s->block.req_op = value; break;
    case 0x178: s->block.req_lba_hi = value; break;
    case 0x17c: s->block.req_lba_lo = value; break;
    case 0x180: s->block.req_sectors = value; break;
    case 0x184: s->block.req_buffer = value; break;
    case 0x188:
        if (value & BLOCK_SUBMIT) {
            astra_block_submit(s);
        }
        break;
    case 0x1a0:
        if (value & BLOCK_CPL_POP_BIT) {
            astra_block_pop_completion(s);
            astra_update_irq(s);
        }
        break;
    case 0x1a4:
        s->block.error &= ~value;
        break;
    case 0x1ac:
        if (value & BLOCK_STATE_ACK_BIT) {
            s->block.state_change = false;
            astra_update_irq(s);
        }
        break;
    case 0x1e4: s->display.request_id = value; break;
    case 0x1e8: s->display.request_op = value; break;
    case 0x1ec: s->display.request_source = value; break;
    case 0x1f0:
        if (value & ASTRA_DISPLAY_HOST_RESET) {
            astra_display_reset(s);
        } else {
            if (value & ASTRA_DISPLAY_HOST_POP) {
                s->display.completion_valid = false;
                s->display.collect_cycle = astra_now_cycles(s);
                if (s->display.operation == ASTRA_DISPLAY_CURSOR_UPDATE)
                    s->display.cursor_collect_cycle =
                        s->display.collect_cycle;
            }
            if (value & ASTRA_DISPLAY_HOST_SUBMIT)
                astra_display_submit(s);
            astra_update_irq(s);
        }
        break;
    case 0x500:
        fputc(value & 0xff, stdout);
        fflush(stdout);
        break;
    case 0x724:
        if ((value & ASTRA_INPUT_POP_EVENT) != 0 &&
            astra_input_level(&s->input) != 0) {
            s->input.head = (s->input.head + 1u) & ASTRA_INPUT_QUEUE_MASK;
        }
        if ((value & ASTRA_INPUT_ACK_OVERFLOW) != 0) {
            s->input.overflow = false;
        }
        astra_update_irq(s);
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
#ifdef CONFIG_POSIX
    if (s->host_panel != NULL && offset < ASTRA_PANEL_SIZE) {
        return le32_to_cpu(s->host_panel[offset / sizeof(uint32_t)]);
    }
#endif
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
#ifdef CONFIG_POSIX
    if (s->host_panel != NULL && offset < ASTRA_PANEL_SIZE) {
        s->host_panel[offset / sizeof(uint32_t)] = cpu_to_le32(value);
        return;
    }
#endif
    switch (offset) {
    case 0x18: s->panel_led_data = value; break;
    case 0x1c: s->panel_led_ownership = value; break;
    case 0x20: s->panel_led_data |= value; break;
    case 0x24: s->panel_led_data &= ~value; break;
    case 0x28: s->panel_led_data ^= value; break;
    }
}

static void astra_panel_host_init(Astra68State *s)
{
    const char *path = g_getenv("ASTRA_FRONT_PANEL_MMIO_PATH");

    if (path == NULL || path[0] == '\0') {
        return;
    }
#ifdef CONFIG_POSIX
    const char *offset_text = g_getenv("ASTRA_FRONT_PANEL_MMIO_OFFSET");
    uint64_t offset = 0;
    int fd;
    void *mapping;

    if (offset_text != NULL && offset_text[0] != '\0') {
        char *end = NULL;

        errno = 0;
        offset = g_ascii_strtoull(offset_text, &end, 0);
        if (errno != 0 || end == offset_text || *end != '\0') {
            error_report("invalid ASTRA_FRONT_PANEL_MMIO_OFFSET '%s'",
                         offset_text);
            exit(EXIT_FAILURE);
        }
    }
    if ((offset & (qemu_real_host_page_size() - 1u)) != 0) {
        error_report("front-panel MMIO offset 0x%" PRIx64
                     " is not host-page aligned", offset);
        exit(EXIT_FAILURE);
    }
    fd = open(path, O_RDWR | O_SYNC);
    if (fd < 0) {
        error_report("cannot open front-panel MMIO '%s': %s",
                     path, strerror(errno));
        exit(EXIT_FAILURE);
    }
    mapping = mmap(NULL, ASTRA_PANEL_SIZE, PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, (off_t)offset);
    close(fd);
    if (mapping == MAP_FAILED) {
        error_report("cannot map front-panel MMIO '%s' at 0x%" PRIx64
                     ": %s", path, offset, strerror(errno));
        exit(EXIT_FAILURE);
    }
    s->host_panel = mapping;
#else
    error_report("ASTRA_FRONT_PANEL_MMIO_PATH requires a POSIX host");
    exit(EXIT_FAILURE);
#endif
}

static bool astra_sdram_span(Astra68State *s, uint32_t offset, uint32_t bytes)
{
    return offset <= s->ram_size && bytes <= s->ram_size - offset;
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
        if (!astra_sdram_span(s, dst, row_bytes) ||
            ((a->op & 0xf) == 0 && !astra_sdram_span(s, src, row_bytes))) {
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

/* Every guest-visible chip, including future audio/math devices, resets here. */
static void astra_machine_reset(void *opaque)
{
    Astra68State *s = opaque;
    AstraBlockRequest *block_request;
    int i;

    cpu_reset(CPU(s->cpu));
    s->cpu->env.aregs[7] = s->initial_sp;
    s->cpu->env.pc = s->initial_pc;
    s->reset_clock_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->scratch = 0u;
    s->irq_enable = 0u;
    s->irq_soft = 0u;
    memset(s->irq_config, 0, sizeof(s->irq_config));
    for (i = 0; i < 2; ++i) {
        timer_del(s->timers[i].qemu_timer);
        s->timers[i].load = 0u;
        s->timers[i].control = 0u;
        s->timers[i].status = 0u;
    }

    memset(&s->astraea, 0, sizeof(s->astraea));
    timer_del(s->vega.vblank_timer);
    s->vega.irq_enable = 0u;
    s->vega.irq_status = 0u;
    s->vega.frame_counter = 0u;
    memset(s->vega.regs, 0, sizeof(s->vega.regs));
    timer_mod_ns(s->vega.vblank_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     NANOSECONDS_PER_SECOND / 60);

    s->panel_led_data = 0u;
    s->panel_led_ownership = 0u;
    astra_panel_write32(s, 0x18, 0u);
    astra_panel_write32(s, 0x1c, 0u);
    astra_panel_write32(s, ASTRA_PANEL_ACTIVITY, 0u);

    s->input.head = 0;
    s->input.tail = 0;
    s->input.keyboard_sequence = 0;
    s->input.pointer_sequence = 0;
    s->input.overflow = false;
    s->input.dropped = 0;
    ++s->input.host_generation;

    if (s->block.service_timer) {
        timer_del(s->block.service_timer);
    }
    block_request = s->block.active_request;
    s->block.active_request = NULL;
    if (block_request != NULL && block_request->aiocb != NULL) {
        blk_aio_cancel_async(block_request->aiocb);
    }
    s->block.head = 0;
    s->block.tail = 0;
    s->block.busy = false;
    s->block.error = 0;
    s->block.req_id = 0;
    s->block.req_op = 0;
    s->block.req_lba_hi = 0;
    s->block.req_lba_lo = 0;
    s->block.req_sectors = 0;
    s->block.req_buffer = 0;
    s->block.active_id = 0;
    s->block.active_op = 0;
    s->block.active_sectors = 0;
    s->block.active_buffer = 0;
    s->block.active_lba = 0;
    memset(s->block.completion, 0, sizeof(s->block.completion));
    /*
     * A reset is a fresh host service generation. The guest must resynchronise
     * before trusting any state it cached, so raise the pending state change
     * the storage interrupt reports.
     */
    ++s->block.host_generation;
    s->block.state_change = astra_block_present(s);
    astra_display_reset(s);
    astra_update_irq(s);
}

static void astra68_init(MachineState *machine)
{
    Astra68State *s = g_new0(Astra68State, 1);
    MemoryRegion *sysmem = get_system_memory();
    const char *firmware = machine->firmware;
    DriveInfo *dinfo;
    char *filename;
    char *contents;
    gsize firmware_size;
    GError *gerror = NULL;
    const char *display_mailbox_path;
    const char *text_plane_path;
    int i;

    s->trace_timers = g_getenv("ASTRA_QEMU_TIMER_TRACE") != NULL;
    s->input.host_generation = 0;
    astra_input_machine = s;
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-block-read-requests",
                                   &s->block.read_requests,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-block-read-sectors",
                                   &s->block.read_sectors,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-submissions",
                                   &s->display.submissions,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-completions",
                                   &s->display.completions,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-generation",
                                   &s->display.generation,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-submit-cycle",
                                   &s->display.submit_cycle,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-completion-cycle",
                                   &s->display.completion_cycle,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-collect-cycle",
                                   &s->display.collect_cycle,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-operation",
                                   &s->display.operation,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-render-batches",
                                   &s->display.batch_submissions,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-render-commands",
                                   &s->display.batch_commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-fill-commands",
                                   &s->display.fill_commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-blit-commands",
                                   &s->display.blit_commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-glyph-commands",
                                   &s->display.glyph_commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-x",
                                   &s->display.cursor_x,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-y",
                                   &s->display.cursor_y,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-visible",
                                   &s->display.cursor_visible,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-updates",
                                   &s->display.cursor_updates,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-submit-cycle",
                                   &s->display.cursor_submit_cycle,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-completion-cycle",
                                   &s->display.cursor_completion_cycle,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-collect-cycle",
                                   &s->display.cursor_collect_cycle,
                                   OBJ_PROP_FLAG_READ);

    /*
     * RAM is whatever the board has, not one of two blessed profiles.
     *
     * This used to accept only 32 MiB or 128 MiB, which matched the two boards
     * that existed when it was written. Nothing in this model depends on
     * either number -- every other use reads s->ram_size -- so the check was
     * describing the hardware that happened to exist rather than anything the
     * emulator requires. The guest kernel sizes its frame tables from the boot
     * info now, so modelling a board with a different amount of memory should
     * be a command line rather than a patch.
     *
     * What is left are the constraints that are real: the firmware's POST
     * wants at least a megabyte, the guest's page tables want page alignment,
     * and SDRAM must not run into the ROM aperture above it.
     */
    if (machine->ram_size < MiB ||
        (machine->ram_size & (4 * KiB - 1)) != 0 ||
        (uint64_t)ASTRA_SDRAM_BASE + machine->ram_size > ASTRA_ROM_BASE) {
        error_report("Astra68 RAM must be at least 1 MiB, a multiple of 4 KiB,"
                     " and must fit below the ROM aperture at 0x%08x"
                     " (reference profiles: %u MiB physical, %u MiB hosted)",
                     ASTRA_ROM_BASE,
                     (unsigned)(ASTRA_SDRAM_PHYSICAL_SIZE / MiB),
                     (unsigned)(ASTRA_SDRAM_HOSTED_SIZE / MiB));
        exit(EXIT_FAILURE);
    }
    if (!firmware) {
        error_report("Astra68 requires -bios <astra_boot.bin>");
        exit(EXIT_FAILURE);
    }

    s->ram_size = machine->ram_size;
    s->cpu = M68K_CPU(cpu_create(machine->cpu_type));
    qemu_register_reset(astra_machine_reset, s);

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

    text_plane_path = g_getenv("ASTRA_TEXT_PLANE_PATH");
    if (text_plane_path != NULL && text_plane_path[0] != '\0') {
#ifdef CONFIG_POSIX
        if (!memory_region_init_ram_from_file(
                &s->text, NULL, "astra68.post-text", ASTRA_TEXT_SIZE, 0,
                RAM_SHARED, text_plane_path, 0, &error_fatal)) {
            exit(EXIT_FAILURE);
        }
#else
        error_report("ASTRA_TEXT_PLANE_PATH requires a POSIX host");
        exit(EXIT_FAILURE);
#endif
    } else {
        memory_region_init_ram(&s->text, NULL, "astra68.post-text",
                               ASTRA_TEXT_SIZE, &error_fatal);
    }
    memory_region_add_subregion(sysmem, ASTRA_TEXT_BASE, &s->text);

    display_mailbox_path = g_getenv("ASTRA_DISPLAY_MAILBOX_PATH");
    if (display_mailbox_path != NULL && display_mailbox_path[0] != '\0') {
#ifdef CONFIG_POSIX
        if (!memory_region_init_ram_from_file(
                &s->display.mailbox_region, NULL,
                "astra68.display-mailbox", ASTRA_DISPLAY_MAILBOX_BYTES, 0,
                RAM_SHARED, display_mailbox_path, 0, &error_fatal)) {
            exit(EXIT_FAILURE);
        }
        s->display.mailbox = memory_region_get_ram_ptr(
            &s->display.mailbox_region);
        s->display.mailbox_enabled = true;
#else
        error_report("ASTRA_DISPLAY_MAILBOX_PATH requires a POSIX host");
        exit(EXIT_FAILURE);
#endif
    }

    astra_panel_host_init(s);

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
    s->display.service_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL_RT,
                                             astra_display_service, s);
    timer_mod_ns(s->vega.vblank_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                 NANOSECONDS_PER_SECOND / 60);

    s->input.handler = qemu_input_handler_register(NULL,
                                                   &astra_input_handler);
    qemu_input_handler_activate(s->input.handler);

    /*
     * The AstraHost block service is present only when an image is attached
     * with -drive if=none. Without one the machine reports no block controller
     * and SYS_ASTRA_HOST stays clear, so the K1-K10 boot path is unchanged.
     * if=none is the interface QEMU allows a machine to claim without a qdev
     * device behind it.
     */
    dinfo = drive_get(IF_NONE, 0, 0);
    if (dinfo) {
        int64_t length;

        s->block.blk = blk_by_legacy_dinfo(dinfo);
        blk_set_perm(s->block.blk, BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE,
                     BLK_PERM_ALL, &error_fatal);
        length = blk_getlength(s->block.blk);
        if (length < 0) {
            error_report("Astra68 storage image is not readable");
            exit(EXIT_FAILURE);
        }
        if (length % BLOCK_SECTOR_SIZE) {
            error_report("Astra68 storage image must be a multiple of %u bytes",
                         BLOCK_SECTOR_SIZE);
            exit(EXIT_FAILURE);
        }
        s->block.media_sectors = (uint64_t)length / BLOCK_SECTOR_SIZE;
        s->block.write_enable = blk_is_writable(s->block.blk);
        s->block.media_generation = 1;
        s->block.service_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                              astra_block_service, s);
    }

    astra_machine_reset(s);
}

static void astra68_machine_init(MachineClass *mc)
{
    mc->desc = "Astra 68 reference machine";
    mc->init = astra68_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68030");
    mc->default_ram_size = ASTRA_SDRAM_PHYSICAL_SIZE;
    mc->default_ram_id = "astra68.sdram";
    mc->max_cpus = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

DEFINE_MACHINE("astra68", astra68_machine_init)
