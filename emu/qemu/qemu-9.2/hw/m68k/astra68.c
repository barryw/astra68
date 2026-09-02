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
#include <poll.h>
#ifdef CONFIG_POSIX
#include <sys/file.h>
#endif
#ifdef CONFIG_LINUX
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif
#include "qemu/bswap.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/atomic.h"
#ifdef CONFIG_LINUX
#include "qemu/futex.h"
#endif
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
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
#include "astra/block.h"
#include "astra/display.h"
#include "astra/host.h"
#include "astra/network.h"
#include "astra/status.h"
#include "astra/vfs_service.h"
#include "block/thread-pool.h"
#include "sysemu/block-backend.h"
#include "sysemu/block-backend-io.h"
#include "sysemu/blockdev.h"
#include "sysemu/reset.h"
#include "sysemu/runstate.h"
#include "ui/input.h"

#define ASTRA_BRAM_BASE          0x01ff8000u
#define ASTRA_BRAM_SIZE          (32 * KiB)
#define ASTRA_SDRAM_BASE         0x02000000u
#define ASTRA_SDRAM_HOSTED_SIZE  (128 * MiB)
#define ASTRA_ROM_BASE           0xffe00000u
#define ASTRA_ROM_SIZE           (512 * KiB)
#define ASTRA_VESTA_BASE         0xfff00000u
#define ASTRA_VESTA_SIZE         0x900u
#define ASTRA_PANEL_BASE         0xfff01000u
#define ASTRA_PANEL_SIZE         0x100u
#define ASTRA_PANEL_ACTIVITY     0x2cu
#define ASTRA_ASTRAEA_BASE       0xfff10000u
#define ASTRA_ASTRAEA_SIZE       0x8000u
#define ASTRA_VEGA_BASE          0xfff20000u
#define ASTRA_VEGA_SIZE          0x2000u
#define ASTRA_TEXT_BASE          0xfff22000u
#define ASTRA_TEXT_SIZE          0x1000u
#define ASTRA_HOST_CHANNEL_BASE ASTRA_HOST_CHANNEL_PHYSICAL_BASE
#define ASTRA_HOST_CHANNEL_SIZE ASTRA_HOST_CHANNEL_APERTURE_SIZE

#define ASTRA_CPU_HZ             12500000ull
#define ASTRA_BUILD_ID           0x18ebe2e1u
#define ASTRA_KERNEL_READY       0x4b314f4bu
#define ASTRA_KERNEL_SOAK        0x4b31534bu
#define ASTRA_KERNEL_PANIC       0x4b50414eu

#define TIMER_ENABLE             (1u << 0)
#define TIMER_PERIODIC           (1u << 1)
#define TIMER_IRQ_ENABLE         (1u << 2)
#define TIMER_EXPIRED            (1u << 0)
#define RTC_VALID                (1u << 0)
#define RTC_ZONE_VALID           (1u << 1)
#define VEGA_IRQ_VBLANK          (1u << 0)
#define ASTRAEA_IRQ_BLIT_DONE    (1u << 0)
#define ASTRAEA_IRQ_DRAW_DONE    (1u << 3)
#define IRQ_SOURCE_VEGA          8
#define IRQ_SOURCE_ASTRAEA       9
#define IRQ_SOURCE_INPUT         5
#define IRQ_SOURCE_STORAGE       4
#define IRQ_SOURCE_NETWORK       11
#define IRQ_SOURCE_HOST          12
#define IRQ_VALID                (1u << 31)

#define SYS_STATUS_BASE          0x0000000fu
#define SYS_STATUS_ASTRA_HOST    (1u << 5)
#define SYS_STATUS_USB_READY     (1u << 6)

/*
 * The USB host controller, enough of it to be brought up.
 *
 * This models the register file and the reset handshake, not the wire: no
 * transfer descriptors are walked and no device is attached, so nothing is
 * ever transferred. That is deliberate and it is still worth having, because
 * the thing that was untestable was never the data path -- it was everything
 * that happens *before* one: the firmware validating the DMA aperture the
 * controller reports, building a memory map with a device range in the middle
 * of RAM, and the kernel resetting the controller and pointing it at an HCCA.
 * All of that ran only on hardware until now, and all of it is where the
 * board-specific assumptions were hiding.
 *
 * The aperture matches the guest's reference constants in sw/include/ohci.h.
 * The active 128 MiB guest has RAM above it, which exercises split usable
 * ranges around a device aperture.
 */
#define ASTRA_OHCI_BASE          0xfff40000u
#define ASTRA_OHCI_SIZE          0x1000u
#define OHCI_ASTRA_ID_MAGIC      0x41555342u /* "AUSB" */
#define OHCI_ASTRA_VERSION_1_0   0x00010000u
#define OHCI_REVISION_1_0        0x10u
#define OHCI_DMA_POOL_BASE       0x03f00000u
#define OHCI_DMA_POOL_SIZE       0x00100000u
#define OHCI_COMMAND_HCR         (1u << 0)
#define OHCI_CONTROL_HCFS_MASK   (3u << 6)
#define OHCI_CONTROL_HCFS_SUSPEND (3u << 6)
#define OHCI_ASTRA_DMA_FAULT     (1u << 0)
#define OHCI_ASTRA_IRQ           (1u << 1)
#define OHCI_INT_SF              (1u << 2)
#define OHCI_INT_MIE             (1u << 31)
#define OHCI_CONTROL_HCFS_OPERATIONAL (2u << 6)
/* OHCI frames are a millisecond; start-of-frame is the tick that proves the
 * controller is live without a device attached. */
#define OHCI_FRAME_INTERVAL_NS   (NANOSECONDS_PER_SECOND / 1000)
#define IRQ_SOURCE_USB           7

/*
 * AstraHost runtime block service, Vesta offsets 0x150..0x1b0. The register
 * contract and 512-byte sector are defined by sw/include/vesta.h and
 * the Astra host transport ABI. The maximum transfer is reported at runtime,
 * allowing
 * this hosted backend to batch more sectors than the physical SPI service.
 */
#define BLOCK_ID_MAGIC           0x484f5354u /* "HOST" */
#define BLOCK_VERSION_1_1        0x00010001u
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
#define BLOCK_QUEUE_DEPTH_SHIFT  24
#define BLOCK_OP_READ            1u
#define BLOCK_OP_WRITE           2u
#define BLOCK_OP_FLUSH           3u
#define BLOCK_SUBMIT             (1u << 0)
#define BLOCK_CPL_POP_BIT        (1u << 0)
#define BLOCK_STATE_ACK_BIT      (1u << 0)
#define BLOCK_RESET_BIT          (1u << 1)
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

#define BLOCK_REQUEST_QUEUE_DEPTH ASTRA_BLOCK_MAX_REQUESTS_PER_SERVICE
#define BLOCK_COMPLETION_QUEUE_SIZE (BLOCK_REQUEST_QUEUE_DEPTH * 2u)
#define BLOCK_COMPLETION_QUEUE_MASK (BLOCK_COMPLETION_QUEUE_SIZE - 1u)

_Static_assert(BLOCK_REQUEST_QUEUE_DEPTH <= 0x1fu,
               "block queue depth must fit its MMIO field");
_Static_assert((BLOCK_COMPLETION_QUEUE_SIZE &
                (BLOCK_COMPLETION_QUEUE_SIZE - 1u)) == 0u,
               "block completion queue must be a power of two");

/*
 * The physical service completes one transfer at a time over SPI. Completing
 * after a short virtual delay keeps the guest on the interrupt path it will
 * use on hardware instead of letting a store to BLOCK_REQ_SUBMIT finish the
 * transfer before the next instruction retires.
 */
#define BLOCK_SERVICE_DELAY_NS   20000ull
#define DISPLAY_SERVICE_DELAY_NS 1000000ull

#define NETWORK_ID_MAGIC         0x4e455457u
#define NETWORK_VERSION_1_0      0x00010000u
#define NETWORK_QUEUE_READY      (1u << 0)
#define NETWORK_QUEUE_EVENT_PENDING (1u << 1)
#define NETWORK_EXECUTE_BIT      (1u << 0)
#define NETWORK_READY_ACK_BIT    (1u << 0)
#define NETWORK_RESET_BIT        (1u << 0)
#define NETWORK_COMMAND_BYTES    ASTRA_NETWORK_HOST_COMMAND_SIZE
#define HOST_ACCEL_ID_MAGIC      ASTRA_DEVICE_CLASS_HOST
#define HOST_ACCEL_EXECUTE_BIT   (1u << 0)
#define HOST_ACCEL_RESET_BIT     (1u << 0)
#define HOST_ACCEL_MAX_TRANSFER  (2u * MiB)
#define HOST_SUBMIT_COMPLETED_SHIFT 16u

_Static_assert((ASTRA_HOST_CHANNEL_COUNT &
                (ASTRA_HOST_CHANNEL_COUNT - 1u)) == 0u,
               "host channel aperture must contain whole power-of-two pages");

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

typedef struct AstraHostBlockCompletion {
    uint32_t id;
    uint32_t status;
    uint32_t sectors;
    uint32_t detail;
    uint32_t media_generation;
    uint32_t host_generation;
} AstraHostBlockCompletion;

typedef struct AstraHostBlockRequest {
    QTAILQ_ENTRY(AstraHostBlockRequest) next;
    Astra68State *machine;
    BlockAIOCB *aiocb;
    QEMUIOVector qiov;
    uint32_t id;
    uint32_t operation;
    uint32_t sectors;
    uint32_t buffer;
    uint32_t media_generation;
    uint32_t host_generation;
    uint64_t lba;
    bool qiov_initialized;
    bool started;
} AstraHostBlockRequest;

typedef QTAILQ_HEAD(, AstraHostBlockRequest) AstraHostBlockRequestList;

typedef struct AstraBlockState {
    BlockBackend *blk;
    QEMUTimer *service_timer;
    AstraHostBlockCompletion completion[BLOCK_COMPLETION_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    AstraHostBlockRequestList requests;
    uint32_t request_count;
    uint32_t active_count;
    /* Registers the guest programs before writing BLOCK_REQ_SUBMIT. */
    uint32_t req_id;
    uint32_t req_op;
    uint32_t req_lba_hi;
    uint32_t req_lba_lo;
    uint32_t req_sectors;
    uint32_t req_buffer;
    uint32_t error;
    uint32_t host_generation;
    uint32_t media_generation;
    uint64_t media_sectors;
    uint64_t read_requests;
    uint64_t read_sectors;
    uint64_t write_requests;
    uint64_t write_sectors;
    uint64_t flush_requests;
    uint64_t durability_transitions;
    uint64_t cut_after_transition;
    bool write_enable;
    bool state_change;
    bool trace_durability;
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
    int mailbox_lock_fd;
    bool mailbox_enabled;
    bool busy;
    bool completion_valid;
} AstraDisplayState;

typedef struct AstraNetworkEndpointState {
    Astra68State *machine;
    int fd;
    uint32_t id;
    uint32_t generation;
    uint32_t readiness;
    uint32_t error_status;
    uint16_t family;
    uint8_t type;
    uint8_t protocol;
    bool listening;
    bool connecting;
    bool armed;
} AstraNetworkEndpointState;

typedef struct AstraNetworkResolveJob {
    Astra68State *machine;
    char *name;
    GArray *addresses;
    uint32_t token;
    uint32_t host_generation;
    uint16_t family;
    uint16_t port;
    uint8_t type;
    uint8_t protocol;
    int resolver_status;
    bool done;
    bool discard;
} AstraNetworkResolveJob;

typedef struct AstraNetworkState {
    GHashTable *endpoints;
    GHashTable *resolvers;
    uint32_t next_endpoint;
    uint32_t next_generation;
    uint32_t next_resolver;
    uint32_t host_generation;
    uint32_t ready_sequence;
    uint32_t request_buffer;
    uint32_t request_bytes;
    uint32_t request_count;
    uint32_t status;
    uint32_t completed;
    bool ready_pending;
} AstraNetworkState;

typedef struct AstraHostFile {
    int fd;
    DIR *directory;
    uint64_t directory_cookie;
    uint32_t owner;
    uint32_t references;
    uint16_t flags;
    QemuMutex lock;
    bool closing;
} AstraHostFile;

typedef struct AstraHostJob {
    Astra68State *machine;
    uint32_t slot;
    uint32_t owner;
    uint32_t host_generation;
    uint32_t channel_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t position;
    uint64_t started_ns;
} AstraHostJob;

typedef struct AstraHostChannel {
    uint32_t owner;
    uint32_t host_generation;
    uint32_t channel_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t consumer_position;
    uint32_t submitted_position;
    uint32_t status;
    uint32_t jobs;
    uint32_t interrupt_position;
    uint8_t *completed;
    bool active;
    bool interrupt_armed;
    bool completion_pending;
} AstraHostChannel;

typedef struct AstraHostState {
    GHashTable *files;
    QemuMutex files_lock;
    int root_fd;
    uint32_t next_handle;
    uint32_t generation;
    uint32_t request_buffer;
    uint32_t request_bytes;
    uint32_t request_count;
    uint32_t status;
    uint32_t completed;
    uint32_t owner;
    uint64_t submissions;
    uint64_t commands;
    uint64_t execution_ns;
    uint64_t operation_counts[ASTRA_HOST_FS_SYMLINK + 1u];
    uint64_t inflight;
    uint64_t max_inflight;
    uint32_t channel_result;
    bool completion_pending;
    AstraHostChannel channels[ASTRA_HOST_CHANNEL_COUNT];
} AstraHostState;

struct Astra68State {
    M68kCPU *cpu;
    MemoryRegion bram;
    MemoryRegion rom;
    MemoryRegion rom_alias;
    MemoryRegion text;
    MemoryRegion vesta_io;
    MemoryRegion host_channel_io;
    MemoryRegion panel_io;
    MemoryRegion astraea_io;
    MemoryRegion vega_io;
    MemoryRegion ohci_io;
    struct {
        bool present;
        uint32_t control;
        uint32_t command_status;
        uint32_t interrupt_status;
        uint32_t interrupt_enable;
        uint32_t hcca;
        uint32_t astra_status;
        uint32_t pool_base;
        uint32_t pool_size;
        uint32_t frame_number;
        QEMUTimer *sof_timer;
    } ohci;
    uint8_t *sdram;
    uint32_t ram_size;
    uint64_t reset_clock_ns;
    uint64_t rtc_latch;
    uint64_t rtc_base_ns;
    uint64_t rtc_base_clock_ns;
    uint32_t rtc_set_high;
    bool rtc_valid;
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
    AstraNetworkState network;
    AstraHostState host;
    uint8_t panel_led_data;
    uint8_t panel_led_ownership;
#ifdef CONFIG_POSIX
    volatile uint32_t *host_panel;
#endif
    bool trace_timers;
};

static Astra68State *astra_input_machine;

static void astra_update_irq(Astra68State *s);
static uint32_t astra_ohci_astra_status(Astra68State *s);
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
    uint32_t reserved = level + block->request_count;
    uint32_t queue =
        BLOCK_REQUEST_QUEUE_DEPTH << BLOCK_QUEUE_DEPTH_SHIFT |
        level << BLOCK_QUEUE_COMPLETION_SHIFT |
        block->request_count;

    if (level != 0) {
        queue |= BLOCK_QUEUE_COMPLETION_VALID;
    }
    if (astra_block_present(s) && reserved < BLOCK_REQUEST_QUEUE_DEPTH) {
        queue |= BLOCK_QUEUE_REQUEST_READY;
    }
    return queue;
}

static void astra_block_push_completion(Astra68State *s,
                                        const AstraHostBlockRequest *request,
                                        uint32_t status, uint32_t sectors,
                                        uint32_t detail)
{
    AstraBlockState *block = &s->block;
    AstraHostBlockCompletion *completion = &block->completion[block->tail];

    g_assert(astra_block_completion_level(block) <
             BLOCK_COMPLETION_QUEUE_MASK);
    completion->id = request->id;
    completion->status = status;
    completion->sectors = sectors;
    completion->detail = detail;
    completion->media_generation = request->media_generation;
    completion->host_generation = request->host_generation;
    block->tail = (block->tail + 1u) & BLOCK_COMPLETION_QUEUE_MASK;
}

static void astra_block_service(void *opaque);

static void astra_block_complete(void *opaque, int rc)
{
    AstraHostBlockRequest *request = opaque;
    Astra68State *s = request->machine;
    AstraBlockState *block;
    uint32_t sectors = request->sectors;

    if (request->qiov_initialized) {
        qemu_iovec_destroy(&request->qiov);
    }
    if (s == NULL) {
        g_free(request);
        return;
    }
    block = &s->block;
    QTAILQ_REMOVE(&block->requests, request, next);
    g_assert(block->request_count != 0u && block->active_count != 0u);
    --block->request_count;
    --block->active_count;
    if (request->operation == BLOCK_OP_FLUSH) {
        sectors = 0;
    }
    if (rc < 0) {
        astra_block_push_completion(s, request, BLOCK_COMPLETION_IO_ERROR,
                                    0, (uint32_t)-rc);
    } else {
        if (request->operation == BLOCK_OP_WRITE ||
            request->operation == BLOCK_OP_FLUSH) {
            const char *op = request->operation == BLOCK_OP_WRITE ?
                "write" : "flush";

            ++block->durability_transitions;
            if (block->durability_transitions ==
                block->cut_after_transition) {
                error_report("Astra68 block power cut: transition=%" PRIu64
                             " op=%s", block->durability_transitions, op);
                fflush(stderr);
                _exit(86);
            } else if (block->trace_durability) {
                error_report("Astra68 block durability transition=%" PRIu64
                             " op=%s", block->durability_transitions, op);
            }
        }
        astra_block_push_completion(s, request, BLOCK_COMPLETION_OK,
                                    sectors, 0);
    }
    astra_block_service(s);
    astra_update_irq(s);
    g_free(request);
}

static void astra_block_start(AstraHostBlockRequest *request)
{
    Astra68State *s = request->machine;
    AstraBlockState *block = &s->block;
    uint8_t *buffer;
    size_t bytes;

    request->started = true;
    ++block->active_count;
    bytes = (size_t)request->sectors * BLOCK_SECTOR_SIZE;
    buffer = s->sdram + (request->buffer - ASTRA_SDRAM_BASE);

    switch (request->operation) {
    case BLOCK_OP_READ:
        ++block->read_requests;
        block->read_sectors += request->sectors;
        qemu_iovec_init_buf(&request->qiov, buffer, bytes);
        request->qiov_initialized = true;
        request->aiocb = blk_aio_preadv(
            block->blk, (int64_t)request->lba * BLOCK_SECTOR_SIZE,
            &request->qiov, 0, astra_block_complete, request);
        break;
    case BLOCK_OP_WRITE:
        ++block->write_requests;
        block->write_sectors += request->sectors;
        qemu_iovec_init_buf(&request->qiov, buffer, bytes);
        request->qiov_initialized = true;
        request->aiocb = blk_aio_pwritev(
            block->blk, (int64_t)request->lba * BLOCK_SECTOR_SIZE,
            &request->qiov, 0, astra_block_complete, request);
        break;
    default:
        ++block->flush_requests;
        request->aiocb = blk_aio_flush(block->blk, astra_block_complete,
                                       request);
        break;
    }
}

static void astra_block_service(void *opaque)
{
    Astra68State *s = opaque;
    AstraBlockState *block = &s->block;
    AstraHostBlockRequest *request;

    QTAILQ_FOREACH(request, &block->requests, next) {
        if (request->started) {
            if (request->operation == BLOCK_OP_FLUSH) {
                return;
            }
            continue;
        }
        if (request->operation == BLOCK_OP_FLUSH) {
            if (block->active_count == 0u) {
                astra_block_start(request);
            }
            return;
        }
        astra_block_start(request);
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
    if (astra_block_completion_level(block) + block->request_count >=
        BLOCK_REQUEST_QUEUE_DEPTH) {
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
    AstraHostBlockRequest *request;
    uint32_t error = astra_block_validate(s);

    if (error != 0) {
        block->error |= error;
        return;
    }

    request = g_new0(AstraHostBlockRequest, 1);
    request->machine = s;
    request->id = block->req_id;
    request->operation = block->req_op & 0xffu;
    request->sectors = block->req_sectors & 0xffffu;
    request->buffer = block->req_buffer;
    request->lba = ((uint64_t)block->req_lba_hi << 32) |
                   block->req_lba_lo;
    request->media_generation = block->media_generation;
    request->host_generation = block->host_generation;
    QTAILQ_INSERT_TAIL(&block->requests, request, next);
    ++block->request_count;
    astra_panel_write32(s, ASTRA_PANEL_ACTIVITY, 1u);
    if (!timer_pending(block->service_timer)) {
        timer_mod_ns(block->service_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     BLOCK_SERVICE_DELAY_NS);
    }
}

static void astra_block_pop_completion(Astra68State *s)
{
    AstraBlockState *block = &s->block;

    if (astra_block_completion_level(block) == 0) {
        return;
    }
    block->head = (block->head + 1u) & BLOCK_COMPLETION_QUEUE_MASK;
}

static void astra_block_reset(Astra68State *s)
{
    AstraBlockState *block = &s->block;
    AstraHostBlockRequest *request;

    if (block->service_timer != NULL) {
        timer_del(block->service_timer);
    }
    while ((request = QTAILQ_FIRST(&block->requests)) != NULL) {
        QTAILQ_REMOVE(&block->requests, request, next);
        request->machine = NULL;
        if (request->aiocb != NULL) {
            /*
             * Device reset is the DMA ownership boundary. Synchronous cancel
             * waits through the completion callback, so the guest may safely
             * reuse every lane as soon as this MMIO write returns.
             */
            blk_aio_cancel(request->aiocb);
        } else {
            if (request->qiov_initialized) {
                qemu_iovec_destroy(&request->qiov);
            }
            g_free(request);
        }
    }
    QTAILQ_INIT(&block->requests);
    block->head = 0u;
    block->tail = 0u;
    block->request_count = 0u;
    block->active_count = 0u;
    block->error = 0u;
    block->req_id = 0u;
    block->req_op = 0u;
    block->req_lba_hi = 0u;
    block->req_lba_lo = 0u;
    block->req_sectors = 0u;
    block->req_buffer = 0u;
    memset(block->completion, 0, sizeof(block->completion));
    ++block->host_generation;
    block->state_change = astra_block_present(s);
    astra_panel_write32(s, ASTRA_PANEL_ACTIVITY, 0u);
    astra_update_irq(s);
}

static uint32_t astra_network_status_from_errno(int error)
{
    switch (error) {
    case 0: return ASTRA_NETWORK_OK;
    case EAGAIN: return ASTRA_NETWORK_WOULD_BLOCK;
#if EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK: return ASTRA_NETWORK_WOULD_BLOCK;
#endif
    case EINPROGRESS: return ASTRA_NETWORK_IN_PROGRESS;
    case ECANCELED: return ASTRA_NETWORK_CANCELLED;
    case ETIMEDOUT: return ASTRA_NETWORK_TIMED_OUT;
    case ECONNREFUSED: return ASTRA_NETWORK_REFUSED;
    case ECONNRESET:
    case EPIPE: return ASTRA_NETWORK_RESET;
    case ENETUNREACH:
    case EHOSTUNREACH: return ASTRA_NETWORK_UNREACHABLE;
    case EADDRINUSE: return ASTRA_NETWORK_ADDRESS_IN_USE;
    case EADDRNOTAVAIL: return ASTRA_NETWORK_ADDRESS_NOT_AVAILABLE;
    case EACCES:
    case EPERM: return ASTRA_NETWORK_ACCESS;
    case ENOMEM:
    case ENOBUFS:
    case EMFILE:
    case ENFILE: return ASTRA_NETWORK_RESOURCE_LIMIT;
    case EAFNOSUPPORT:
    case EPROTONOSUPPORT:
    case ENOPROTOOPT:
    case EOPNOTSUPP: return ASTRA_NETWORK_UNSUPPORTED;
    default: return ASTRA_NETWORK_IO;
    }
}

static uint32_t astra_network_status_from_gai(int status)
{
    if (status == 0) {
        return ASTRA_NETWORK_OK;
    }
#ifdef EAI_AGAIN
    if (status == EAI_AGAIN) {
        return ASTRA_NETWORK_NAME_TEMPORARY;
    }
#endif
#ifdef EAI_NONAME
    if (status == EAI_NONAME) {
        return ASTRA_NETWORK_NAME_NOT_FOUND;
    }
#endif
#ifdef EAI_MEMORY
    if (status == EAI_MEMORY) {
        return ASTRA_NETWORK_OUT_OF_MEMORY;
    }
#endif
#ifdef EAI_FAMILY
    if (status == EAI_FAMILY) {
        return ASTRA_NETWORK_UNSUPPORTED;
    }
#endif
    return ASTRA_NETWORK_IO;
}

static uint32_t astra_network_next(uint32_t *value)
{
    if (++*value == 0) {
        ++*value;
    }
    return *value;
}

static bool astra_network_guest_address(const uint8_t *source,
                                        struct sockaddr_storage *storage,
                                        socklen_t *length)
{
    uint16_t family = lduw_be_p(source + 4);
    uint16_t port = lduw_be_p(source + 6);

    memset(storage, 0, sizeof(*storage));
    if (ldl_be_p(source) != ASTRA_NETWORK_ADDRESS_SIZE) {
        return false;
    }
    if (family == ASTRA_NETWORK_FAMILY_IPV4) {
        struct sockaddr_in *address = (struct sockaddr_in *)storage;

        address->sin_family = AF_INET;
        address->sin_port = htons(port);
        memcpy(&address->sin_addr, source + 12, sizeof(address->sin_addr));
        *length = sizeof(*address);
        return true;
    }
    if (family == ASTRA_NETWORK_FAMILY_IPV6) {
        struct sockaddr_in6 *address = (struct sockaddr_in6 *)storage;

        address->sin6_family = AF_INET6;
        address->sin6_port = htons(port);
        address->sin6_scope_id = ldl_be_p(source + 8);
        memcpy(&address->sin6_addr, source + 12, sizeof(address->sin6_addr));
        *length = sizeof(*address);
        return true;
    }
    return false;
}

static bool astra_network_store_address(uint8_t *destination,
                                        const struct sockaddr *source,
                                        socklen_t length)
{
    memset(destination, 0, ASTRA_NETWORK_ADDRESS_SIZE);
    stl_be_p(destination, ASTRA_NETWORK_ADDRESS_SIZE);
    if (source->sa_family == AF_INET &&
        length >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *address =
            (const struct sockaddr_in *)source;

        stw_be_p(destination + 4, ASTRA_NETWORK_FAMILY_IPV4);
        stw_be_p(destination + 6, ntohs(address->sin_port));
        memcpy(destination + 12, &address->sin_addr,
               sizeof(address->sin_addr));
        return true;
    }
    if (source->sa_family == AF_INET6 &&
        length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *address =
            (const struct sockaddr_in6 *)source;

        stw_be_p(destination + 4, ASTRA_NETWORK_FAMILY_IPV6);
        stw_be_p(destination + 6, ntohs(address->sin6_port));
        stl_be_p(destination + 8, address->sin6_scope_id);
        memcpy(destination + 12, &address->sin6_addr,
               sizeof(address->sin6_addr));
        return true;
    }
    return false;
}

static void astra_network_endpoint_free(gpointer opaque)
{
    AstraNetworkEndpointState *endpoint = opaque;

    if (endpoint->fd >= 0) {
        qemu_set_fd_handler(endpoint->fd, NULL, NULL, NULL);
        close(endpoint->fd);
    }
    g_free(endpoint);
}

static void astra_network_resolver_free(gpointer opaque)
{
    AstraNetworkResolveJob *job = opaque;

    g_free(job->name);
    if (job->addresses) {
        g_array_free(job->addresses, true);
    }
    g_free(job);
}

static void astra_network_raise(Astra68State *s)
{
    s->network.ready_pending = true;
    astra_network_next(&s->network.ready_sequence);
    astra_update_irq(s);
}

static void astra_network_disarm(AstraNetworkEndpointState *endpoint)
{
    if (endpoint->armed) {
        qemu_set_fd_handler(endpoint->fd, NULL, NULL, NULL);
        endpoint->armed = false;
    }
}

static void astra_network_ready(AstraNetworkEndpointState *endpoint,
                                uint32_t readiness)
{
    endpoint->readiness |= readiness;
    astra_network_disarm(endpoint);
    astra_network_raise(endpoint->machine);
}

static void astra_network_read_ready(void *opaque)
{
    AstraNetworkEndpointState *endpoint = opaque;

    astra_network_ready(endpoint, endpoint->listening ?
                        ASTRA_NETWORK_READY_ACCEPTABLE :
                        ASTRA_NETWORK_READY_READABLE);
}

static void astra_network_write_ready(void *opaque)
{
    AstraNetworkEndpointState *endpoint = opaque;
    int error = 0;
    socklen_t length = sizeof(error);

    if (endpoint->connecting) {
        if (getsockopt(endpoint->fd, SOL_SOCKET, SO_ERROR,
                       (void *)&error, &length) < 0) {
            error = errno;
        }
        endpoint->connecting = false;
        endpoint->error_status = astra_network_status_from_errno(error);
        astra_network_ready(endpoint, error == 0 ?
                            ASTRA_NETWORK_READY_CONNECTED |
                                ASTRA_NETWORK_READY_WRITABLE :
                            ASTRA_NETWORK_READY_ERROR);
        return;
    }
    astra_network_ready(endpoint, ASTRA_NETWORK_READY_WRITABLE);
}

static AstraNetworkEndpointState *
astra_network_endpoint(Astra68State *s, uint32_t id, uint32_t generation)
{
    AstraNetworkEndpointState *endpoint = g_hash_table_lookup(
        s->network.endpoints, GUINT_TO_POINTER(id));

    return endpoint != NULL && endpoint->generation == generation ?
           endpoint : NULL;
}

static AstraNetworkEndpointState *
astra_network_endpoint_create(Astra68State *s, int fd, uint16_t family,
                              uint8_t type, uint8_t protocol)
{
    AstraNetworkEndpointState *endpoint = g_new0(
        AstraNetworkEndpointState, 1);

    endpoint->machine = s;
    endpoint->fd = fd;
    endpoint->id = astra_network_next(&s->network.next_endpoint);
    while (g_hash_table_contains(s->network.endpoints,
                                 GUINT_TO_POINTER(endpoint->id))) {
        endpoint->id = astra_network_next(&s->network.next_endpoint);
    }
    endpoint->generation = astra_network_next(&s->network.next_generation);
    endpoint->family = family;
    endpoint->type = type;
    endpoint->protocol = protocol;
    g_hash_table_insert(s->network.endpoints,
                        GUINT_TO_POINTER(endpoint->id), endpoint);
    return endpoint;
}

static int astra_network_resolve_worker(void *opaque)
{
    AstraNetworkResolveJob *job = opaque;
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    struct addrinfo *current;
    int status;

    hints.ai_family = job->family == ASTRA_NETWORK_FAMILY_IPV4 ? AF_INET :
                      job->family == ASTRA_NETWORK_FAMILY_IPV6 ? AF_INET6 :
                      AF_UNSPEC;
    hints.ai_socktype = job->type == ASTRA_NETWORK_TYPE_STREAM ? SOCK_STREAM :
                        job->type == ASTRA_NETWORK_TYPE_DATAGRAM ? SOCK_DGRAM :
                        0;
    hints.ai_protocol = job->protocol;
    status = getaddrinfo(job->name, NULL, &hints, &result);
    if (status == 0) {
        for (current = result; current != NULL; current = current->ai_next) {
            AstraNetworkAddress address = {0};

            address.size = sizeof(address);
            address.port = job->port;
            if (current->ai_family == AF_INET) {
                const struct sockaddr_in *ipv4 =
                    (const struct sockaddr_in *)current->ai_addr;

                address.family = ASTRA_NETWORK_FAMILY_IPV4;
                memcpy(address.address, &ipv4->sin_addr,
                       sizeof(ipv4->sin_addr));
            } else if (current->ai_family == AF_INET6) {
                const struct sockaddr_in6 *ipv6 =
                    (const struct sockaddr_in6 *)current->ai_addr;

                address.family = ASTRA_NETWORK_FAMILY_IPV6;
                address.scope_id = ipv6->sin6_scope_id;
                memcpy(address.address, &ipv6->sin6_addr,
                       sizeof(ipv6->sin6_addr));
            } else {
                continue;
            }
            g_array_append_val(job->addresses, address);
        }
        freeaddrinfo(result);
    }
    job->resolver_status = status;
    if (status != 0) {
        error_report("Astra68 host resolver failed for '%s': %s (%d), "
                     "family=%u type=%u protocol=%u",
                     job->name, gai_strerror(status), status, job->family,
                     job->type, job->protocol);
    }
    return 0;
}

static void astra_network_resolve_complete(void *opaque, int ret)
{
    AstraNetworkResolveJob *job = opaque;
    Astra68State *s = job->machine;

    (void)ret;
    if (job->discard || job->host_generation != s->network.host_generation) {
        g_hash_table_remove(s->network.resolvers,
                            GUINT_TO_POINTER(job->token));
        return;
    }
    job->done = true;
    astra_network_raise(s);
}

static void astra_network_reset(Astra68State *s)
{
    GHashTableIter iterator;
    gpointer value;

    if (s->network.endpoints) {
        g_hash_table_remove_all(s->network.endpoints);
    }
    if (s->network.resolvers) {
        g_hash_table_iter_init(&iterator, s->network.resolvers);
        while (g_hash_table_iter_next(&iterator, NULL, &value)) {
            AstraNetworkResolveJob *job = value;

            if (job->done) {
                g_hash_table_iter_remove(&iterator);
            } else {
                job->discard = true;
            }
        }
    }
    astra_network_next(&s->network.host_generation);
    s->network.ready_pending = false;
    s->network.request_buffer = 0;
    s->network.request_bytes = 0;
    s->network.request_count = 0;
    s->network.status = ASTRA_SYSCALL_OK;
    s->network.completed = 0;
    astra_update_irq(s);
}

static uint8_t *astra_dma_data(Astra68State *s, uint32_t physical,
                               uint32_t bytes, uint32_t offset,
                               uint32_t length)
{
    if (physical < ASTRA_SDRAM_BASE || bytes > s->ram_size ||
        physical - ASTRA_SDRAM_BASE > s->ram_size - bytes ||
        offset > bytes || length > bytes - offset) {
        return NULL;
    }
    return s->sdram + physical - ASTRA_SDRAM_BASE + offset;
}

static void astra_network_command_clear_result(uint8_t *command)
{
    memset(command + 68, 0, 44);
    stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
}

static void astra_network_command_address(uint8_t *destination,
                                          const AstraNetworkAddress *source)
{
    memset(destination, 0, ASTRA_NETWORK_ADDRESS_SIZE);
    stl_be_p(destination, sizeof(*source));
    stw_be_p(destination + 4, source->family);
    stw_be_p(destination + 6, source->port);
    stl_be_p(destination + 8, source->scope_id);
    memcpy(destination + 12, source->address, sizeof(source->address));
}

static int astra_network_socket_option(uint32_t option, int *level,
                                       int *name)
{
    switch (option) {
    case ASTRA_NETWORK_OPTION_REUSE_ADDRESS:
        *level = SOL_SOCKET; *name = SO_REUSEADDR; return 1;
    case ASTRA_NETWORK_OPTION_KEEPALIVE:
        *level = SOL_SOCKET; *name = SO_KEEPALIVE; return 1;
    case ASTRA_NETWORK_OPTION_SEND_BUFFER:
        *level = SOL_SOCKET; *name = SO_SNDBUF; return 1;
    case ASTRA_NETWORK_OPTION_RECEIVE_BUFFER:
        *level = SOL_SOCKET; *name = SO_RCVBUF; return 1;
    case ASTRA_NETWORK_OPTION_TCP_NO_DELAY:
        *level = IPPROTO_TCP; *name = TCP_NODELAY; return 1;
    case ASTRA_NETWORK_OPTION_IPV6_ONLY:
        *level = IPPROTO_IPV6; *name = IPV6_V6ONLY; return 1;
    default:
        return 0;
    }
}

static uint32_t astra_network_arm_endpoint(AstraNetworkEndpointState *endpoint,
                                           uint32_t interest)
{
    struct pollfd pollfd = {0};
    uint32_t readiness = 0;
    int result;

    if ((interest & ~(ASTRA_NETWORK_READY_READABLE |
                      ASTRA_NETWORK_READY_WRITABLE |
                      ASTRA_NETWORK_READY_CONNECTED |
                      ASTRA_NETWORK_READY_ACCEPTABLE)) != 0) {
        return ASTRA_NETWORK_INVALID;
    }
    if (endpoint->readiness != 0) {
        readiness = endpoint->readiness;
        endpoint->readiness = 0;
        goto arm_remaining;
    }
    pollfd.fd = endpoint->fd;
    if ((interest & (ASTRA_NETWORK_READY_READABLE |
                     ASTRA_NETWORK_READY_ACCEPTABLE)) != 0) {
        pollfd.events |= POLLIN;
    }
    if ((interest & (ASTRA_NETWORK_READY_WRITABLE |
                     ASTRA_NETWORK_READY_CONNECTED)) != 0) {
        pollfd.events |= POLLOUT;
    }
    result = poll(&pollfd, 1, 0);
    if (result < 0) {
        endpoint->error_status =
            astra_network_status_from_errno(errno);
        return ASTRA_NETWORK_READY_ERROR;
    }
    if (result != 0) {
        if ((pollfd.revents & (POLLERR | POLLNVAL)) != 0) {
            readiness |= ASTRA_NETWORK_READY_ERROR;
        }
        if ((pollfd.revents & POLLHUP) != 0) {
            readiness |= ASTRA_NETWORK_READY_PEER_CLOSED;
        }
        if ((pollfd.revents & POLLIN) != 0) {
            readiness |= endpoint->listening ?
                ASTRA_NETWORK_READY_ACCEPTABLE :
                ASTRA_NETWORK_READY_READABLE;
        }
        if ((pollfd.revents & POLLOUT) != 0) {
            if (endpoint->connecting) {
                astra_network_write_ready(endpoint);
                readiness |= endpoint->readiness;
                endpoint->readiness = 0;
            } else {
                readiness |= ASTRA_NETWORK_READY_WRITABLE;
            }
        }
    }

arm_remaining:
    if ((readiness & (ASTRA_NETWORK_READY_ERROR |
                      ASTRA_NETWORK_READY_PEER_CLOSED)) != 0) {
        interest = 0;
    } else {
        if ((readiness & (ASTRA_NETWORK_READY_READABLE |
                          ASTRA_NETWORK_READY_ACCEPTABLE)) != 0) {
            interest &= ~(ASTRA_NETWORK_READY_READABLE |
                          ASTRA_NETWORK_READY_ACCEPTABLE);
        }
        if ((readiness & (ASTRA_NETWORK_READY_WRITABLE |
                          ASTRA_NETWORK_READY_CONNECTED)) != 0) {
            interest &= ~(ASTRA_NETWORK_READY_WRITABLE |
                          ASTRA_NETWORK_READY_CONNECTED);
        }
    }
    astra_network_disarm(endpoint);
    if (interest == 0)
        return readiness;
    pollfd.events = 0;
    if ((interest & (ASTRA_NETWORK_READY_READABLE |
                     ASTRA_NETWORK_READY_ACCEPTABLE)) != 0)
        pollfd.events |= POLLIN;
    if ((interest & (ASTRA_NETWORK_READY_WRITABLE |
                     ASTRA_NETWORK_READY_CONNECTED)) != 0)
        pollfd.events |= POLLOUT;
    qemu_set_fd_handler(endpoint->fd,
                        (pollfd.events & POLLIN) != 0 ?
                            astra_network_read_ready : NULL,
                        (pollfd.events & POLLOUT) != 0 ?
                            astra_network_write_ready : NULL,
                        endpoint);
    endpoint->armed = true;
    return readiness;
}

static void astra_network_execute_resolve(Astra68State *s, uint8_t *command,
                                          uint32_t physical, uint32_t bytes)
{
    uint32_t token = ldl_be_p(command + 24);
    uint32_t data_offset = ldl_be_p(command + 56);
    uint32_t data_length = ldl_be_p(command + 60);
    uint32_t data_capacity = ldl_be_p(command + 64);
    uint8_t *data;

    if (token == 0) {
        AstraNetworkResolveJob *job;

        data = astra_dma_data(s, physical, bytes, data_offset, data_length);
        if (data == NULL || data_length == 0 || data_length > 253 ||
            memchr(data, 0, data_length) != NULL) {
            stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
            return;
        }
        job = g_new0(AstraNetworkResolveJob, 1);
        job->machine = s;
        job->name = g_strndup((const char *)data, data_length);
        job->addresses = g_array_new(false, false,
                                     sizeof(AstraNetworkAddress));
        job->token = astra_network_next(&s->network.next_resolver);
        while (g_hash_table_contains(s->network.resolvers,
                                     GUINT_TO_POINTER(job->token))) {
            job->token = astra_network_next(&s->network.next_resolver);
        }
        job->host_generation = s->network.host_generation;
        job->family = lduw_be_p(command + 20);
        job->type = command[22];
        job->protocol = command[23];
        job->port = lduw_be_p(command + 34);
        g_hash_table_insert(s->network.resolvers,
                            GUINT_TO_POINTER(job->token), job);
        thread_pool_submit_aio(astra_network_resolve_worker, job,
                               astra_network_resolve_complete, job);
        stl_be_p(command + 68, ASTRA_NETWORK_IN_PROGRESS);
        stl_be_p(command + 80, job->token);
        return;
    }
    {
        AstraNetworkResolveJob *job = g_hash_table_lookup(
            s->network.resolvers, GUINT_TO_POINTER(token));
        uint32_t count;

        if (job == NULL || job->host_generation !=
                               s->network.host_generation) {
            stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
            return;
        }
        if (!job->done) {
            stl_be_p(command + 68, ASTRA_NETWORK_WOULD_BLOCK);
            return;
        }
        if (job->resolver_status != 0) {
            stl_be_p(command + 68,
                     astra_network_status_from_gai(job->resolver_status));
            g_hash_table_remove(s->network.resolvers,
                                GUINT_TO_POINTER(token));
            return;
        }
        count = job->addresses->len;
        stl_be_p(command + 80, count);
        if (data_capacity / ASTRA_NETWORK_ADDRESS_SIZE < count) {
            stl_be_p(command + 68, ASTRA_NETWORK_BUFFER_TOO_SMALL);
            return;
        }
        data = astra_dma_data(s, physical, bytes, data_offset,
                              count * ASTRA_NETWORK_ADDRESS_SIZE);
        if (data == NULL) {
            stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
            return;
        }
        for (uint32_t index = 0; index < count; ++index) {
            AstraNetworkAddress *address = &g_array_index(
                job->addresses, AstraNetworkAddress, index);

            astra_network_command_address(
                data + index * ASTRA_NETWORK_ADDRESS_SIZE, address);
        }
        stl_be_p(command + 68, count == 0 ?
                 ASTRA_NETWORK_NAME_NOT_FOUND : ASTRA_NETWORK_OK);
        stl_be_p(command + 80, count);
        g_hash_table_remove(s->network.resolvers, GUINT_TO_POINTER(token));
    }
}

static void astra_network_execute_command(Astra68State *s, uint8_t *command,
                                          uint32_t physical, uint32_t bytes)
{
    uint16_t operation;
    uint32_t id;
    uint32_t generation;
    AstraNetworkEndpointState *endpoint;
    struct sockaddr_storage address;
    socklen_t address_length;
    uint32_t status = ASTRA_NETWORK_OK;

    astra_network_command_clear_result(command);
    if (ldl_be_p(command) != NETWORK_COMMAND_BYTES ||
        lduw_be_p(command + 4) != ASTRA_NETWORK_HOST_COMMAND_VERSION) {
        return;
    }
    operation = lduw_be_p(command + 6);
    id = ldl_be_p(command + 12);
    generation = ldl_be_p(command + 16);
    if (operation == ASTRA_NETWORK_HOST_RESOLVE) {
        astra_network_execute_resolve(s, command, physical, bytes);
        return;
    }
    if (operation == ASTRA_NETWORK_HOST_CANCEL) {
        uint32_t token = ldl_be_p(command + 24);
        AstraNetworkResolveJob *job = g_hash_table_lookup(
            s->network.resolvers, GUINT_TO_POINTER(token));

        if (job == NULL) {
            return;
        }
        if (job->done) {
            g_hash_table_remove(s->network.resolvers,
                                GUINT_TO_POINTER(token));
        } else {
            job->discard = true;
        }
        stl_be_p(command + 68, ASTRA_NETWORK_CANCELLED);
        return;
    }
    if (operation == ASTRA_NETWORK_HOST_ENDPOINT_OPEN) {
        uint16_t family = lduw_be_p(command + 20);
        uint8_t type = command[22];
        uint8_t protocol = command[23];
        int native_family = family == ASTRA_NETWORK_FAMILY_IPV4 ? AF_INET :
                            family == ASTRA_NETWORK_FAMILY_IPV6 ? AF_INET6 :
                            -1;
        int native_type = type == ASTRA_NETWORK_TYPE_STREAM ? SOCK_STREAM :
                          type == ASTRA_NETWORK_TYPE_DATAGRAM ? SOCK_DGRAM :
                          -1;
        int fd;

        if (native_family < 0 || native_type < 0 ||
            (protocol != ASTRA_NETWORK_PROTOCOL_DEFAULT &&
             protocol != ASTRA_NETWORK_PROTOCOL_ICMP &&
             protocol != ASTRA_NETWORK_PROTOCOL_TCP &&
             protocol != ASTRA_NETWORK_PROTOCOL_UDP &&
             protocol != ASTRA_NETWORK_PROTOCOL_ICMPV6)) {
            return;
        }
        fd = qemu_socket(native_family, native_type, protocol);
        if (fd < 0) {
            stl_be_p(command + 68,
                     astra_network_status_from_errno(errno));
            return;
        }
        qemu_socket_set_nonblock(fd);
        endpoint = astra_network_endpoint_create(s, fd, family, type,
                                                 protocol);
        stl_be_p(command + 68, ASTRA_NETWORK_OK);
        stl_be_p(command + 72, endpoint->id);
        stl_be_p(command + 76, endpoint->generation);
        return;
    }
    endpoint = astra_network_endpoint(s, id, generation);
    if (endpoint == NULL) {
        return;
    }
    switch (operation) {
    case ASTRA_NETWORK_HOST_BIND:
    case ASTRA_NETWORK_HOST_CONNECT:
        if (!astra_network_guest_address(command + 28, &address,
                                          &address_length)) {
            status = ASTRA_NETWORK_INVALID;
            break;
        }
        if (operation == ASTRA_NETWORK_HOST_BIND) {
            if (bind(endpoint->fd, (struct sockaddr *)&address,
                     address_length) < 0) {
                status = astra_network_status_from_errno(errno);
            }
        } else if (connect(endpoint->fd, (struct sockaddr *)&address,
                           address_length) < 0) {
            status = astra_network_status_from_errno(errno);
            if (status == ASTRA_NETWORK_IN_PROGRESS) {
                endpoint->connecting = true;
            }
        }
        break;
    case ASTRA_NETWORK_HOST_LISTEN:
        if (listen(endpoint->fd, ldl_be_p(command + 24)) < 0) {
            status = astra_network_status_from_errno(errno);
        } else {
            endpoint->listening = true;
        }
        break;
    case ASTRA_NETWORK_HOST_ACCEPT: {
        address_length = sizeof(address);
        int fd = qemu_accept(endpoint->fd, (struct sockaddr *)&address,
                             &address_length);

        if (fd < 0) {
            status = astra_network_status_from_errno(errno);
        } else {
            AstraNetworkEndpointState *accepted;

            qemu_socket_set_nonblock(fd);
            accepted = astra_network_endpoint_create(
                s, fd, endpoint->family, endpoint->type, endpoint->protocol);
            stl_be_p(command + 72, accepted->id);
            stl_be_p(command + 76, accepted->generation);
            if (!astra_network_store_address(command + 84,
                                              (struct sockaddr *)&address,
                                              address_length)) {
                g_hash_table_remove(s->network.endpoints,
                                    GUINT_TO_POINTER(accepted->id));
                status = ASTRA_NETWORK_IO;
            }
        }
        break;
    }
    case ASTRA_NETWORK_HOST_SEND:
    case ASTRA_NETWORK_HOST_RECEIVE: {
        uint32_t offset = ldl_be_p(command + 56);
        uint32_t length = ldl_be_p(command + 60);
        uint32_t capacity = ldl_be_p(command + 64);
        uint32_t flags = ldl_be_p(command + 8);
        uint8_t *data = astra_dma_data(
            s, physical, bytes, offset,
            operation == ASTRA_NETWORK_HOST_SEND ? length : capacity);
        int native_flags = 0;
        ssize_t moved;

        if (data == NULL || (flags & ~(ASTRA_NETWORK_MESSAGE_PEEK |
                                       ASTRA_NETWORK_MESSAGE_WAIT_ALL |
                                       ASTRA_NETWORK_MESSAGE_TRUNCATE)) != 0) {
            status = ASTRA_NETWORK_INVALID;
            break;
        }
        if ((flags & ASTRA_NETWORK_MESSAGE_PEEK) != 0) native_flags |= MSG_PEEK;
        if ((flags & ASTRA_NETWORK_MESSAGE_WAIT_ALL) != 0)
            native_flags |= MSG_WAITALL;
#ifdef MSG_TRUNC
        if ((flags & ASTRA_NETWORK_MESSAGE_TRUNCATE) != 0)
            native_flags |= MSG_TRUNC;
#endif
#ifdef MSG_NOSIGNAL
        if (operation == ASTRA_NETWORK_HOST_SEND) native_flags |= MSG_NOSIGNAL;
#endif
        address_length = sizeof(address);
        if (operation == ASTRA_NETWORK_HOST_SEND) {
            if (lduw_be_p(command + 32) != ASTRA_NETWORK_FAMILY_UNSPEC) {
                if (!astra_network_guest_address(command + 28, &address,
                                                  &address_length)) {
                    status = ASTRA_NETWORK_INVALID;
                    break;
                }
                moved = sendto(endpoint->fd, data, length, native_flags,
                               (struct sockaddr *)&address, address_length);
            } else {
                moved = send(endpoint->fd, data, length, native_flags);
            }
        } else {
            moved = recvfrom(endpoint->fd, data, capacity, native_flags,
                             (struct sockaddr *)&address, &address_length);
        }
        if (moved < 0) {
            status = astra_network_status_from_errno(errno);
        } else if (moved == 0 && operation == ASTRA_NETWORK_HOST_RECEIVE &&
                   endpoint->type == ASTRA_NETWORK_TYPE_STREAM) {
            status = ASTRA_NETWORK_PEER_CLOSED;
        } else {
            stl_be_p(command + 80, moved);
            if (operation == ASTRA_NETWORK_HOST_RECEIVE) {
                (void)astra_network_store_address(
                    command + 84, (struct sockaddr *)&address,
                    address_length);
            }
        }
        break;
    }
    case ASTRA_NETWORK_HOST_GET_LOCAL_ADDRESS:
    case ASTRA_NETWORK_HOST_GET_PEER_ADDRESS:
        address_length = sizeof(address);
        if ((operation == ASTRA_NETWORK_HOST_GET_LOCAL_ADDRESS ?
             getsockname(endpoint->fd, (struct sockaddr *)&address,
                         &address_length) :
             getpeername(endpoint->fd, (struct sockaddr *)&address,
                         &address_length)) < 0) {
            status = astra_network_status_from_errno(errno);
        } else if (!astra_network_store_address(
                       command + 84, (struct sockaddr *)&address,
                       address_length)) {
            status = ASTRA_NETWORK_IO;
        }
        break;
    case ASTRA_NETWORK_HOST_GET_OPTION: {
        uint32_t option = ldl_be_p(command + 24);
        int level;
        int name;
        int value = 0;
        socklen_t length = sizeof(value);

        if (option == ASTRA_NETWORK_OPTION_ERROR) {
            value = endpoint->error_status;
            endpoint->error_status = 0;
        } else if (option == ASTRA_NETWORK_OPTION_TYPE) {
            value = endpoint->type;
        } else if (!astra_network_socket_option(option, &level, &name)) {
            status = ASTRA_NETWORK_UNSUPPORTED;
        } else if (getsockopt(endpoint->fd, level, name,
                              (void *)&value, &length) < 0) {
            status = astra_network_status_from_errno(errno);
        }
        stl_be_p(command + 80, value);
        break;
    }
    case ASTRA_NETWORK_HOST_SET_OPTION: {
        uint32_t option = ldl_be_p(command + 24);
        int level;
        int name;
        int value = ldl_be_p(command + 8);

        if (!astra_network_socket_option(option, &level, &name)) {
            status = ASTRA_NETWORK_UNSUPPORTED;
        } else if (setsockopt(endpoint->fd, level, name,
                              (void *)&value, sizeof(value)) < 0) {
            status = astra_network_status_from_errno(errno);
        }
        break;
    }
    case ASTRA_NETWORK_HOST_SHUTDOWN: {
        uint32_t flags = ldl_be_p(command + 8);
        int how = flags == ASTRA_NETWORK_SHUTDOWN_READ ? SHUT_RD :
                  flags == ASTRA_NETWORK_SHUTDOWN_WRITE ? SHUT_WR :
                  flags == (ASTRA_NETWORK_SHUTDOWN_READ |
                            ASTRA_NETWORK_SHUTDOWN_WRITE) ? SHUT_RDWR : -1;

        if (how < 0) {
            status = ASTRA_NETWORK_INVALID;
        } else if (shutdown(endpoint->fd, how) < 0) {
            status = astra_network_status_from_errno(errno);
        }
        break;
    }
    case ASTRA_NETWORK_HOST_ARM:
        if ((ldl_be_p(command + 8) &
             ~(ASTRA_NETWORK_READY_READABLE |
               ASTRA_NETWORK_READY_WRITABLE |
               ASTRA_NETWORK_READY_CONNECTED |
               ASTRA_NETWORK_READY_ACCEPTABLE)) != 0) {
            status = ASTRA_NETWORK_INVALID;
        } else {
            stl_be_p(command + 80,
                     astra_network_arm_endpoint(endpoint,
                                                ldl_be_p(command + 8)));
        }
        break;
    case ASTRA_NETWORK_HOST_CLOSE:
        g_hash_table_remove(s->network.endpoints, GUINT_TO_POINTER(id));
        break;
    default:
        status = ASTRA_NETWORK_UNSUPPORTED;
        break;
    }
    stl_be_p(command + 68, status);
}

static void astra_network_execute(Astra68State *s)
{
    uint8_t *base;

    s->network.completed = 0;
    s->network.status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    base = astra_dma_data(s, s->network.request_buffer,
                          s->network.request_bytes, 0,
                          s->network.request_bytes);
    if (base == NULL || s->network.request_count == 0 ||
        s->network.request_count > s->network.request_bytes /
                                       NETWORK_COMMAND_BYTES) {
        return;
    }
    for (uint32_t index = 0; index < s->network.request_count; ++index) {
        astra_network_execute_command(
            s, base + index * NETWORK_COMMAND_BYTES,
            s->network.request_buffer, s->network.request_bytes);
        ++s->network.completed;
    }
    s->network.status = ASTRA_SYSCALL_OK;
}

#define HOST_FIELD(field) offsetof(AstraHostCommand, field)

static void astra_host_file_free(gpointer opaque)
{
    AstraHostFile *file = opaque;

    if (file != NULL) {
        if (file->directory != NULL) {
            closedir(file->directory);
        } else if (file->fd >= 0) {
            close(file->fd);
        }
        qemu_mutex_destroy(&file->lock);
        g_free(file);
    }
}

static void astra_host_file_release(Astra68State *s, AstraHostFile *file)
{
    bool free_file;

    qemu_mutex_lock(&s->host.files_lock);
    assert(file->references != 0);
    --file->references;
    free_file = file->references == 0;
    qemu_mutex_unlock(&s->host.files_lock);
    if (free_file) {
        astra_host_file_free(file);
    }
}

static void astra_host_release_files(Astra68State *s, uint32_t owner)
{
    GHashTableIter iterator;
    gpointer key;
    gpointer value;
    GPtrArray *released = g_ptr_array_new();

    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.files);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostFile *file = value;

        if (owner != 0 && file->owner != owner) {
            continue;
        }
        file->closing = true;
        g_hash_table_iter_steal(&iterator);
        g_ptr_array_add(released, file);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    for (guint index = 0; index < released->len; ++index) {
        astra_host_file_release(s, g_ptr_array_index(released, index));
    }
    g_ptr_array_free(released, true);
}

static AstraHostFile *astra_host_file_acquire(Astra68State *s,
                                               uint32_t owner,
                                               uint32_t handle)
{
    AstraHostFile *file = NULL;

    if (handle == 0) {
        return NULL;
    }
    qemu_mutex_lock(&s->host.files_lock);
    file = g_hash_table_lookup(s->host.files, GUINT_TO_POINTER(handle));
    if (file == NULL || file->owner != owner || file->closing) {
        file = NULL;
    } else {
        ++file->references;
    }
    qemu_mutex_unlock(&s->host.files_lock);
    return file;
}

static AstraHostFile *astra_host_file_lock(Astra68State *s, uint32_t owner,
                                            uint32_t handle)
{
    AstraHostFile *file = astra_host_file_acquire(s, owner, handle);

    if (file != NULL) {
        qemu_mutex_lock(&file->lock);
    }
    return file;
}

static void astra_host_file_unlock(Astra68State *s, AstraHostFile *file)
{
    qemu_mutex_unlock(&file->lock);
    astra_host_file_release(s, file);
}

static void astra_host_channel_drain(AstraHostChannel *channel)
{
    channel->active = false;
    channel->completion_pending = false;
    while (channel->jobs != 0) {
        aio_poll(qemu_get_aio_context(), true);
    }
    g_free(channel->completed);
    memset(channel, 0, sizeof(*channel));
}

static void astra_host_refresh_completion(Astra68State *s)
{
    s->host.completion_pending = false;
    for (uint32_t slot = 0; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot) {
        s->host.completion_pending |=
            s->host.channels[slot].completion_pending;
    }
    astra_update_irq(s);
}

static void astra_host_release_owner(Astra68State *s, uint32_t owner)
{
    if (owner == 0) {
        return;
    }
    for (uint32_t slot = 0; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot) {
        if (s->host.channels[slot].active &&
            s->host.channels[slot].owner == owner) {
            astra_host_channel_drain(&s->host.channels[slot]);
        }
    }
    astra_host_refresh_completion(s);
    astra_host_release_files(s, owner);
    if (s->host.owner == owner) {
        s->host.owner = 0;
    }
}

static uint32_t astra_host_status_from_errno(int error)
{
    switch (error) {
    case 0: return ASTRA_STATUS_OK;
    case ENOENT: return ASTRA_STATUS_NOT_FOUND;
    case EEXIST: return ASTRA_STATUS_EXISTS;
    case ENOTDIR: return ASTRA_STATUS_NOT_DIR;
    case EISDIR: return ASTRA_STATUS_IS_DIR;
    case EACCES:
    case EPERM: return ASTRA_STATUS_ACCESS;
    case ENOSPC:
#ifdef EDQUOT
    case EDQUOT:
#endif
        return ASTRA_STATUS_NO_SPACE;
    case ENOTEMPTY: return ASTRA_STATUS_NOT_EMPTY;
    case EINVAL:
    case ENAMETOOLONG: return ASTRA_STATUS_INVALID;
    case ELOOP: return ASTRA_STATUS_LOOP;
    case EBUSY: return ASTRA_STATUS_BUSY;
    case EMFILE:
    case ENFILE: return ASTRA_STATUS_LIMIT;
    case EXDEV: return ASTRA_STATUS_CROSS_DEVICE;
#if defined(EOPNOTSUPP) && EOPNOTSUPP != ENOTSUP
    case EOPNOTSUPP:
#endif
    case ENOTSUP: return ASTRA_STATUS_UNSUPPORTED;
    default: return ASTRA_STATUS_IO;
    }
}

static uint64_t astra_host_get64(const uint8_t *command, size_t high)
{
    return ((uint64_t)(uint32_t)ldl_be_p(command + high) << 32) |
           (uint32_t)ldl_be_p(command + high + sizeof(uint32_t));
}

static void astra_host_put64(uint8_t *command, size_t high, uint64_t value)
{
    stl_be_p(command + high, value >> 32);
    stl_be_p(command + high + sizeof(uint32_t), value);
}

static bool astra_host_path_terminated(const uint8_t *path)
{
    return memchr(path, 0, ASTRA_HOST_FS_PATH_MAX) != NULL;
}

static bool astra_host_path_valid(const char *path)
{
    const char *component;

    if (path == NULL || path[0] != '/' ||
        !astra_host_path_terminated((const uint8_t *)path)) {
        errno = EINVAL;
        return false;
    }
    component = path + 1;
    while (component[0] != '\0') {
        const char *slash = strchr(component, '/');
        size_t length = slash == NULL ? strlen(component) :
                                        (size_t)(slash - component);

        if (length == 0 ||
            (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.')) {
            errno = EINVAL;
            return false;
        }
        if (slash == NULL)
            return true;
        component = slash + 1;
    }
    if (component != path + 1 && component[-1] == '/') {
        errno = EINVAL;
        return false;
    }
    return true;
}

static int astra_host_open_beneath(Astra68State *s, const char *path,
                                   int flags, mode_t mode)
{
#ifdef CONFIG_LINUX
    struct open_how how = {
        .flags = (uint64_t)(flags | O_CLOEXEC),
        .mode = (flags & O_CREAT) != 0 ? mode : 0,
        .resolve = RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS |
                   RESOLVE_NO_SYMLINKS,
    };

    return syscall(SYS_openat2, s->host.root_fd, path, &how, sizeof(how));
#else
    errno = ENOSYS;
    return -1;
#endif
}

/*
 * Opens the parent one component at a time beneath the configured root.
 * Intermediate and final traversal never follows a host symlink.  Astra's
 * shared assign layer owns logical symlink resolution, so a host symlink is
 * data to report, not an alternate route around the namespace boundary.
 */
static int astra_host_open_parent_walk(Astra68State *s, const char *path,
                                       int *parent_out,
                                       char leaf[ASTRA_HOST_FS_PATH_MAX])
{
    char copy[ASTRA_HOST_FS_PATH_MAX];
    char *component;
    char *slash;
    int parent;

    if (s->host.root_fd < 0 || !astra_host_path_valid(path)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(copy, path, sizeof(copy));
    if (copy[1] == '\0') {
        leaf[0] = '\0';
        parent = dup(s->host.root_fd);
        if (parent < 0) {
            return -1;
        }
        *parent_out = parent;
        return 0;
    }
    if (copy[1] == '/') {
        errno = EINVAL;
        return -1;
    }
    parent = dup(s->host.root_fd);
    if (parent < 0) {
        return -1;
    }
    component = copy + 1;
    for (;;) {
        int next;

        slash = strchr(component, '/');
        if (slash != NULL) {
            *slash = '\0';
        }
        if (component[0] == '\0' || strcmp(component, ".") == 0 ||
            strcmp(component, "..") == 0) {
            close(parent);
            errno = EINVAL;
            return -1;
        }
        if (slash == NULL) {
            memcpy(leaf, component, strlen(component) + 1u);
            *parent_out = parent;
            return 0;
        }
        next = openat(parent, component,
                      O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (next < 0) {
            int saved = errno;
            struct stat st;

            if (saved == ENOTDIR &&
                fstatat(parent, component, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISLNK(st.st_mode)) {
                saved = ELOOP;
            }
            close(parent);
            errno = saved;
            return -1;
        }
        close(parent);
        parent = next;
        component = slash + 1;
    }
}

static int astra_host_open_parent(Astra68State *s, const char *path,
                                  int *parent_out,
                                  char leaf[ASTRA_HOST_FS_PATH_MAX])
{
    char copy[ASTRA_HOST_FS_PATH_MAX];
    char *slash;
    int parent;

    if (s->host.root_fd < 0 || parent_out == NULL || leaf == NULL ||
        !astra_host_path_valid(path)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(copy, path, sizeof(copy));
    slash = strrchr(copy, '/');
    assert(slash != NULL);
    memcpy(leaf, slash + 1, strlen(slash + 1) + 1u);
    if (slash == copy) {
        parent = dup(s->host.root_fd);
    } else {
        *slash = '\0';
        parent = astra_host_open_beneath(
            s, copy + 1, O_RDONLY | O_DIRECTORY, 0);
        if (parent < 0 && errno == ENOSYS)
            return astra_host_open_parent_walk(s, path, parent_out, leaf);
    }
    if (parent < 0)
        return -1;
    *parent_out = parent;
    return 0;
}

static int astra_host_open_path(Astra68State *s, const char *path, int flags,
                                mode_t mode)
{
    char leaf[ASTRA_HOST_FS_PATH_MAX];
    int parent;
    int fd;

    if (s->host.root_fd < 0 || !astra_host_path_valid(path)) {
        return -1;
    }
    if (path[1] == '\0') {
        if ((flags & (O_CREAT | O_TRUNC | O_WRONLY | O_RDWR)) != 0) {
            errno = EISDIR;
            return -1;
        }
        return dup(s->host.root_fd);
    }
    fd = astra_host_open_beneath(s, path + 1, flags | O_NOFOLLOW, mode);
    if (fd >= 0 || errno != ENOSYS)
        return fd;
    if (astra_host_open_parent_walk(s, path, &parent, leaf) < 0)
        return -1;
    fd = openat(parent, leaf, flags | O_NOFOLLOW | O_CLOEXEC, mode);
    close(parent);
    return fd;
}

static int astra_host_parent_pair(Astra68State *s, const char *left,
                                  const char *right, int *left_parent,
                                  char left_leaf[ASTRA_HOST_FS_PATH_MAX],
                                  int *right_parent,
                                  char right_leaf[ASTRA_HOST_FS_PATH_MAX])
{
    if (astra_host_open_parent(s, left, left_parent, left_leaf) < 0) {
        return -1;
    }
    if (astra_host_open_parent(s, right, right_parent, right_leaf) < 0) {
        close(*left_parent);
        return -1;
    }
    if (left_leaf[0] == '\0' || right_leaf[0] == '\0') {
        close(*left_parent);
        close(*right_parent);
        errno = EACCES;
        return -1;
    }
    return 0;
}

static void astra_host_publish_stat(uint8_t *command, const struct stat *st)
{
    uint16_t kind = ASTRA_VFS_KIND_UNKNOWN;

    if (S_ISREG(st->st_mode)) {
        kind = ASTRA_VFS_KIND_FILE;
    } else if (S_ISDIR(st->st_mode)) {
        kind = ASTRA_VFS_KIND_DIRECTORY;
    } else if (S_ISLNK(st->st_mode)) {
        kind = ASTRA_VFS_KIND_SYMLINK;
    }
    astra_host_put64(command, HOST_FIELD(node_size_hi), st->st_size);
    astra_host_put64(command, HOST_FIELD(mtime_hi), st->st_mtime);
    stl_be_p(command + HOST_FIELD(uid), st->st_uid);
    stl_be_p(command + HOST_FIELD(gid), st->st_gid);
    stw_be_p(command + HOST_FIELD(kind), kind);
    stw_be_p(command + HOST_FIELD(mode), st->st_mode);
    stw_be_p(command + HOST_FIELD(nlink), st->st_nlink);
}

static uint32_t astra_host_insert_file(Astra68State *s, uint32_t owner, int fd,
                                       DIR *directory, uint16_t flags)
{
    AstraHostFile *file;
    uint32_t handle;

    file = g_new0(AstraHostFile, 1);
    file->fd = fd;
    file->directory = directory;
    file->owner = owner;
    file->references = 1;
    file->flags = flags;
    qemu_mutex_init(&file->lock);
    qemu_mutex_lock(&s->host.files_lock);
    do {
        astra_network_next(&s->host.next_handle);
        handle = s->host.next_handle;
    } while (g_hash_table_contains(s->host.files, GUINT_TO_POINTER(handle)));
    g_hash_table_insert(s->host.files, GUINT_TO_POINTER(handle), file);
    qemu_mutex_unlock(&s->host.files_lock);
    return handle;
}

static bool astra_host_close_file(Astra68State *s, uint32_t owner,
                                  uint32_t handle)
{
    AstraHostFile *file;

    qemu_mutex_lock(&s->host.files_lock);
    file = g_hash_table_lookup(s->host.files, GUINT_TO_POINTER(handle));
    if (file == NULL || file->owner != owner || file->closing) {
        qemu_mutex_unlock(&s->host.files_lock);
        return false;
    }
    file->closing = true;
    g_hash_table_steal(s->host.files, GUINT_TO_POINTER(handle));
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_file_release(s, file);
    return true;
}

static uint8_t *astra_host_command_data(Astra68State *s, uint32_t physical,
                                        uint32_t bytes, uint32_t command_bytes,
                                        uint8_t *command, uint32_t amount)
{
    uint32_t offset = ldl_be_p(command + HOST_FIELD(data_offset));

    if (offset < command_bytes) {
        return NULL;
    }
    return astra_dma_data(s, physical, bytes, offset, amount);
}

static uint32_t astra_host_stat_path(Astra68State *s, const char *path,
                                     struct stat *st)
{
    char leaf[ASTRA_HOST_FS_PATH_MAX];
    int parent;
    int rc;

    if (astra_host_open_parent(s, path, &parent, leaf) < 0) {
        return astra_host_status_from_errno(errno);
    }
    rc = leaf[0] == '\0' ? fstat(parent, st) :
         fstatat(parent, leaf, st, AT_SYMLINK_NOFOLLOW);
    close(parent);
    return rc == 0 ? ASTRA_STATUS_OK : astra_host_status_from_errno(errno);
}

static void astra_host_execute_fs(Astra68State *s, uint32_t owner,
                                  uint8_t *command,
                                  uint32_t physical, uint32_t bytes,
                                  uint32_t command_bytes,
                                  uint32_t expected_generation)
{
    const char *path = (const char *)(command + HOST_FIELD(path));
    const char *path2 = (const char *)(command + HOST_FIELD(path2));
    uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
    uint16_t flags = lduw_be_p(command + HOST_FIELD(flags));
    uint32_t handle = ldl_be_p(command + HOST_FIELD(handle));
    AstraHostFile *file;
    uint32_t status = ASTRA_STATUS_INVALID;
    struct stat st;

    memset(command + HOST_FIELD(status), 0, sizeof(uint32_t));
    memset(command + HOST_FIELD(result_length), 0,
           HOST_FIELD(path) - HOST_FIELD(result_length));
    if (ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) !=
            ASTRA_HOST_COMMAND_VERSION ||
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_FILESYSTEM ||
        ldl_be_p(command + HOST_FIELD(generation)) != expected_generation) {
        qatomic_inc(&s->host.operation_counts[0]);
        goto done;
    }
    qatomic_inc(&s->host.operation_counts[
        operation <= ASTRA_HOST_FS_SYMLINK ? operation : 0u]);

    switch (operation) {
    case ASTRA_HOST_FS_OPEN: {
        uint32_t allowed = ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE |
                           ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_TRUNCATE |
                           ASTRA_VFS_OPEN_DIRECTORY |
                           ASTRA_VFS_OPEN_EXCLUSIVE | ASTRA_VFS_OPEN_APPEND;
        int native_flags;
        int fd;
        DIR *directory = NULL;

        if (!astra_host_path_terminated((const uint8_t *)path) ||
            (flags & ~allowed) != 0 ||
            (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0) {
            break;
        }
        native_flags = (flags & ASTRA_VFS_OPEN_WRITE) == 0 ? O_RDONLY :
                       (flags & ASTRA_VFS_OPEN_READ) != 0 ? O_RDWR : O_WRONLY;
        if ((flags & ASTRA_VFS_OPEN_CREATE) != 0) native_flags |= O_CREAT;
        if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0) native_flags |= O_TRUNC;
        if ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0) native_flags |= O_EXCL;
        if ((flags & ASTRA_VFS_OPEN_APPEND) != 0) native_flags |= O_APPEND;
        if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0)
            native_flags |= O_DIRECTORY;
        fd = astra_host_open_path(s, path, native_flags,
                                  ldl_be_p(command + HOST_FIELD(value_lo)) &
                                      07777u);
        if (fd < 0) {
            status = astra_host_status_from_errno(errno);
            break;
        }
        if (fstat(fd, &st) < 0) {
            status = astra_host_status_from_errno(errno);
            close(fd);
            break;
        }
        if ((flags & ASTRA_VFS_OPEN_DIRECTORY) == 0 && S_ISDIR(st.st_mode)) {
            close(fd);
            status = ASTRA_STATUS_IS_DIR;
            break;
        }
        if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0) {
            directory = fdopendir(fd);
            if (directory == NULL) {
                status = astra_host_status_from_errno(errno);
                close(fd);
                break;
            }
        }
        handle = astra_host_insert_file(s, owner, fd, directory, flags);
        stl_be_p(command + HOST_FIELD(handle), handle);
        astra_host_publish_stat(command, &st);
        status = ASTRA_STATUS_OK;
        break;
    }
    case ASTRA_HOST_FS_CLOSE:
        status = astra_host_close_file(s, owner, handle) ?
                 ASTRA_STATUS_OK : ASTRA_STATUS_BAD_HANDLE;
        break;
    case ASTRA_HOST_FS_READ: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);
        ssize_t moved;

        file = astra_host_file_lock(s, owner, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
        } else if ((file->flags & ASTRA_VFS_OPEN_READ) == 0) {
            status = ASTRA_STATUS_ACCESS;
        } else if (file->directory != NULL) {
            status = ASTRA_STATUS_IS_DIR;
        } else if (data == NULL) {
            status = ASTRA_STATUS_INVALID;
        } else {
            moved = pread(file->fd, data, capacity,
                          astra_host_get64(command, HOST_FIELD(offset_hi)));
            if (moved < 0) {
                status = astra_host_status_from_errno(errno);
            } else {
                stl_be_p(command + HOST_FIELD(result_length), moved);
                status = ASTRA_STATUS_OK;
            }
        }
        if (file != NULL) {
            astra_host_file_unlock(s, file);
        }
        break;
    }
    case ASTRA_HOST_FS_WRITE: {
        uint32_t length = ldl_be_p(command + HOST_FIELD(data_length));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, length);
        ssize_t moved;
        off_t position;

        file = astra_host_file_lock(s, owner, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
        } else if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0) {
            status = ASTRA_STATUS_ACCESS;
        } else if (file->directory != NULL) {
            status = ASTRA_STATUS_IS_DIR;
        } else if (data == NULL) {
            status = ASTRA_STATUS_INVALID;
        } else {
            moved = (file->flags & ASTRA_VFS_OPEN_APPEND) != 0 ?
                    write(file->fd, data, length) :
                    pwrite(file->fd, data, length,
                           astra_host_get64(command, HOST_FIELD(offset_hi)));
            if (moved < 0) {
                status = astra_host_status_from_errno(errno);
            } else {
                position = (file->flags & ASTRA_VFS_OPEN_APPEND) != 0 ?
                           lseek(file->fd, 0, SEEK_CUR) :
                           astra_host_get64(command, HOST_FIELD(offset_hi)) +
                               moved;
                if (position < 0) {
                    status = astra_host_status_from_errno(errno);
                } else {
                    stl_be_p(command + HOST_FIELD(result_length), moved);
                    astra_host_put64(command, HOST_FIELD(value_hi), position);
                    status = ASTRA_STATUS_OK;
                }
            }
        }
        if (file != NULL) {
            astra_host_file_unlock(s, file);
        }
        break;
    }
    case ASTRA_HOST_FS_SYNC:
        file = astra_host_file_lock(s, owner, handle);
        status = file == NULL ? ASTRA_STATUS_BAD_HANDLE :
                 fsync(file->fd) == 0 ? ASTRA_STATUS_OK :
                 astra_host_status_from_errno(errno);
        if (file != NULL) {
            astra_host_file_unlock(s, file);
        }
        break;
    case ASTRA_HOST_FS_TRUNCATE:
        file = astra_host_file_lock(s, owner, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
        } else if ((file->flags & ASTRA_VFS_OPEN_WRITE) == 0) {
            status = ASTRA_STATUS_ACCESS;
        } else if (file->directory != NULL) {
            status = ASTRA_STATUS_IS_DIR;
        } else {
            status = ftruncate(
                file->fd, astra_host_get64(command, HOST_FIELD(value_hi))) == 0 ?
                ASTRA_STATUS_OK : astra_host_status_from_errno(errno);
        }
        if (file != NULL) {
            astra_host_file_unlock(s, file);
        }
        break;
    case ASTRA_HOST_FS_STAT:
        if (!astra_host_path_terminated((const uint8_t *)path)) {
            break;
        }
        status = astra_host_stat_path(s, path, &st);
        if (status == ASTRA_STATUS_OK) astra_host_publish_stat(command, &st);
        break;
    case ASTRA_HOST_FS_READDIR: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);
        uint64_t cookie = astra_host_get64(command, HOST_FIELD(offset_hi));
        struct dirent *entry;
        DIR *directory;
        int directory_fd;
        bool persistent = false;

        if (!astra_host_path_terminated((const uint8_t *)path) ||
            data == NULL || capacity == 0 || cookie > LONG_MAX) {
            break;
        }
        file = handle != 0 ? astra_host_file_lock(s, owner, handle) : NULL;
        if (handle != 0) {
            if (file == NULL || file->directory == NULL) {
                status = ASTRA_STATUS_BAD_HANDLE;
                if (file != NULL) {
                    astra_host_file_unlock(s, file);
                }
                break;
            }
            directory = file->directory;
            persistent = true;
            if (cookie == 0) {
                rewinddir(directory);
                file->directory_cookie = 0;
            } else if (cookie != file->directory_cookie) {
                seekdir(directory, (long)cookie);
                file->directory_cookie = cookie;
            }
        } else {
            directory_fd = astra_host_open_path(
                s, path, O_RDONLY | O_DIRECTORY, 0);
            if (directory_fd < 0) {
                status = astra_host_status_from_errno(errno);
                break;
            }
            directory = fdopendir(directory_fd);
            if (directory == NULL) {
                status = astra_host_status_from_errno(errno);
                close(directory_fd);
                break;
            }
            if (cookie != 0)
                seekdir(directory, (long)cookie);
        }
        errno = 0;
        do {
            entry = readdir(directory);
        } while (entry != NULL &&
                 (strcmp(entry->d_name, ".") == 0 ||
                  strcmp(entry->d_name, "..") == 0));
        if (entry == NULL) {
            status = errno == 0 ? ASTRA_STATUS_NOT_FOUND :
                     astra_host_status_from_errno(errno);
            if (persistent) {
                astra_host_file_unlock(s, file);
            } else {
                closedir(directory);
            }
            break;
        }
        if (strlen(entry->d_name) + 1u > capacity) {
            status = ASTRA_STATUS_BUFFER_TOO_SMALL;
            if (persistent) {
                astra_host_file_unlock(s, file);
            } else {
                closedir(directory);
            }
            break;
        }
        memcpy(data, entry->d_name, strlen(entry->d_name) + 1u);
        stl_be_p(command + HOST_FIELD(result_length), strlen(entry->d_name));
        {
            long position = telldir(directory);
            uint64_t next;

            if (position < 0) {
                status = astra_host_status_from_errno(errno);
                if (persistent) {
                    astra_host_file_unlock(s, file);
                } else {
                    closedir(directory);
                }
                break;
            }
            next = (uint64_t)position;
            astra_host_put64(command, HOST_FIELD(value_hi), next);
            if (persistent)
                file->directory_cookie = next;
        }
        if (fstatat(dirfd(directory), entry->d_name, &st,
                    AT_SYMLINK_NOFOLLOW) < 0) {
            status = astra_host_status_from_errno(errno);
        } else {
            astra_host_publish_stat(command, &st);
            status = ASTRA_STATUS_OK;
        }
        if (persistent) {
            astra_host_file_unlock(s, file);
        } else {
            closedir(directory);
        }
        break;
    }
    case ASTRA_HOST_FS_MKDIR: {
        char leaf[ASTRA_HOST_FS_PATH_MAX];
        int parent;

        if (!astra_host_path_terminated((const uint8_t *)path) ||
            astra_host_open_parent(s, path, &parent, leaf) < 0) {
            status = astra_host_status_from_errno(errno);
        } else if (leaf[0] == '\0') {
            status = ASTRA_STATUS_EXISTS;
            close(parent);
        } else {
            status = mkdirat(parent, leaf,
                             ldl_be_p(command + HOST_FIELD(value_lo)) &
                                 07777u) == 0 ?
                     ASTRA_STATUS_OK : astra_host_status_from_errno(errno);
            close(parent);
        }
        break;
    }
    case ASTRA_HOST_FS_UNLINK: {
        char leaf[ASTRA_HOST_FS_PATH_MAX];
        int parent;

        if (!astra_host_path_terminated((const uint8_t *)path) ||
            astra_host_open_parent(s, path, &parent, leaf) < 0) {
            status = astra_host_status_from_errno(errno);
        } else if (leaf[0] == '\0') {
            status = ASTRA_STATUS_ACCESS;
            close(parent);
        } else if (fstatat(parent, leaf, &st, AT_SYMLINK_NOFOLLOW) < 0) {
            status = astra_host_status_from_errno(errno);
            close(parent);
        } else {
            status = unlinkat(parent, leaf,
                              S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0) == 0 ?
                     ASTRA_STATUS_OK : astra_host_status_from_errno(errno);
            close(parent);
        }
        break;
    }
    case ASTRA_HOST_FS_RENAME: {
        char left_leaf[ASTRA_HOST_FS_PATH_MAX];
        char right_leaf[ASTRA_HOST_FS_PATH_MAX];
        int left_parent;
        int right_parent;

        if (!astra_host_path_terminated((const uint8_t *)path) ||
            !astra_host_path_terminated((const uint8_t *)path2) ||
            astra_host_parent_pair(s, path, path2, &left_parent, left_leaf,
                                   &right_parent, right_leaf) < 0) {
            status = astra_host_status_from_errno(errno);
        } else {
            status = renameat(left_parent, left_leaf,
                              right_parent, right_leaf) == 0 ?
                     ASTRA_STATUS_OK : astra_host_status_from_errno(errno);
            close(left_parent);
            close(right_parent);
        }
        break;
    }
    case ASTRA_HOST_FS_CHMOD: {
        int fd;

        if (!astra_host_path_terminated((const uint8_t *)path)) break;
        fd = astra_host_open_path(s, path, O_RDONLY, 0);
        if (fd < 0) {
            status = astra_host_status_from_errno(errno);
        } else {
            status = fchmod(fd, ldl_be_p(command + HOST_FIELD(value_lo)) &
                                 07777u) == 0 ?
                     ASTRA_STATUS_OK : astra_host_status_from_errno(errno);
            close(fd);
        }
        break;
    }
    case ASTRA_HOST_FS_READLINK: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);
        char leaf[ASTRA_HOST_FS_PATH_MAX];
        char *target;
        int parent;
        ssize_t length;

        if (!astra_host_path_terminated((const uint8_t *)path) ||
            data == NULL || capacity == 0 ||
            astra_host_open_parent(s, path, &parent, leaf) < 0) {
            status = astra_host_status_from_errno(errno);
        } else if (leaf[0] == '\0') {
            status = ASTRA_STATUS_INVALID;
            close(parent);
        } else {
            target = g_malloc((size_t)capacity + 1u);
            length = readlinkat(parent, leaf, target, (size_t)capacity + 1u);
            status = length < 0 ? astra_host_status_from_errno(errno) :
                     (uint32_t)length > capacity ?
                         ASTRA_STATUS_BUFFER_TOO_SMALL : ASTRA_STATUS_OK;
            if (length >= 0 && (uint32_t)length <= capacity) {
                memcpy(data, target, length);
                stl_be_p(command + HOST_FIELD(result_length), length);
            }
            g_free(target);
            close(parent);
        }
        break;
    }
    case ASTRA_HOST_FS_SYMLINK: {
        char leaf[ASTRA_HOST_FS_PATH_MAX];
        int parent;

        if (!astra_host_path_terminated((const uint8_t *)path) ||
            !astra_host_path_terminated((const uint8_t *)path2) ||
            astra_host_open_parent(s, path2, &parent, leaf) < 0) {
            status = astra_host_status_from_errno(errno);
        } else if (leaf[0] == '\0') {
            status = ASTRA_STATUS_EXISTS;
            close(parent);
        } else {
            status = symlinkat(path, parent, leaf) == 0 ? ASTRA_STATUS_OK :
                     astra_host_status_from_errno(errno);
            close(parent);
        }
        break;
    }
    default:
        status = ASTRA_STATUS_UNSUPPORTED;
        break;
    }

done:
    stl_be_p(command + HOST_FIELD(status), status);
}

static void astra_host_reset(Astra68State *s)
{
    for (uint32_t slot = 0; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot) {
        astra_host_channel_drain(&s->host.channels[slot]);
    }
    astra_host_refresh_completion(s);
    if (s->host.files != NULL) {
        astra_host_release_files(s, 0);
    }
    astra_network_next(&s->host.generation);
    s->host.request_buffer = 0;
    s->host.request_bytes = 0;
    s->host.request_count = 0;
    s->host.owner = 0;
    s->host.status = ASTRA_SYSCALL_OK;
    s->host.completed = 0;
    s->host.channel_result = ASTRA_SYSCALL_OK;
    s->host.completion_pending = false;
}

static void astra_host_execute(Astra68State *s)
{
    uint8_t *base;
    uint32_t command_bytes;
    uint64_t started;

    s->host.completed = 0;
    s->host.status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (s->host.root_fd < 0 || s->host.owner == 0 ||
        s->host.request_count == 0 ||
        s->host.request_count > UINT32_MAX / ASTRA_HOST_COMMAND_SIZE) {
        return;
    }
    command_bytes = s->host.request_count * ASTRA_HOST_COMMAND_SIZE;
    if (command_bytes > s->host.request_bytes) {
        return;
    }
    base = astra_dma_data(s, s->host.request_buffer, s->host.request_bytes,
                          0, s->host.request_bytes);
    if (base == NULL) {
        return;
    }
    started = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    ++s->host.submissions;
    for (uint32_t index = 0; index < s->host.request_count; ++index) {
        astra_host_execute_fs(s, s->host.owner,
                              base + index * ASTRA_HOST_COMMAND_SIZE,
                              s->host.request_buffer, s->host.request_bytes,
                              command_bytes, s->host.generation);
        ++s->host.completed;
    }
    s->host.commands += s->host.completed;
    s->host.execution_ns += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - started;
    s->host.status = ASTRA_SYSCALL_OK;
}

#define HOST_SUBMISSION_FIELD(field) offsetof(AstraHostSubmission, field)

static void astra_host_submit(Astra68State *s, uint32_t physical)
{
    uint8_t *submission;

    s->host.completed = 0;
    s->host.status = ASTRA_SYSCALL_INVALID_ARGUMENT;
    if ((physical & 63u) != 0) {
        return;
    }
    submission = astra_dma_data(s, physical, ASTRA_HOST_SUBMISSION_SIZE, 0,
                                ASTRA_HOST_SUBMISSION_SIZE);
    if (submission == NULL ||
        ldl_be_p(submission + HOST_SUBMISSION_FIELD(size)) !=
            ASTRA_HOST_SUBMISSION_SIZE ||
        lduw_be_p(submission + HOST_SUBMISSION_FIELD(version)) !=
            ASTRA_HOST_SUBMISSION_VERSION ||
        lduw_be_p(submission + HOST_SUBMISSION_FIELD(flags)) != 0) {
        return;
    }
    for (size_t index = 0;
         index < sizeof(((AstraHostSubmission *)0)->reserved); ++index) {
        if (submission[HOST_SUBMISSION_FIELD(reserved) + index] != 0) {
            return;
        }
    }
    if (ldl_be_p(submission + HOST_SUBMISSION_FIELD(host_generation)) !=
        s->host.generation) {
        s->host.status = ASTRA_SYSCALL_PEER_DEAD;
        return;
    }
    s->host.owner = ldl_be_p(submission + HOST_SUBMISSION_FIELD(owner));
    s->host.request_buffer = ldl_be_p(
        submission + HOST_SUBMISSION_FIELD(physical_buffer));
    s->host.request_bytes = ldl_be_p(
        submission + HOST_SUBMISSION_FIELD(byte_size));
    s->host.request_count = ldl_be_p(
        submission + HOST_SUBMISSION_FIELD(command_count));
    astra_host_execute(s);
}

#define HOST_CHANNEL_CONFIG_FIELD(field) \
    offsetof(AstraHostChannelConfig, field)
#define HOST_CHANNEL_HEADER_FIELD(field) \
    offsetof(AstraHostChannelHeader, field)

static void astra_host_channel_configure(Astra68State *s, uint32_t physical)
{
    AstraHostChannel *channel;
    uint8_t *config;
    uint8_t *header;
    uint32_t slot;
    uint32_t owner;
    uint32_t host_generation;
    uint32_t channel_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t data_offset;
    uint16_t operation;

    s->host.channel_result = ASTRA_SYSCALL_INVALID_ARGUMENT;
    if ((physical & 63u) != 0) {
        return;
    }
    config = astra_dma_data(s, physical, ASTRA_HOST_CHANNEL_CONFIG_SIZE, 0,
                            ASTRA_HOST_CHANNEL_CONFIG_SIZE);
    if (config == NULL ||
        ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(size)) !=
            ASTRA_HOST_CHANNEL_CONFIG_SIZE ||
        lduw_be_p(config + HOST_CHANNEL_CONFIG_FIELD(version)) !=
            ASTRA_HOST_CHANNEL_CONFIG_VERSION) {
        return;
    }
    for (size_t index = 0;
         index < sizeof(((AstraHostChannelConfig *)0)->reserved); ++index) {
        if (config[HOST_CHANNEL_CONFIG_FIELD(reserved) + index] != 0) {
            return;
        }
    }
    operation = lduw_be_p(config + HOST_CHANNEL_CONFIG_FIELD(operation));
    slot = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(slot));
    owner = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(owner));
    host_generation = ldl_be_p(
        config + HOST_CHANNEL_CONFIG_FIELD(host_generation));
    channel_generation = ldl_be_p(
        config + HOST_CHANNEL_CONFIG_FIELD(channel_generation));
    physical_buffer = ldl_be_p(
        config + HOST_CHANNEL_CONFIG_FIELD(physical_buffer));
    byte_size = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(byte_size));
    command_capacity = ldl_be_p(
        config + HOST_CHANNEL_CONFIG_FIELD(command_capacity));
    if (slot >= ASTRA_HOST_CHANNEL_COUNT || owner == 0 ||
        channel_generation == 0) {
        return;
    }
    channel = &s->host.channels[slot];
    if (operation == ASTRA_HOST_CHANNEL_CONFIG_CLOSE) {
        if (physical_buffer != 0 || byte_size != 0 || command_capacity != 0 ||
            !channel->active || channel->owner != owner ||
            channel->host_generation != host_generation ||
            channel->channel_generation != channel_generation) {
            s->host.channel_result = channel->active ?
                ASTRA_SYSCALL_ACCESS_DENIED : ASTRA_SYSCALL_PEER_DEAD;
            return;
        }
        astra_host_channel_drain(channel);
        astra_host_refresh_completion(s);
        s->host.channel_result = ASTRA_SYSCALL_OK;
        return;
    }
    if (operation != ASTRA_HOST_CHANNEL_CONFIG_OPEN || channel->active ||
        channel->jobs != 0 || channel->completed != NULL ||
        host_generation != s->host.generation ||
        (physical_buffer & (ASTRA_ABI_ALIGNMENT - 1u)) != 0 ||
        byte_size > HOST_ACCEL_MAX_TRANSFER || command_capacity == 0 ||
        (command_capacity & (command_capacity - 1u)) != 0 ||
        command_capacity > HOST_ACCEL_MAX_TRANSFER / ASTRA_HOST_COMMAND_SIZE ||
        command_capacity >
            (UINT32_MAX - ASTRA_HOST_CHANNEL_HEADER_SIZE) /
                ASTRA_HOST_COMMAND_SIZE) {
        return;
    }
    data_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                  command_capacity * ASTRA_HOST_COMMAND_SIZE;
    if (byte_size <= data_offset) {
        return;
    }
    header = astra_dma_data(s, physical_buffer, byte_size, 0,
                            ASTRA_HOST_CHANNEL_HEADER_SIZE);
    if (header == NULL) {
        return;
    }
    memset(header, 0, ASTRA_HOST_CHANNEL_HEADER_SIZE);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(magic),
             ASTRA_HOST_CHANNEL_MAGIC);
    stw_be_p(header + HOST_CHANNEL_HEADER_FIELD(version),
             ASTRA_HOST_CHANNEL_VERSION);
    stw_be_p(header + HOST_CHANNEL_HEADER_FIELD(header_size),
             ASTRA_HOST_CHANNEL_HEADER_SIZE);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(command_size),
             ASTRA_HOST_COMMAND_SIZE);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(command_capacity),
             command_capacity);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(command_offset),
             ASTRA_HOST_CHANNEL_HEADER_SIZE);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(data_offset), data_offset);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(total_size), byte_size);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(channel_generation),
             channel_generation);
    stl_be_p(header + HOST_CHANNEL_HEADER_FIELD(transport_status),
             ASTRA_SYSCALL_OK);
    channel->owner = owner;
    channel->host_generation = host_generation;
    channel->channel_generation = channel_generation;
    channel->physical_buffer = physical_buffer;
    channel->byte_size = byte_size;
    channel->command_capacity = command_capacity;
    channel->consumer_position = 0;
    channel->submitted_position = 0;
    channel->status = ASTRA_SYSCALL_OK;
    channel->completed = g_new0(uint8_t, command_capacity);
    channel->active = true;
    s->host.channel_result = ASTRA_SYSCALL_OK;
}

static bool astra_host_channel_header_valid(const AstraHostChannel *channel,
                                            const uint8_t *header)
{
    uint32_t data_offset = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                           channel->command_capacity *
                               ASTRA_HOST_COMMAND_SIZE;

    if (ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(magic)) !=
            ASTRA_HOST_CHANNEL_MAGIC ||
        lduw_be_p(header + HOST_CHANNEL_HEADER_FIELD(version)) !=
            ASTRA_HOST_CHANNEL_VERSION ||
        lduw_be_p(header + HOST_CHANNEL_HEADER_FIELD(header_size)) !=
            ASTRA_HOST_CHANNEL_HEADER_SIZE ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(flags)) != 0 ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(command_size)) !=
            ASTRA_HOST_COMMAND_SIZE ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(command_capacity)) !=
            channel->command_capacity ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(command_offset)) !=
            ASTRA_HOST_CHANNEL_HEADER_SIZE ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(data_offset)) !=
            data_offset ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(total_size)) !=
            channel->byte_size ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(channel_generation)) !=
            channel->channel_generation ||
        ldl_be_p(header + HOST_CHANNEL_HEADER_FIELD(consumer_position)) !=
            channel->consumer_position) {
        return false;
    }
    for (size_t index = 0;
         index < sizeof(((AstraHostChannelHeader *)0)->reserved0); ++index) {
        if (header[HOST_CHANNEL_HEADER_FIELD(reserved0) + index] != 0) {
            return false;
        }
    }
    for (size_t index = 0;
         index < sizeof(((AstraHostChannelHeader *)0)->reserved1); ++index) {
        if (header[HOST_CHANNEL_HEADER_FIELD(reserved1) + index] != 0) {
            return false;
        }
    }
    return true;
}

static int astra_host_channel_worker(void *opaque)
{
    AstraHostJob *job = opaque;
    Astra68State *s = job->machine;
    uint8_t *base = astra_dma_data(s, job->physical_buffer, job->byte_size,
                                   0, job->byte_size);
    uint32_t command_bytes = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                             job->command_capacity * ASTRA_HOST_COMMAND_SIZE;

    if (base == NULL) {
        return -EFAULT;
    }
    astra_host_execute_fs(
        s, job->owner, base + ASTRA_HOST_CHANNEL_HEADER_SIZE +
            (job->position & (job->command_capacity - 1u)) *
                ASTRA_HOST_COMMAND_SIZE,
        job->physical_buffer, job->byte_size, command_bytes,
        job->host_generation);
    return 0;
}

static void astra_host_channel_publish_completion(
    Astra68State *s, AstraHostChannel *channel, uint8_t *base,
    uint32_t position, int ret)
{
    uint32_t old_consumer;

    if (ret != 0) {
        channel->status = ASTRA_SYSCALL_IO_ERROR;
    } else {
        ++s->host.commands;
    }
    channel->completed[position & (channel->command_capacity - 1u)] = 1;
    old_consumer = channel->consumer_position;
    while (channel->consumer_position != channel->submitted_position &&
           channel->completed[channel->consumer_position &
                              (channel->command_capacity - 1u)] != 0) {
        channel->completed[channel->consumer_position &
                           (channel->command_capacity - 1u)] = 0;
        ++channel->consumer_position;
    }
    if (base != NULL) {
        stl_be_p(base + HOST_CHANNEL_HEADER_FIELD(consumer_position),
                 channel->consumer_position);
        stl_be_p(base + HOST_CHANNEL_HEADER_FIELD(transport_status),
                 channel->status);
    }
    if ((channel->consumer_position != old_consumer || ret != 0) &&
        channel->interrupt_armed &&
        (ret != 0 ||
         (int32_t)(channel->consumer_position -
                   channel->interrupt_position) >= 0)) {
        channel->interrupt_armed = false;
        channel->completion_pending = true;
        s->host.completion_pending = true;
        astra_update_irq(s);
    }
}

static void astra_host_channel_complete(void *opaque, int ret)
{
    AstraHostJob *job = opaque;
    Astra68State *s = job->machine;
    AstraHostChannel *channel = &s->host.channels[job->slot];
    uint8_t *base;

    assert(channel->jobs != 0);
    --channel->jobs;
    assert(s->host.inflight != 0);
    --s->host.inflight;
    if (!channel->active ||
        channel->owner != job->owner ||
        channel->host_generation != job->host_generation ||
        channel->channel_generation != job->channel_generation) {
        g_free(job);
        return;
    }
    base = astra_dma_data(s, channel->physical_buffer, channel->byte_size, 0,
                          channel->byte_size);
    s->host.execution_ns += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) -
                            job->started_ns;
    astra_host_channel_publish_completion(s, channel, base, job->position,
                                          ret);
    g_free(job);
}

static void astra_host_channel_arm(Astra68State *s, uint32_t slot,
                                   uint32_t consumer)
{
    AstraHostChannel *channel = &s->host.channels[slot];

    if (!channel->active) {
        return;
    }
    channel->interrupt_position = consumer;
    channel->interrupt_armed = true;
    if (channel->status != ASTRA_SYSCALL_OK ||
        (int32_t)(channel->consumer_position - consumer) >= 0) {
        channel->interrupt_armed = false;
        channel->completion_pending = true;
        s->host.completion_pending = true;
        astra_update_irq(s);
    }
}

static void astra_host_channel_disarm(Astra68State *s, uint32_t slot)
{
    AstraHostChannel *channel = &s->host.channels[slot];

    channel->interrupt_armed = false;
    channel->completion_pending = false;
    astra_host_refresh_completion(s);
}

typedef struct AstraHostDataRange {
    uint32_t start;
    uint32_t end;
    bool writable;
} AstraHostDataRange;

static int astra_host_data_range_compare(const void *left,
                                         const void *right)
{
    const AstraHostDataRange *a = left;
    const AstraHostDataRange *b = right;

    if (a->start != b->start) {
        return a->start < b->start ? -1 : 1;
    }
    if (a->end != b->end) {
        return a->end < b->end ? -1 : 1;
    }
    return 0;
}

static bool astra_host_channel_data_safe(const AstraHostChannel *channel,
                                         const uint8_t *base,
                                         uint32_t producer)
{
    uint32_t command_bytes = ASTRA_HOST_CHANNEL_HEADER_SIZE +
                             channel->command_capacity *
                                 ASTRA_HOST_COMMAND_SIZE;
    uint32_t outstanding = producer - channel->consumer_position;
    AstraHostDataRange *ranges;
    uint32_t range_count = 0;
    uint32_t maximum_end = 0;
    uint32_t maximum_writable_end = 0;
    bool safe = true;

    if (outstanding < 2) {
        return true;
    }
    ranges = g_new(AstraHostDataRange, outstanding);

    for (uint32_t position = channel->consumer_position;
         position != producer; ++position) {
        const uint8_t *command = base + ASTRA_HOST_CHANNEL_HEADER_SIZE +
            (position & (channel->command_capacity - 1u)) *
                ASTRA_HOST_COMMAND_SIZE;
        uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
        uint32_t amount;
        uint32_t offset;
        bool writable;

        if (operation == ASTRA_HOST_FS_WRITE) {
            amount = ldl_be_p(command + HOST_FIELD(data_length));
            writable = false;
        } else if (operation == ASTRA_HOST_FS_READ ||
                   operation == ASTRA_HOST_FS_READDIR ||
                   operation == ASTRA_HOST_FS_READLINK) {
            amount = ldl_be_p(command + HOST_FIELD(data_capacity));
            writable = true;
        } else {
            continue;
        }
        offset = ldl_be_p(command + HOST_FIELD(data_offset));
        if (amount == 0 || offset < command_bytes ||
            offset > channel->byte_size ||
            amount > channel->byte_size - offset) {
            continue;
        }
        ranges[range_count++] = (AstraHostDataRange){
            offset, offset + amount, writable
        };
    }
    if (range_count > 1) {
        qsort(ranges, range_count, sizeof(*ranges),
              astra_host_data_range_compare);
    }
    for (uint32_t index = 0; index < range_count; ++index) {
        AstraHostDataRange *range = &ranges[index];

        if ((range->writable && range->start < maximum_end) ||
            (!range->writable &&
             range->start < maximum_writable_end)) {
            safe = false;
            break;
        }
        if (range->end > maximum_end) {
            maximum_end = range->end;
        }
        if (range->writable && range->end > maximum_writable_end) {
            maximum_writable_end = range->end;
        }
    }
    g_free(ranges);
    return safe;
}

static void astra_host_channel_kick(Astra68State *s, uint32_t slot,
                                    uint32_t producer)
{
    AstraHostChannel *channel = &s->host.channels[slot];
    uint8_t *base;
    uint32_t pending;
    uint32_t outstanding;

    if (!channel->active) {
        channel->status = ASTRA_SYSCALL_PEER_DEAD;
        return;
    }
    if (channel->status != ASTRA_SYSCALL_OK) {
        return;
    }
    base = astra_dma_data(s, channel->physical_buffer, channel->byte_size, 0,
                          channel->byte_size);
    if (base == NULL || !astra_host_channel_header_valid(channel, base) ||
        ldl_be_p(base + HOST_CHANNEL_HEADER_FIELD(producer_position)) !=
            producer) {
        return;
    }
    outstanding = channel->submitted_position - channel->consumer_position;
    pending = producer - channel->submitted_position;
    if (outstanding > channel->command_capacity ||
        pending > channel->command_capacity - outstanding) {
        channel->status = ASTRA_SYSCALL_INVALID_ARGUMENT;
        stl_be_p(base + HOST_CHANNEL_HEADER_FIELD(transport_status),
                 channel->status);
        return;
    }
    if (!astra_host_channel_data_safe(channel, base, producer)) {
        channel->status = ASTRA_SYSCALL_INVALID_ARGUMENT;
        stl_be_p(base + HOST_CHANNEL_HEADER_FIELD(transport_status),
                 channel->status);
        return;
    }
    channel->status = ASTRA_SYSCALL_OK;
    stl_be_p(base + HOST_CHANNEL_HEADER_FIELD(transport_status),
             channel->status);
    if (pending == 0) {
        return;
    }
    ++s->host.submissions;
    channel->submitted_position = producer;
    if (pending == 1 && outstanding == 0) {
        uint64_t started = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

        astra_host_execute_fs(
            s, channel->owner,
            base + ASTRA_HOST_CHANNEL_HEADER_SIZE +
                ((producer - 1u) & (channel->command_capacity - 1u)) *
                    ASTRA_HOST_COMMAND_SIZE,
            channel->physical_buffer, channel->byte_size,
            ASTRA_HOST_CHANNEL_HEADER_SIZE +
                channel->command_capacity * ASTRA_HOST_COMMAND_SIZE,
            channel->host_generation);
        s->host.execution_ns += qemu_clock_get_ns(QEMU_CLOCK_REALTIME) -
                                started;
        astra_host_channel_publish_completion(s, channel, base, producer - 1u,
                                              0);
        return;
    }
    for (uint32_t position = producer - pending;
         position != producer; ++position) {
        AstraHostJob *job = g_new0(AstraHostJob, 1);

        job->machine = s;
        job->slot = slot;
        job->owner = channel->owner;
        job->host_generation = channel->host_generation;
        job->channel_generation = channel->channel_generation;
        job->physical_buffer = channel->physical_buffer;
        job->byte_size = channel->byte_size;
        job->command_capacity = channel->command_capacity;
        job->position = position;
        job->started_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        ++channel->jobs;
        ++s->host.inflight;
        if (s->host.inflight > s->host.max_inflight) {
            s->host.max_inflight = s->host.inflight;
        }
        thread_pool_submit_aio(astra_host_channel_worker, job,
                               astra_host_channel_complete, job);
    }
}

#undef HOST_CHANNEL_HEADER_FIELD
#undef HOST_CHANNEL_CONFIG_FIELD
#undef HOST_SUBMISSION_FIELD

#undef HOST_FIELD

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
    if (s->ohci.present &&
        (astra_ohci_astra_status(s) &
         (OHCI_ASTRA_IRQ | OHCI_ASTRA_DMA_FAULT)) != 0) {
        pending |= 1u << IRQ_SOURCE_USB;
    }
    if (astra_input_level(&s->input) != 0) {
        pending |= 1u << IRQ_SOURCE_INPUT;
    }
    if (astra_block_present(s) &&
        (astra_block_completion_level(&s->block) != 0 ||
         s->block.state_change)) {
        pending |= 1u << IRQ_SOURCE_STORAGE;
    }
    if (s->network.ready_pending) {
        pending |= 1u << IRQ_SOURCE_NETWORK;
    }
    if (s->host.completion_pending) {
        pending |= 1u << IRQ_SOURCE_HOST;
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
    uint64_t host_ns;
    int timer_index;
    AstraTimer *timer;

    switch (offset) {
    case 0x000: return 0x56535441; /* VSTA */
    case 0x004: return 0x00010000;
    case 0x008: return 0x41363801;
    case 0x010:
        return SYS_STATUS_BASE |
               (astra_block_present(s) ? SYS_STATUS_ASTRA_HOST : 0) |
               (s->ohci.present ? SYS_STATUS_USB_READY : 0);
    case 0x018: return s->scratch;
    case 0x01c: return 0x00068030;
    case 0x020: return 0x51454d55; /* QEMU */
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
    /* The host seeds this clock at reset; Astra may discipline it later. */
    case 0x420:
        return s->rtc_valid ? RTC_VALID | RTC_ZONE_VALID : 0u;
    case 0x424:
        host_ns = (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_HOST);
        s->rtc_latch = s->rtc_valid ?
            host_ns + (s->rtc_base_ns - s->rtc_base_clock_ns) : 0u;
        return (uint32_t)s->rtc_latch;
    case 0x428:
        return (uint32_t)(s->rtc_latch >> 32);
    /*
     * Where the machine is, as the host understands it: the offset in force
     * right now, with summer time already decided by the host's own rules, and
     * the abbreviation it prints. The guest gets a local time without carrying
     * a timezone database to recompute what this side already knows.
     */
    case 0x42c:
        return 0u;
    case 0x430:
        return 0x55544300u; /* UTC */
    case 0x150:
        return astra_block_present(s) ? BLOCK_ID_MAGIC : 0;
    case 0x154:
        return astra_block_present(s) ? BLOCK_VERSION_1_1 : 0;
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
    case 0x820: return NETWORK_ID_MAGIC;
    case 0x824: return NETWORK_VERSION_1_0;
    case 0x828:
        return ASTRA_NETWORK_CAP_IPV4 | ASTRA_NETWORK_CAP_IPV6 |
               ASTRA_NETWORK_CAP_TCP | ASTRA_NETWORK_CAP_UDP |
               ASTRA_NETWORK_CAP_RESOLVE | ASTRA_NETWORK_CAP_ICMP;
    case 0x82c: return ASTRA_NETWORK_STATE_LINK_UP;
    case 0x830: return s->network.host_generation;
    case 0x834: return 2u * MiB;
    case 0x838:
        return (1u << 16) | NETWORK_QUEUE_READY |
               (s->network.ready_pending ?
                    NETWORK_QUEUE_EVENT_PENDING : 0u);
    case 0x83c: return s->network.request_buffer;
    case 0x840: return s->network.request_bytes;
    case 0x844: return s->network.request_count;
    case 0x84c: return s->network.status;
    case 0x850: return s->network.completed;
    case 0x854: return g_hash_table_size(s->network.endpoints);
    case 0x858: return s->network.ready_sequence;
    case 0x880:
        return s->host.root_fd >= 0 ? HOST_ACCEL_ID_MAGIC : 0u;
    case 0x884: return ASTRA_HOST_VERSION;
    case 0x888:
        return s->host.root_fd >= 0 ?
            ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED |
                ASTRA_HOST_CAP_SUBMISSION_DESCRIPTOR |
                ASTRA_HOST_CAP_CHANNEL |
                ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ : 0u;
    case 0x88c:
        return s->host.root_fd >= 0 ? ASTRA_HOST_STATE_READY : 0u;
    case 0x890: return s->host.generation;
    case 0x894: return HOST_ACCEL_MAX_TRANSFER;
    case 0x898: return HOST_ACCEL_MAX_TRANSFER / ASTRA_HOST_COMMAND_SIZE;
    case 0x89c: return s->host.request_buffer;
    case 0x8a0: return s->host.request_bytes;
    case 0x8a4: return s->host.request_count;
    case 0x8ac: return s->host.status;
    case 0x8b0: return s->host.completed;
    case 0x8c4:
        return (s->host.completed << HOST_SUBMIT_COMPLETED_SHIFT) |
               (s->host.status & 0xffffu);
    case 0x8cc: return s->host.channel_result;
    case 0x8d0: return s->host.completion_pending ? 1u : 0u;
    case 0x8d8: return s->host.inflight;
    case 0x8dc: return s->host.max_inflight;
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
    case 0x434:
        s->rtc_set_high = value;
        break;
    case 0x438:
        s->rtc_base_ns = ((uint64_t)s->rtc_set_high << 32) | value;
        s->rtc_base_clock_ns =
            (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_HOST);
        s->rtc_valid = true;
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
        if (value & BLOCK_RESET_BIT) {
            astra_block_reset(s);
        } else if (value & BLOCK_STATE_ACK_BIT) {
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
    case 0x83c: s->network.request_buffer = value; break;
    case 0x840: s->network.request_bytes = value; break;
    case 0x844: s->network.request_count = value; break;
    case 0x848:
        if ((value & NETWORK_EXECUTE_BIT) != 0) {
            astra_network_execute(s);
        }
        break;
    case 0x85c:
        if ((value & NETWORK_READY_ACK_BIT) != 0) {
            s->network.ready_pending = false;
            astra_update_irq(s);
        }
        break;
    case 0x860:
        if ((value & NETWORK_RESET_BIT) != 0) {
            astra_network_reset(s);
        }
        break;
    case 0x89c: s->host.request_buffer = value; break;
    case 0x8a0: s->host.request_bytes = value; break;
    case 0x8a4: s->host.request_count = value; break;
    case 0x8a8:
        if ((value & HOST_ACCEL_EXECUTE_BIT) != 0) {
            astra_host_execute(s);
        }
        break;
    case 0x8b4:
        if ((value & HOST_ACCEL_RESET_BIT) != 0) {
            astra_host_reset(s);
        }
        break;
    case 0x8b8: s->host.owner = value; break;
    case 0x8bc: astra_host_release_owner(s, value); break;
    case 0x8c0: astra_host_submit(s, value); break;
    case 0x8c8: astra_host_channel_configure(s, value); break;
    case 0x8d4:
        if ((value & 1u) != 0u) {
            for (uint32_t slot = 0; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot) {
                s->host.channels[slot].completion_pending = false;
            }
            s->host.completion_pending = false;
            astra_update_irq(s);
        }
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

/*
 * Bringing the controller out of reset is the whole of what this models. The
 * kernel writes HCR and then requires the controller to have cleared it, gone
 * to SUSPEND, and dropped its HCCA pointer -- a self-clearing reset, which a
 * plain register array cannot imitate, which is why platform.c carries a
 * host-test branch that fakes it. Here it happens for real.
 */
/*
 * OHCI signals an interrupt when master enable is set and some enabled source
 * is pending. The Astra wrapper's IRQ bit is that condition rather than a
 * separate latch, which is what makes clearing the source clear the line --
 * the kernel acknowledges by writing the status bit back and expects the
 * controller to go quiet, and a latch it had to clear separately would leave
 * the qualification's quiesce check failing forever.
 */
static bool astra_ohci_asserting(Astra68State *s)
{
    if ((s->ohci.interrupt_enable & OHCI_INT_MIE) == 0) {
        return false;
    }
    return (s->ohci.interrupt_status & s->ohci.interrupt_enable &
            ~OHCI_INT_MIE) != 0;
}

static uint32_t astra_ohci_astra_status(Astra68State *s)
{
    return s->ohci.astra_status |
           (astra_ohci_asserting(s) ? OHCI_ASTRA_IRQ : 0);
}

static void astra_ohci_reset_controller(Astra68State *s)
{
    s->ohci.control = OHCI_CONTROL_HCFS_SUSPEND;
    s->ohci.command_status = 0;
    s->ohci.interrupt_status = 0;
    s->ohci.interrupt_enable = 0;
    s->ohci.hcca = 0;
    s->ohci.astra_status = 0;
    s->ohci.frame_number = 0;
    if (s->ohci.sof_timer) {
        timer_del(s->ohci.sof_timer);
    }
}

/*
 * The frame tick. A real controller counts frames whenever it is operational,
 * whether or not anything is plugged in, and raises start-of-frame if that
 * interrupt is enabled -- so this is the one piece of controller behaviour
 * that is observable with no device attached, and it is exactly what the
 * kernel's IRQ qualification waits for.
 */
static void astra_ohci_sof(void *opaque)
{
    Astra68State *s = opaque;

    if ((s->ohci.control & OHCI_CONTROL_HCFS_MASK) !=
        OHCI_CONTROL_HCFS_OPERATIONAL) {
        return;
    }
    s->ohci.frame_number++;
    s->ohci.interrupt_status |= OHCI_INT_SF;
    timer_mod_ns(s->ohci.sof_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                 OHCI_FRAME_INTERVAL_NS);
    astra_update_irq(s);
}

/* Frames run while the controller is operational and stop when it is not. */
static void astra_ohci_sync_frames(Astra68State *s)
{
    if (!s->ohci.sof_timer) {
        return;
    }
    if ((s->ohci.control & OHCI_CONTROL_HCFS_MASK) ==
        OHCI_CONTROL_HCFS_OPERATIONAL) {
        timer_mod_ns(s->ohci.sof_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                     OHCI_FRAME_INTERVAL_NS);
    } else {
        timer_del(s->ohci.sof_timer);
    }
}

static uint32_t astra_ohci_read32(Astra68State *s, hwaddr offset)
{
    switch (offset) {
    case 0x000: return OHCI_REVISION_1_0;
    case 0x004: return s->ohci.control;
    case 0x008: return s->ohci.command_status;
    case 0x00c: return s->ohci.interrupt_status;
    case 0x010: return s->ohci.interrupt_enable;
    case 0x014: return s->ohci.interrupt_enable;
    case 0x018: return s->ohci.hcca;
    case 0x03c: return s->ohci.frame_number & 0xffffu;
    case 0xf00: return OHCI_ASTRA_ID_MAGIC;
    case 0xf04: return OHCI_ASTRA_VERSION_1_0;
    case 0xf08: return astra_ohci_astra_status(s);
    case 0xf0c: return 0;
    case 0xf10: return s->ohci.pool_base;
    case 0xf14: return s->ohci.pool_size;
    default: return 0;
    }
}

static void astra_ohci_write32(Astra68State *s, hwaddr offset, uint32_t value)
{
    switch (offset) {
    case 0x004:
        s->ohci.control = value;
        astra_ohci_sync_frames(s);
        astra_update_irq(s);
        break;
    case 0x008:
        /* HCR is self-clearing: the reset is complete before the write is. */
        if ((value & OHCI_COMMAND_HCR) != 0) {
            astra_ohci_reset_controller(s);
            astra_update_irq(s);
        } else {
            s->ohci.command_status = value;
        }
        break;
    case 0x00c:
        s->ohci.interrupt_status &= ~value;
        astra_update_irq(s);
        break;
    case 0x010:
        s->ohci.interrupt_enable |= value;
        astra_update_irq(s);
        break;
    case 0x014:
        s->ohci.interrupt_enable &= ~value;
        astra_update_irq(s);
        break;
    case 0x018:
        s->ohci.hcca = value;
        break;
    case 0xf08:
        if ((value & OHCI_ASTRA_DMA_FAULT) != 0) {
            s->ohci.astra_status &= ~OHCI_ASTRA_DMA_FAULT;
        }
        break;
    default:
        break;
    }
}

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
static uint32_t astra_ohci_read32_wrap(Astra68State *s, hwaddr o)
{ return astra_ohci_read32(s, o); }
static void astra_ohci_write32_wrap(Astra68State *s, hwaddr o, uint32_t v)
{ astra_ohci_write32(s, o, v); }

static uint64_t astra_host_channel_read(void *opaque, hwaddr offset,
                                        unsigned size)
{
    Astra68State *s = opaque;
    uint32_t slot = offset / ASTRA_HOST_CHANNEL_PAGE_SIZE;
    uint32_t field = offset & (ASTRA_HOST_CHANNEL_PAGE_SIZE - 1u);
    AstraHostChannel *channel;

    if (size != 4 || slot >= ASTRA_HOST_CHANNEL_COUNT) {
        return 0;
    }
    channel = &s->host.channels[slot];
    switch (field) {
    case 0x00: return ASTRA_HOST_CHANNEL_MAGIC;
    case 0x04: return ASTRA_HOST_CHANNEL_VERSION;
    case ASTRA_HOST_CHANNEL_STATE_OFFSET:
        return channel->active ? ASTRA_HOST_STATE_READY : 0;
    case ASTRA_HOST_CHANNEL_GENERATION_OFFSET:
        return channel->channel_generation;
    case ASTRA_HOST_CHANNEL_CONSUMER_OFFSET:
        return channel->consumer_position;
    case ASTRA_HOST_CHANNEL_STATUS_OFFSET:
        return channel->status;
    default: return 0;
    }
}

static void astra_host_channel_write(void *opaque, hwaddr offset,
                                     uint64_t value, unsigned size)
{
    Astra68State *s = opaque;
    uint32_t slot = offset / ASTRA_HOST_CHANNEL_PAGE_SIZE;
    uint32_t field = offset & (ASTRA_HOST_CHANNEL_PAGE_SIZE - 1u);

    if (size != 4 || slot >= ASTRA_HOST_CHANNEL_COUNT) {
        return;
    }
    if (field == ASTRA_HOST_CHANNEL_KICK_OFFSET) {
        astra_host_channel_kick(s, slot, value);
    } else if (field == ASTRA_HOST_CHANNEL_ARM_OFFSET) {
        astra_host_channel_arm(s, slot, value);
    } else if (field == ASTRA_HOST_CHANNEL_DISARM_OFFSET &&
               (value & 1u) != 0u) {
        astra_host_channel_disarm(s, slot);
    }
}

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
#define astra_ohci_read32 astra_ohci_read32_wrap
#define astra_ohci_write32 astra_ohci_write32_wrap
ASTRA_MMIO_WRAPPERS(astra_ohci)
#undef astra_ohci_read32
#undef astra_ohci_write32

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
static const MemoryRegionOps astra_ohci_ops = ASTRA_OPS(astra_ohci);
static const MemoryRegionOps astra_host_channel_ops = {
    .read = astra_host_channel_read,
    .write = astra_host_channel_write,
    .endianness = DEVICE_BIG_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .impl.min_access_size = 4,
    .impl.max_access_size = 4,
};

/* Every guest-visible chip, including future audio/math devices, resets here. */
static void astra_machine_reset(void *opaque)
{
    Astra68State *s = opaque;
    int i;

    cpu_reset(CPU(s->cpu));
    s->cpu->env.aregs[7] = s->initial_sp;
    s->cpu->env.pc = s->initial_pc;
    s->reset_clock_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    s->rtc_base_ns = (uint64_t)qemu_clock_get_ns(QEMU_CLOCK_HOST);
    s->rtc_base_clock_ns = s->rtc_base_ns;
    s->rtc_latch = s->rtc_base_ns;
    s->rtc_set_high = 0u;
    s->rtc_valid = true;
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

    astra_block_reset(s);
    astra_display_reset(s);
    astra_network_reset(s);
    astra_host_reset(s);
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
    const char *hostfs_root;
    const char *text_plane_path;
    const char *cut_after_text;
    int i;

    s->trace_timers = g_getenv("ASTRA_QEMU_TIMER_TRACE") != NULL;
    s->block.trace_durability =
        g_getenv("ASTRA_QEMU_BLOCK_TRACE") != NULL;
    cut_after_text = g_getenv("ASTRA_QEMU_BLOCK_CUT_AFTER");
    if (cut_after_text != NULL && cut_after_text[0] != '\0') {
        char *end = NULL;

        errno = 0;
        s->block.cut_after_transition =
            g_ascii_strtoull(cut_after_text, &end, 10);
        if (errno != 0 || end == cut_after_text || *end != '\0' ||
            s->block.cut_after_transition == 0) {
            error_report("invalid ASTRA_QEMU_BLOCK_CUT_AFTER '%s'",
                         cut_after_text);
            exit(EXIT_FAILURE);
        }
    }
    s->input.host_generation = 0;
    s->network.endpoints = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, astra_network_endpoint_free);
    s->network.resolvers = g_hash_table_new_full(
        g_direct_hash, g_direct_equal, NULL, astra_network_resolver_free);
    s->host.root_fd = -1;
    qemu_mutex_init(&s->host.files_lock);
    s->host.files = g_hash_table_new(g_direct_hash, g_direct_equal);
    hostfs_root = g_getenv("ASTRA_HOSTFS_ROOT");
    if (hostfs_root != NULL && hostfs_root[0] != '\0') {
        s->host.root_fd = open(hostfs_root,
                               O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                   O_NOFOLLOW);
        if (s->host.root_fd < 0) {
            error_report("cannot open Astra host filesystem root '%s': %s",
                         hostfs_root, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
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
                                   "astra-block-write-requests",
                                   &s->block.write_requests,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-block-write-sectors",
                                   &s->block.write_sectors,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-block-flush-requests",
                                   &s->block.flush_requests,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-block-durability-transitions",
                                   &s->block.durability_transitions,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-block-power-cut-after",
                                   &s->block.cut_after_transition,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-submissions",
                                   &s->host.submissions,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-commands",
                                   &s->host.commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-execution-ns",
                                   &s->host.execution_ns,
                                   OBJ_PROP_FLAG_READ);
    {
        static const char *const names[] = {
            "astra-host-fs-invalid", "astra-host-fs-open",
            "astra-host-fs-close", "astra-host-fs-read",
            "astra-host-fs-write", "astra-host-fs-sync",
            "astra-host-fs-truncate", "astra-host-fs-stat",
            "astra-host-fs-readdir", "astra-host-fs-mkdir",
            "astra-host-fs-unlink", "astra-host-fs-rename",
            "astra-host-fs-chmod", "astra-host-fs-readlink",
            "astra-host-fs-symlink"
        };

        G_STATIC_ASSERT(G_N_ELEMENTS(names) ==
                        ASTRA_HOST_FS_SYMLINK + 1u);
        for (size_t index = 0; index < G_N_ELEMENTS(names); ++index)
            object_property_add_uint64_ptr(
                OBJECT(machine), names[index],
                &s->host.operation_counts[index], OBJ_PROP_FLAG_READ);
    }
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-inflight",
                                   &s->host.inflight,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-max-inflight",
                                   &s->host.max_inflight,
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

    /* Real constraints only: POST minimum, page alignment, and aperture. */
    if (machine->ram_size < MiB ||
        (machine->ram_size & (4 * KiB - 1)) != 0 ||
        (uint64_t)ASTRA_SDRAM_BASE + machine->ram_size > ASTRA_ROM_BASE) {
        error_report("Astra68 RAM must be at least 1 MiB, a multiple of 4 KiB,"
                     " and must fit below the ROM aperture at 0x%08x",
                     ASTRA_ROM_BASE);
        exit(EXIT_FAILURE);
    }
    if (!firmware) {
        error_report("Astra68 requires -bios <astra_boot.bin>");
        exit(EXIT_FAILURE);
    }

    s->ram_size = machine->ram_size;
    s->cpu = M68K_CPU(cpu_create(machine->cpu_type));
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-pmmu-tlb-fills",
                                   &s->cpu->env.pmmu030.qemu_tlb_fills,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-pmmu-atc-hits",
                                   &s->cpu->env.pmmu030.qemu_atc_hits,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-pmmu-table-walks",
                                   &s->cpu->env.pmmu030.qemu_table_walks,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-pmmu-crp-writes",
                                   &s->cpu->env.pmmu030.qemu_crp_writes,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-pmmu-crp-changes",
                                   &s->cpu->env.pmmu030.qemu_crp_changes,
                                   OBJ_PROP_FLAG_READ);
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
        s->display.mailbox_lock_fd = open(display_mailbox_path,
                                          O_RDWR | O_CLOEXEC);
        if (s->display.mailbox_lock_fd < 0) {
            error_report("cannot open display mailbox '%s': %s",
                         display_mailbox_path, strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (flock(s->display.mailbox_lock_fd, LOCK_EX | LOCK_NB) != 0) {
            if (errno == EWOULDBLOCK) {
                error_report("display mailbox '%s' is already owned by "
                             "another QEMU", display_mailbox_path);
            } else {
                error_report("cannot lock display mailbox '%s': %s",
                             display_mailbox_path, strerror(errno));
            }
            exit(EXIT_FAILURE);
        }
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
    memory_region_init_io(&s->host_channel_io, NULL,
                          &astra_host_channel_ops, s,
                          "astra68.host-channel", ASTRA_HOST_CHANNEL_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_HOST_CHANNEL_BASE,
                                &s->host_channel_io);
    memory_region_init_io(&s->panel_io, NULL, &astra_panel_ops, s,
                          "astra68.panel", ASTRA_PANEL_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_PANEL_BASE, &s->panel_io);
    memory_region_init_io(&s->astraea_io, NULL, &astra_astraea_ops, s,
                          "astra68.astraea", ASTRA_ASTRAEA_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_ASTRAEA_BASE, &s->astraea_io);
    memory_region_init_io(&s->vega_io, NULL, &astra_vega_ops, s,
                          "astra68.vega", ASTRA_VEGA_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_VEGA_BASE, &s->vega_io);
    /*
     * The controller only claims to be ready if its aperture actually fits in
     * this machine's RAM. A design that advertised USB it could not back would
     * be a broken board, and the firmware would rightly refuse to boot on it.
     */
    s->ohci.pool_base = OHCI_DMA_POOL_BASE;
    s->ohci.pool_size = OHCI_DMA_POOL_SIZE;
    s->ohci.present =
        (uint64_t)OHCI_DMA_POOL_BASE + OHCI_DMA_POOL_SIZE <=
        (uint64_t)ASTRA_SDRAM_BASE + machine->ram_size;
    astra_ohci_reset_controller(s);
    memory_region_init_io(&s->ohci_io, NULL, &astra_ohci_ops, s,
                          "astra68.ohci", ASTRA_OHCI_SIZE);
    memory_region_add_subregion(sysmem, ASTRA_OHCI_BASE, &s->ohci_io);

    for (i = 0; i < 2; i++) {
        s->timers[i].machine = s;
        s->timers[i].index = i;
        s->timers[i].qemu_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                               astra_timer_expired,
                                               &s->timers[i]);
    }
    s->vega.vblank_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, astra_vblank, s);
    s->ohci.sof_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, astra_ohci_sof, s);
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
        QTAILQ_INIT(&s->block.requests);
    }

    astra_machine_reset(s);
}

static void astra68_machine_init(MachineClass *mc)
{
    mc->desc = "Astra 68 reference machine";
    mc->init = astra68_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68030");
    mc->default_ram_size = ASTRA_SDRAM_HOSTED_SIZE;
    mc->default_ram_id = "astra68.sdram";
    mc->max_cpus = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

DEFINE_MACHINE("astra68", astra68_machine_init)
