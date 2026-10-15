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
#include <netdb.h>
#include <poll.h>
#include <sys/un.h>
#ifdef CONFIG_POSIX
#include <ifaddrs.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/statvfs.h>
#include <sys/resource.h>
#endif
#ifdef CONFIG_LINUX
#include <sys/syscall.h>
#endif
#include "qemu/bswap.h"
#include "qemu/datadir.h"
#include "qemu/error-report.h"
#include "qemu/atomic.h"
#ifdef CONFIG_LINUX
#include "qemu/futex.h"
#include "qemu/event_notifier.h"
#include "qemu/thread.h"
#endif
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/sockets.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "cpu.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/m68k/astra_input.h"
#include "hw/m68k/astra_display_mailbox.h"
#include "hw/m68k/astra_render_batch.h"
#include "hw/m68k/astra_render_protocol.h"
#include "astra68-resolve.h"
#include "astra_storage_transport.h"
#include "astra/block.h"
#include "astra/boot.h"
#include "astra/audio_host.h"
#include "astra/audio_mailbox.h"
#include "astra/audio_stream.h"
#include "astra/copy_engine.h"
#include "astra/display.h"
#include "astra/host.h"
#include "astra/process.h"
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
#define ASTRA_GUEST_SHUTDOWN_EXIT_CODE 88
#define ASTRA_GUEST_RESTART_EXIT_CODE 89

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
 * The active 512 MiB guest has RAM above it, which exercises split usable
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
 * Block and host-only display requests complete from a timer armed for now:
 * it runs from the main loop after the submitting store has retired, which
 * keeps the guest on its interrupt path without adding any latency. The
 * non-Linux mailbox has no futex to wake on and polls at this period.
 */
#define DISPLAY_MAILBOX_POLL_NS 50000ull

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
#define ASTRA_REMOTE_DESKTOP_CONTROL_DEFAULT \
    "/run/astra/remote-desktop-control.sock"
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
    /* When the armed countdown is due, on QEMU_CLOCK_VIRTUAL. */
    int64_t due_ns;
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
    /* Motion records absorbed by an unread one on the same axis. */
    uint64_t coalesced;
    bool overflow;
    /* The frame has released the queued motion: interrupt for it. */
    bool motion_released;
    /* Half a frame after vblank, so the motion is consumed and published
       before the display samples the pointer at the next one: every frame
       sees the newest position, never a race with the vblank. */
    QEMUTimer *motion_timer;
    QemuInputHandlerState *handler;
} AstraInputState;

/* A request the display holds, from the submit it accepted until it
   completes: queued, then running at the head of the FIFO. */
typedef struct AstraDisplayRequest {
    uint32_t id;
    uint32_t operation;
    /* The color, or the guest physical source (READ_SURFACE: the buffer the
       rows return to). */
    uint32_t source;
    uint32_t byte_size;
    /* Bytes taken from source: a batch ends where its attachment starts. */
    uint32_t source_bytes;
    uint32_t attachment;
    uint32_t attachment_target;
    uint32_t attachment_bytes;
    /* The attachment as it was when the submit was accepted behind another
       request: the guest may reuse its staging once accepted. Grown, never
       shrunk. */
    uint8_t *staged;
    uint32_t staged_capacity;
    bool staged_valid;
} AstraDisplayRequest;

typedef struct AstraDisplayCompletion {
    uint32_t id;
    uint32_t status;
    uint32_t generation;
} AstraDisplayCompletion;

typedef struct AstraDisplayState {
    MemoryRegion mailbox_region;
    AstraDisplayMailbox *mailbox;
    /* ASTRA_DISPLAY_PAYLOAD_PATH, mapped directly: on the DE25 it is the
       non-cacheable host arena the render engine reads. */
    uint8_t *payload;
    QEMUTimer *service_timer;
    uint32_t request_id;
    uint32_t request_op;
    uint32_t request_source;
    uint32_t request_attachment;
    /* ASTRA_DISPLAY_HOST_QUEUE_DEPTH slots: requests (the head runs) and
       the completions the guest has not popped. */
    AstraDisplayRequest requests[ASTRA_DISPLAY_HOST_QUEUE_DEPTH];
    uint32_t request_head;
    uint32_t request_count;
    AstraDisplayCompletion completion_fifo[ASTRA_DISPLAY_HOST_QUEUE_DEPTH];
    uint32_t completion_head;
    uint32_t completion_count;
    /* Accepted submits, modulo 256 in DISPLAY_QUEUE. */
    uint32_t accepted;
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
    uint64_t render_only_batches;
    uint64_t fill_commands;
    uint64_t blit_commands;
    uint64_t glyph_commands;
    uint64_t triangle_commands;
    uint64_t surface_reads;
    /* Submits accepted while another request ran. */
    uint64_t queued_submissions;
    uint64_t cursor_x;
    uint64_t cursor_y;
    uint64_t cursor_visible;
    uint64_t cursor_updates;
    /* Newest DISPLAY_CURSOR word (ASTRA_DISPLAY_HOST_CURSOR_PACK). */
    uint32_t cursor;
    uint32_t cursor_sequence;
    uint32_t wake_sequence;
    int mailbox_lock_fd;
#ifdef CONFIG_LINUX
    /* The helper wakes completion_sequence; this thread turns that wake into
       a main-loop event, so a completion is seen when it is written. */
    QemuThread completion_thread;
    QemuEvent completion_armed;
    EventNotifier completion_notifier;
#endif
    bool mailbox_enabled;
} AstraDisplayState;

/* One application audio stream (astra/audio_stream.h): its buffers are in
   guest memory, and each is copied into the stream's mailbox ring when the
   ring has room for it, then given back when the mixer consumes it. */
typedef struct AstraAudioStream {
    uint32_t owner;
    uint32_t generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t buffer_count;
    uint32_t stride;
    uint32_t audio_bytes;
    /* Buffers given back: the header's `consumed` is written from this. */
    uint32_t consumed;
    uint32_t copied;
    /* The header's `queued` as the last kick read it. Only the vCPU thread
       reads the header's counter, right after the guest's own stores to the
       buffers it covers; the main loop, refilling on a daemon wake, works
       from this copy, so on a weakly ordered host it can never see the
       count before the samples. */
    uint32_t queued;
    bool active;
    bool armed;
} AstraAudioStream;

typedef struct AstraAudioStreamState {
    AstraAudioStream streams[ASTRA_AUDIO_MAILBOX_STREAMS];
    AstraAudioMailbox *mailbox;
    uint8_t *rings;
    uint32_t result;
    int lock_fd;
#ifdef CONFIG_LINUX
    /* Sleeps on the daemon's room_sequence and hands each wake to the main
       loop, which copies the buffers that now fit. */
    QemuThread room_thread;
    EventNotifier room_notifier;
#endif
    bool enabled;
    /* A stream interrupt raised and not yet acknowledged (CHANNEL_ACK). */
    bool completion_pending;
} AstraAudioStreamState;

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
    struct sockaddr_storage reverse_address;
    socklen_t reverse_length;
    int resolver_status;
    bool reverse;
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

typedef struct AstraHostDescription {
    Astra68State *machine;
    int fd;
    DIR *directory;
    uint64_t directory_cookie;
    gint references;
    uint16_t flags;
    bool charged;
    QemuMutex lock;
} AstraHostDescription;

/* Native identity survives descriptor loss; files and pending shares use
 * the same retained identity rather than inventing a lost-object ledger. */
typedef struct AstraHostIdentity {
    uint64_t mount;
    uint64_t mount_generation;
    uint64_t inode;
    uint16_t flags;
    uint16_t kind;
} AstraHostIdentity;

typedef struct AstraHostFile {
    AstraHostDescription *description;
    uint32_t owner;
    uint32_t process;
    uint32_t session; /* the session it was opened in */
    uint32_t references;
    bool closing;
    AstraHostIdentity identity;
} AstraHostFile;

typedef struct AstraHostShare {
    uint64_t token;
    uint32_t owner;
    uint32_t process;
    uint32_t session;
    AstraHostDescription *description;
    AstraHostIdentity identity;
} AstraHostShare;

/* A rooted session: a directory every path of its commands resolves
 * beneath, opened by the claimed filesystem owner (record 4). */
typedef struct AstraHostSession {
    int root;               /* directory fd: the confinement root */
    uint32_t owner;         /* the claimed owner that opened it */
    uint32_t requester;     /* authenticated grant holder, zero for service roots */
    uint64_t mount;
    uint64_t mount_generation;
    uint32_t references;
    uint16_t flags;         /* ASTRA_HOST_FS_SESSION_READ_ONLY */
    bool closing;
} AstraHostSession;

typedef struct AstraHostJob {
    Astra68State *machine;
    uint32_t slot;
    uint32_t owner;
    uint32_t process;
    uint32_t host_generation;
    uint32_t channel_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t position;
    uint64_t started_ns;
    struct AstraHostJob *next;
    AstraHostSession *session; /* borrowed while this canonical command executes */
    uint64_t id;
} AstraHostJob;

/* A scope the kernel bound to a channel: the provider word (a session of
 * the claimed owner), its rights, and its creator (FS15, FS44). */
typedef struct AstraHostChannelScope {
    uint32_t scope;
    uint32_t flags;          /* ASTRA_HOST_SCOPE_READ_ONLY */
    uint32_t owner;
    uint32_t files;
    bool live;
} AstraHostChannelScope;

typedef struct AstraHostChannel {
    uint32_t owner;
    uint32_t process;
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
    int remote_desktop_fd;
    int audio_fd;
    uint32_t remote_desktop_generation;
    bool active;
    bool interrupt_armed;
    bool completion_pending;
    /* Opened on a scope: filesystem commands only, each through one of
     * `scopes` (vCPU writes under files_lock; workers read under it). */
    bool bound;
    /* Opened unbound by the filesystem provider: the kernel saw HOST_SCOPE
     * on its device handle. The only channel CLAIM is honoured on. */
    bool provider;
    uint32_t scope_count;
    AstraHostChannelScope scopes[ASTRA_HOST_CHANNEL_SCOPE_MAX];
} AstraHostChannel;

typedef struct AstraHostState {
    GHashTable *files;
    GHashTable *lost_handles; /* disconnected handle -> AstraHostFile identity */
    GHashTable *shares; /* token -> description reference, under files_lock */
    QemuMutex files_lock;
    QemuMutex namespace_lock; /* namespace changes cannot race rooted walks */
    GHashTable *sessions;   /* id -> AstraHostSession, under files_lock */
    uint32_t next_session;  /* never reset: an id is not reused */
    uint32_t fs_owner;      /* the claimed filesystem owner, 0 when free */
    uint32_t fs_lost_owner;
    uint32_t fs_epoch;
    uint64_t fs_resets;
    uint64_t fs_descriptors;
    uint64_t fs_directories;
    uint64_t fs_max_descriptors;
    uint64_t fs_max_directories;
    uint64_t usage_sequence; /* canonical resource mutations, never reset */
    uint64_t next_job;
    AstraHostJob *jobs; /* canonical queued/active commands, under files_lock */
    /* Private Linux provider authority. Native descriptions remain here. */
    int provider_control;
    int provider_path;
    bool provider_broken;
    uint64_t provider_epoch;
    uint64_t provider_control_serial;
    uint64_t provider_path_serial;
    QemuMutex provider_control_lock;
    QemuMutex provider_path_lock;
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
    uint64_t non_fs_commands;
    uint64_t non_fs_execution_ns;
    uint64_t execution_ns;
    /* Worst submit-to-completion time of a channel command (writable, so a
     * measurement can start from zero), and how many took over 20 ms. */
    uint64_t execution_max_ns;
    uint64_t slow_commands;
    uint64_t operation_counts[ASTRA_HOST_FS_MAX + 1u];
    uint64_t operation_execution_ns[ASTRA_HOST_FS_MAX + 1u];
    uint64_t inflight;
    uint64_t max_inflight;
    uint64_t scoped_commands;
    uint32_t channel_result;
    bool completion_pending;
    AstraHostChannel channels[ASTRA_HOST_CHANNEL_COUNT];
    QemuMutex channel_socket_locks[ASTRA_HOST_CHANNEL_COUNT];
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
    /* CPU_CYCLES_LO latches the whole count; CPU_CYCLES_HI reads the latch
     * (vesta.h), so a low-word wrap between the reads cannot tear it. */
    uint64_t cycles_latch;
    /* Worst lateness of a timer expiry callback past its due time
     * (writable, so a measurement can start from zero), and how many were
     * over 1 ms late. */
    uint64_t timer_late_max_ns;
    uint64_t timer_late_count;
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
    uint32_t copy_status;
    uint64_t copy_lists;
    AstraNetworkState network;
    AstraHostState host;
    AstraAudioStreamState audio;
    uint8_t panel_led_data;
    uint8_t panel_led_ownership;
#ifdef CONFIG_POSIX
    volatile uint32_t *host_panel;
#endif
    bool trace_timers;
};

static Astra68State *astra_input_machine;

static void astra_update_irq(Astra68State *s);
static void astra_audio_stream_stop(Astra68State *s, uint32_t slot);
static uint32_t astra_ohci_astra_status(Astra68State *s);
static uint64_t astra_now_cycles(Astra68State *s);
static void astra_panel_write32(Astra68State *s, hwaddr offset,
                                uint32_t value);

static uint32_t astra_display_held(const AstraDisplayState *display)
{
    return display->request_count + display->completion_count;
}

static uint32_t astra_display_queue(const AstraDisplayState *display)
{
    uint32_t held = astra_display_held(display);

    return (display->request_count != 0u ?
                ASTRA_DISPLAY_HOST_QUEUE_BUSY : 0u) |
           (held << ASTRA_DISPLAY_HOST_QUEUE_HELD_SHIFT) |
           (held < ASTRA_DISPLAY_HOST_QUEUE_DEPTH ?
                ASTRA_DISPLAY_HOST_QUEUE_REQUEST_READY : 0u) |
           (display->completion_count != 0u ?
                ASTRA_DISPLAY_HOST_QUEUE_COMPLETION_VALID : 0u) |
           ((display->accepted & 0xffu) <<
            ASTRA_DISPLAY_HOST_QUEUE_ACCEPTED_SHIFT);
}

static AstraDisplayRequest *astra_display_running(AstraDisplayState *display)
{
    return display->request_count != 0u ?
        &display->requests[display->request_head] : NULL;
}

/* Astraea IRQ_STAT: DRAW_DONE stands while any display completion waits to
   be popped, so one that lands after the guest cleared the bit for an
   earlier one still interrupts. */
static uint32_t astra_astraea_irq_status(const Astra68State *s)
{
    return s->astraea.irq_status |
           (s->display.completion_count != 0u ? ASTRAEA_IRQ_DRAW_DONE : 0u);
}

/* The oldest completion the guest has not popped, or NULL. */
static const AstraDisplayCompletion *
astra_display_completion(const AstraDisplayState *display)
{
    return display->completion_count != 0u ?
        &display->completion_fifo[display->completion_head] : NULL;
}

static void astra_display_count_batch(Astra68State *s, uint32_t source,
                                      uint32_t byte_size)
{
    AstraDisplayState *display = &s->display;
    const uint8_t *batch = s->sdram + (source - ASTRA_SDRAM_BASE);
    uint32_t count = ldl_be_p(batch + 12u);
    uint32_t version = ldl_be_p(batch + 4u);
    uint32_t scene_offset = ldl_be_p(batch + 48u);
    uint32_t scene_bytes = ldl_be_p(batch + 52u);
    uint32_t records = ASTRA_RENDER_BATCH_SUBMISSION_OFFSET -
                       ASTRA_RENDER_BATCH_ARENA_OFFSET;

    if (ldl_be_p(batch) != ASTRA_RENDER_BATCH_MAGIC ||
        (version != ASTRA_RENDER_BATCH_VERSION_1_2 &&
         version != ASTRA_RENDER_BATCH_VERSION_1_3 &&
         version != ASTRA_RENDER_BATCH_VERSION_1_4) ||
        ldl_be_p(batch + 8u) != byte_size ||
        ldl_be_p(batch + 16u) != ASTRA_RENDER_BATCH_SUBMISSION_OFFSET ||
        records > byte_size || count > ASTRA_RENDER_RING_ENTRIES ||
        count > (byte_size - records) / ASTRA_RENDER_COMMAND_BYTES ||
        ((version == ASTRA_RENDER_BATCH_VERSION_1_2 ||
          version == ASTRA_RENDER_BATCH_VERSION_1_4) &&
         (count == 0u || scene_offset != 0u || scene_bytes != 0u)) ||
        (version == ASTRA_RENDER_BATCH_VERSION_1_3 &&
         (scene_offset < ASTRA_RENDER_BATCH_DATA_OFFSET ||
          scene_bytes < 64u || scene_offset < ASTRA_RENDER_BATCH_ARENA_OFFSET ||
          (uint64_t)(scene_offset - ASTRA_RENDER_BATCH_ARENA_OFFSET) +
              scene_bytes > byte_size)))
        return;
    ++display->batch_submissions;
    display->batch_commands += count;
    for (uint32_t index = 0; index < count; ++index) {
        uint32_t opcode = ldl_be_p(batch + records +
                                   index * ASTRA_RENDER_COMMAND_BYTES + 4u) >>
                          16;

        /* A FILL_RECTS command is one fill command of many records. */
        if (opcode == ASTRA_RENDER_OP_FILL ||
            opcode == ASTRA_RENDER_OP_FILL_RECTS)
            ++display->fill_commands;
        else if (opcode == ASTRA_RENDER_OP_BLIT)
            ++display->blit_commands;
        else if (opcode == ASTRA_RENDER_OP_GLYPH_RUN)
            ++display->glyph_commands;
        else if (opcode == ASTRA_RENDER_OP_TRIANGLES)
            ++display->triangle_commands;
    }
    if (version == ASTRA_RENDER_BATCH_VERSION_1_4)
        ++display->render_only_batches;
}

/*
 * A render batch's attachment list (DISPLAY_REQ_ATTACH): extents of guest RAM
 * the device places, in order, at batch offset `target`. The attachment ends
 * the batch's defined bytes -- only row padding follows it -- so the batch's
 * own bytes end at `target`. Returns the bytes of batch to take from
 * the request source, or zero if the list does not describe this batch.
 */
static uint32_t astra_display_attachment_valid(Astra68State *s,
                                               uint32_t list,
                                               uint32_t byte_size)
{
    const uint64_t ram_end = (uint64_t)ASTRA_SDRAM_BASE + s->ram_size;
    const uint8_t *header;
    uint32_t count;
    uint32_t target;
    uint32_t bytes;
    uint64_t total = 0u;

    if ((list & 3u) != 0u || list < ASTRA_SDRAM_BASE ||
        (uint64_t)list + 16u > ram_end)
        return 0u;
    header = s->sdram + (list - ASTRA_SDRAM_BASE);
    count = ldl_be_p(header + 4u);
    target = ldl_be_p(header + 8u);
    bytes = ldl_be_p(header + 12u);
    if (ldl_be_p(header) != ASTRA_RENDER_ATTACHMENT_MAGIC || count == 0u ||
        count > ASTRA_RENDER_ATTACHMENT_EXTENT_MAX ||
        (uint64_t)list + 16u + (uint64_t)count * 8u > ram_end ||
        target < ASTRA_RENDER_BATCH_MIN_BYTES || (target & 3u) != 0u ||
        bytes == 0u || target > byte_size || bytes > byte_size - target)
        return 0u;
    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t physical = ldl_be_p(header + 16u + index * 8u);
        uint32_t length = ldl_be_p(header + 20u + index * 8u);

        if (length == 0u || physical < ASTRA_SDRAM_BASE ||
            (uint64_t)physical + length > ram_end)
            return 0u;
        total += length;
    }
    return total == bytes ? target : 0u;
}

/* The attachment's extents, in order, to @p target. */
static void astra_display_attachment_copy(Astra68State *s, uint32_t list,
                                          uint8_t *target)
{
    const uint8_t *header = s->sdram + (list - ASTRA_SDRAM_BASE);
    uint32_t count = ldl_be_p(header + 4u);

    for (uint32_t index = 0u; index < count; ++index) {
        uint32_t physical = ldl_be_p(header + 16u + index * 8u);
        uint32_t length = ldl_be_p(header + 20u + index * 8u);

        memcpy(target, s->sdram + (physical - ASTRA_SDRAM_BASE), length);
        target += length;
    }
}

/* A request accepted behind another keeps its attachment's bytes now: the
   guest owns its staging again once the submit is accepted. */
static void astra_display_attachment_stage(Astra68State *s,
                                           AstraDisplayRequest *request)
{
    if (request->staged_capacity < request->attachment_bytes) {
        request->staged = g_realloc(request->staged,
                                    request->attachment_bytes);
        request->staged_capacity = request->attachment_bytes;
    }
    astra_display_attachment_copy(s, request->attachment, request->staged);
    request->staged_valid = true;
}

/*
 * The copy engine: runs the AstraCopyList at `list` over guest RAM while the
 * kernel's store to COPY_LIST is in flight, so the copy is done when that
 * store retires. The kernel validated every page; the engine only refuses
 * what lies outside RAM.
 */
static uint32_t astra_copy_run(Astra68State *s, uint32_t list)
{
    const uint64_t ram_end = (uint64_t)ASTRA_SDRAM_BASE + s->ram_size;
    const uint8_t *header;
    uint32_t count;

    if ((list & 3u) != 0u || list < ASTRA_SDRAM_BASE ||
        (uint64_t)list + 8u > ram_end)
        return ASTRA_COPY_STATUS_INVALID;
    header = s->sdram + (list - ASTRA_SDRAM_BASE);
    count = ldl_be_p(header + 4u);
    if (ldl_be_p(header) != ASTRA_COPY_LIST_MAGIC || count == 0u ||
        count > ASTRA_COPY_LIST_MAX ||
        (uint64_t)list + 8u + (uint64_t)count * 12u > ram_end)
        return ASTRA_COPY_STATUS_INVALID;
    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *extent = header + 8u + index * 12u;
        uint32_t source = ldl_be_p(extent);
        uint32_t destination = ldl_be_p(extent + 4u);
        uint32_t bytes = ldl_be_p(extent + 8u);

        if (source < ASTRA_SDRAM_BASE || destination < ASTRA_SDRAM_BASE ||
            (uint64_t)source + bytes > ram_end ||
            (uint64_t)destination + bytes > ram_end)
            return ASTRA_COPY_STATUS_INVALID;
    }
    for (uint32_t index = 0u; index < count; ++index) {
        const uint8_t *extent = header + 8u + index * 12u;

        memmove(s->sdram + (ldl_be_p(extent + 4u) - ASTRA_SDRAM_BASE),
                s->sdram + (ldl_be_p(extent) - ASTRA_SDRAM_BASE),
                ldl_be_p(extent + 8u));
    }
    ++s->copy_lists;
    return ASTRA_COPY_STATUS_OK;
}

static void astra_display_start(Astra68State *s);

/* The running request is done: its completion joins the FIFO the guest
   pops, and the next request, if one is queued, starts at once, so the
   device runs back to back without waiting for the guest. */
static void astra_display_complete(Astra68State *s, uint32_t status,
                                   uint32_t generation)
{
    AstraDisplayState *display = &s->display;
    AstraDisplayRequest *done = astra_display_running(display);
    AstraDisplayCompletion *completion;

    if (done == NULL)
        return;
    completion = &display->completion_fifo[
        (display->completion_head + display->completion_count) %
        ASTRA_DISPLAY_HOST_QUEUE_DEPTH];
    *completion = (AstraDisplayCompletion){
        .id = done->id, .status = status, .generation = generation,
    };
    ++display->completion_count;
    done->staged_valid = false;
    display->request_head =
        (display->request_head + 1u) % ASTRA_DISPLAY_HOST_QUEUE_DEPTH;
    --display->request_count;
    display->generation = generation;
    display->completion_cycle = astra_now_cycles(s);
    ++display->completions;
    if (display->request_count != 0u)
        astra_display_start(s);
    astra_update_irq(s);
}

/* Bytes per pixel of a byte-addressed render format, or zero. */
static uint32_t astra_display_read_pixel_bytes(uint32_t format)
{
    switch (format) {
    case ASTRA_RENDER_FORMAT_INDEX8:
    case ASTRA_RENDER_FORMAT_A8:
        return 1u;
    case ASTRA_RENDER_FORMAT_RGB565:
        return 2u;
    case ASTRA_RENDER_FORMAT_XRGB8888:
    case ASTRA_RENDER_FORMAT_ARGB8888:
        return 4u;
    default:
        return 0u;
    }
}

/*
 * Host-only READ_SURFACE: without a mailbox there is no arena, and every
 * surface the display service creates is cleared, so a consistent request
 * reads zeros. The helper performs the full arena validation.
 */
static uint32_t astra_display_read_host_only(Astra68State *s,
                                             const AstraDisplayRequest *read)
{
    uint8_t *buffer = s->sdram + (read->source - ASTRA_SDRAM_BASE);
    uint32_t size = ldl_be_p(buffer + 20u);
    uint32_t origin = ldl_be_p(buffer + 28u);
    uint32_t extent = ldl_be_p(buffer + 32u);
    uint32_t pixel = astra_display_read_pixel_bytes(ldl_be_p(buffer + 24u));
    uint64_t rows = (uint64_t)(extent >> 16) * pixel * (extent & 0xffffu);

    for (uint32_t offset = 36u;
         offset < ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES; offset += 4u)
        if (ldl_be_p(buffer + offset) != 0u)
            return ASTRA_DISPLAY_COMPLETION_BAD_REQUEST;
    if (ldl_be_p(buffer) != ASTRA_DISPLAY_SURFACE_READ_MAGIC ||
        ldl_be_p(buffer + 4u) != ASTRA_DISPLAY_SURFACE_READ_VERSION ||
        pixel == 0u || (extent >> 16) == 0u || (extent & 0xffffu) == 0u ||
        (origin >> 16) + (extent >> 16) > (size >> 16) ||
        (origin & 0xffffu) + (extent & 0xffffu) > (size & 0xffffu) ||
        ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES + rows != read->byte_size)
        return ASTRA_DISPLAY_COMPLETION_BAD_REQUEST;
    memset(buffer + ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES, 0,
           read->byte_size - ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES);
    return ASTRA_DISPLAY_COMPLETION_OK;
}

static void astra_display_service(void *opaque)
{
    Astra68State *s = opaque;
    AstraDisplayState *display = &s->display;
    const AstraDisplayRequest *running = astra_display_running(display);
    uint32_t status;

    if (running == NULL)
        return;
    if (!display->mailbox_enabled) {
        astra_display_complete(
            s,
            running->operation == ASTRA_DISPLAY_FRAME_READ_SURFACE ?
                astra_display_read_host_only(s, running) :
                ASTRA_DISPLAY_COMPLETION_OK,
            (uint32_t)display->generation + 1u);
        return;
    }
    if (qatomic_read(&display->mailbox->completion_sequence) !=
            display->mailbox_sequence) {
#ifndef CONFIG_LINUX
        timer_mod_ns(display->service_timer,
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT) +
                         DISPLAY_MAILBOX_POLL_NS);
#endif
        return;
    }
    smp_rmb();
    status = qatomic_read(&display->mailbox->completion_id) ==
                     running->id ?
                 qatomic_read(&display->mailbox->completion_status) :
                 ASTRA_DISPLAY_COMPLETION_BAD_REQUEST;
    /* The helper wrote the rows behind the header; return them to the
       guest buffer, which submit validated against guest RAM. */
    if (status == ASTRA_DISPLAY_COMPLETION_OK &&
        running->operation == ASTRA_DISPLAY_FRAME_READ_SURFACE)
        memcpy(s->sdram + (running->source - ASTRA_SDRAM_BASE) +
                   ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES,
               display->payload + ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES,
               running->byte_size - ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES);
    astra_display_complete(
        s, status, qatomic_read(&display->mailbox->completion_generation));
}

/* Rings the helper's request loop: wake_sequence is the word it sleeps on,
   so it is changed after whatever it announces. */
static void astra_display_wake_helper(AstraDisplayState *display)
{
    display->wake_sequence += 1u;
    qatomic_set(&display->mailbox->wake_sequence, display->wake_sequence);
#ifdef CONFIG_LINUX
    qemu_futex_wake((void *)&display->mailbox->wake_sequence, 1);
#endif
}

/*
 * DISPLAY_CURSOR: the posted cursor. It replaces the newest position and
 * flags and is done -- no request slot, no completion, no interrupt -- so a
 * moving pointer never waits behind rendering, nor rendering behind it. The
 * helper's cursor loop, a thread of its own, commits the newest word when
 * the pointer hardware next takes one.
 * A word naming no valid cursor is ignored.
 */
static void astra_display_post_cursor(Astra68State *s, uint32_t value)
{
    AstraDisplayState *display = &s->display;

    if (!ASTRA_DISPLAY_HOST_CURSOR_VALID(value))
        return;
    display->cursor = value;
    display->cursor_x = ASTRA_DISPLAY_HOST_CURSOR_X(value);
    display->cursor_y = ASTRA_DISPLAY_HOST_CURSOR_Y(value);
    display->cursor_visible = (ASTRA_DISPLAY_HOST_CURSOR_FLAGS(value) &
                               ASTRA_DISPLAY_CURSOR_VISIBLE) != 0u;
    ++display->cursor_updates;
    if (!display->mailbox_enabled)
        return;
    display->cursor_sequence += 1u;
    qatomic_set(&display->mailbox->magic, ASTRA_DISPLAY_MAILBOX_MAGIC);
    qatomic_set(&display->mailbox->version,
                ASTRA_DISPLAY_MAILBOX_VERSION_1_9);
    qatomic_set(&display->mailbox->cursor, value);
    smp_wmb();
    qatomic_set(&display->mailbox->cursor_sequence,
                display->cursor_sequence);
#ifdef CONFIG_LINUX
    /* The helper's cursor loop sleeps on cursor_sequence itself. */
    qemu_futex_wake((void *)&display->mailbox->cursor_sequence, 1);
#endif
}

/*
 * Hands the running request to the helper, or, without one, to the service
 * timer. The batch is read from guest RAM now: the guest holds its DMA
 * buffer until the completion. An attachment comes from guest RAM when the
 * request starts as it is accepted, and from its staged copy otherwise.
 */
static void astra_display_start(Astra68State *s)
{
    AstraDisplayState *display = &s->display;
    AstraDisplayRequest *request = astra_display_running(display);
    uint32_t operation = request->operation;

    display->operation = operation;
    if (display->mailbox_enabled) {
        qatomic_set(&display->mailbox_sequence,
                    display->mailbox_sequence + 1u == 0u ?
                        1u : display->mailbox_sequence + 1u);
        qatomic_set(&display->mailbox->magic, ASTRA_DISPLAY_MAILBOX_MAGIC);
        qatomic_set(&display->mailbox->version,
                    ASTRA_DISPLAY_MAILBOX_VERSION_1_9);
        qatomic_set(&display->mailbox->request_id, request->id);
        qatomic_set(&display->mailbox->operation, operation);
        qatomic_set(&display->mailbox->color_rgb565, request->source);
        qatomic_set(&display->mailbox->frame_pitch,
                    operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ?
                        ASTRA_DISPLAY_WIDTH * 2u : 0u);
        qatomic_set(&display->mailbox->frame_bytes,
                    operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ?
                        ASTRA_DISPLAY_MAILBOX_FRAME_BYTES :
                    operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH ||
                    operation == ASTRA_DISPLAY_FRAME_READ_SURFACE ||
                    operation == ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE ?
                        request->byte_size : 0u);
        if (operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ||
            operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH ||
            operation == ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE ||
            operation == ASTRA_DISPLAY_FRAME_READ_SURFACE) {
            memcpy(display->payload,
                   s->sdram + (request->source - ASTRA_SDRAM_BASE),
                   operation == ASTRA_DISPLAY_FRAME_PRESENT_RGB565 ?
                       ASTRA_DISPLAY_MAILBOX_FRAME_BYTES :
                       request->source_bytes);
            if (request->staged_valid)
                memcpy(display->payload + request->attachment_target,
                       request->staged, request->attachment_bytes);
            else if (request->attachment != 0u)
                astra_display_attachment_copy(
                    s, request->attachment,
                    display->payload + request->attachment_target);
        }
#if defined(__aarch64__)
        /* The payload may be Normal non-cacheable memory the FPGA reads
           over F2SDRAM, outside the CPUs' shareability domain: the stores
           must be complete, not just ordered, before the helper can see the
           request and ring the engine. */
        asm volatile("dsb st" ::: "memory");
#else
        smp_wmb();
#endif
        qatomic_set(&display->mailbox->request_sequence,
                    display->mailbox_sequence);
        astra_display_wake_helper(display);
#ifdef CONFIG_LINUX
        qemu_event_set(&display->completion_armed);
        return;
#endif
    }
    timer_mod_ns(display->service_timer,
                 qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL_RT));
}

/* DISPLAY_REQ_SUBMIT: accepts the staged request into a free slot, or
   ignores it -- the guest sees ACCEPTED unchanged. */
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
    uint32_t attachment = display->request_attachment;
    uint32_t source_bytes = byte_size;
    AstraDisplayRequest *request;
    bool running;

    if (astra_display_held(display) >= ASTRA_DISPLAY_HOST_QUEUE_DEPTH ||
        display->request_id == 0u ||
        (operation != ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         operation != ASTRA_DISPLAY_FRAME_PRESENT_RGB565 &&
         operation != ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
         operation != ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE &&
         operation != ASTRA_DISPLAY_FRAME_READ_SURFACE) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         (display->request_source & 0xffff0000u) != 0u) ||
        (operation == ASTRA_DISPLAY_FRAME_READ_SURFACE &&
         (byte_size <= ASTRA_DISPLAY_SURFACE_READ_HEADER_BYTES ||
          byte_size > ASTRA_RENDER_BATCH_MAX_BYTES)) ||
        (operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH &&
         (byte_size < ASTRA_RENDER_BATCH_MIN_BYTES ||
          byte_size > ASTRA_RENDER_BATCH_MAX_BYTES)) ||
        (operation == ASTRA_DISPLAY_CURSOR_IMAGE_UPDATE &&
         byte_size != ASTRA_DISPLAY_CURSOR_IMAGE_BYTES) ||
        (operation != ASTRA_DISPLAY_FRAME_PRESENT_SOLID &&
         (display->request_source < ASTRA_SDRAM_BASE ||
          frame_end > (uint64_t)ASTRA_SDRAM_BASE + s->ram_size)) ||
        (attachment != 0u &&
         (operation != ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH ||
          (source_bytes = astra_display_attachment_valid(
               s, attachment, byte_size)) == 0u)))
        return;
    request = &display->requests[
        (display->request_head + display->request_count) %
        ASTRA_DISPLAY_HOST_QUEUE_DEPTH];
    request->id = display->request_id;
    request->operation = operation;
    request->source = display->request_source;
    request->byte_size = byte_size;
    request->source_bytes = source_bytes;
    request->attachment = attachment;
    request->attachment_target = attachment != 0u ? source_bytes : 0u;
    request->attachment_bytes = attachment != 0u ?
        ldl_be_p(s->sdram + (attachment - ASTRA_SDRAM_BASE) + 12u) : 0u;
    request->staged_valid = false;
    display->accepted = (display->accepted + 1u) & 0xffu;
    if (operation == ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH)
        astra_display_count_batch(s, request->source, byte_size);
    if (operation == ASTRA_DISPLAY_FRAME_READ_SURFACE)
        ++display->surface_reads;
    display->submit_cycle = astra_now_cycles(s);
    ++display->submissions;
    running = display->request_count != 0u;
    ++display->request_count;
    if (!running) {
        astra_display_start(s);
        return;
    }
    ++display->queued_submissions;
    if (display->mailbox_enabled && attachment != 0u)
        astra_display_attachment_stage(s, request);
}

#ifdef CONFIG_LINUX
/* Sleeps until the helper publishes the completion of the newest request,
   then hands it to the main loop. Submit arms it; a request that is never
   completed (a panic text) is superseded by the next submit, whose
   completion wakes the futex again. */
static void *astra_display_completion_thread(void *opaque)
{
    AstraDisplayState *display = opaque;
    uint32_t notified = 0u;

    for (;;) {
        uint32_t expected;
        uint32_t seen;

        qemu_event_reset(&display->completion_armed);
        expected = qatomic_read(&display->mailbox_sequence);
        if (expected == notified) {
            qemu_event_wait(&display->completion_armed);
            continue;
        }
        seen = qatomic_read(&display->mailbox->completion_sequence);
        if (seen != expected) {
            qemu_futex_wait((void *)&display->mailbox->completion_sequence,
                            seen);
            continue;
        }
        notified = expected;
        event_notifier_set(&display->completion_notifier);
    }
    return NULL;
}

static void astra_display_completion_ready(EventNotifier *notifier)
{
    Astra68State *s = container_of(notifier, Astra68State,
                                   display.completion_notifier);

    if (event_notifier_test_and_clear(notifier))
        astra_display_service(s);
}
#endif

static void astra_display_reset(Astra68State *s)
{
    AstraDisplayState *display = &s->display;

    if (display->service_timer)
        timer_del(display->service_timer);
    display->request_head = 0u;
    display->request_count = 0u;
    display->completion_head = 0u;
    display->completion_count = 0u;
    for (uint32_t slot = 0u; slot < ASTRA_DISPLAY_HOST_QUEUE_DEPTH; ++slot)
        display->requests[slot].staged_valid = false;
    display->request_id = 0u;
    display->request_op = 0u;
    display->request_source = 0u;
    display->request_attachment = 0u;
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
    qatomic_set(&display->mailbox_sequence,
                display->mailbox_sequence + 1u == 0u ?
                    1u : display->mailbox_sequence + 1u);
    qatomic_set(&display->mailbox->magic, ASTRA_DISPLAY_MAILBOX_MAGIC);
    qatomic_set(&display->mailbox->version,
                ASTRA_DISPLAY_MAILBOX_VERSION_1_9);
    qatomic_set(&display->mailbox->request_id, UINT32_MAX);
    qatomic_set(&display->mailbox->operation, ASTRA_DISPLAY_PANIC_TEXT);
    qatomic_set(&display->mailbox->color_rgb565, 0u);
    qatomic_set(&display->mailbox->frame_pitch, 0u);
    qatomic_set(&display->mailbox->frame_bytes, 0u);
    smp_wmb();
    qatomic_set(&display->mailbox->request_sequence,
                display->mailbox_sequence);
    astra_display_wake_helper(display);
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

/*
 * Pointer motion is a latest value, as a mouse's counters are (the Amiga's
 * JOYxDAT, read once a frame) and as Haiku's shared cursor is: a motion
 * record nobody has read yet absorbs the next one on its axis -- deltas
 * add, positions replace -- and a queue that holds only motion interrupts
 * once a frame (astra_vblank). Buttons and keys interrupt at once, behind
 * the motion queued before them. A mouse reporting at 1000 Hz then costs
 * the machine one input wake-up a frame, not a thousand.
 */
static bool astra_input_is_motion(uint32_t header)
{
    uint32_t kind = (header >> 16) & 0xffu;

    return (header >> 24) == ASTRA_INPUT_CLASS_POINTER &&
           (kind == ASTRA_INPUT_POINTER_RELATIVE ||
            kind == ASTRA_INPUT_POINTER_ABSOLUTE);
}

/* The head record is never merged into: the guest may be reading it. */
static bool astra_input_coalesce(AstraInputState *input, uint32_t header,
                                 uint32_t value, uint32_t device_sequence)
{
    uint8_t at = input->tail;

    while (at != input->head) {
        AstraInputEvent *event;

        at = (at - 1u) & ASTRA_INPUT_QUEUE_MASK;
        event = &input->queue[at];
        if (at == input->head || !astra_input_is_motion(event->header))
            return false;
        if (event->header == header &&
            (event->device_sequence >> 16) == (device_sequence >> 16)) {
            event->value = ((header >> 16) & 0xffu) ==
                               ASTRA_INPUT_POINTER_RELATIVE ?
                (uint32_t)((int32_t)event->value + (int32_t)value) : value;
            event->timestamp_ms = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL);
            event->device_sequence = device_sequence;
            ++input->coalesced;
            return true;
        }
    }
    return false;
}

/* Interrupt for anything but motion at once, and for motion once the frame
   has released it. */
static bool astra_input_irq_due(const AstraInputState *input)
{
    for (uint8_t at = input->head; at != input->tail;
         at = (at + 1u) & ASTRA_INPUT_QUEUE_MASK)
        if (input->motion_released ||
            !astra_input_is_motion(input->queue[at].header))
            return true;
    return false;
}

static bool astra_input_push(Astra68State *s, uint8_t event_class,
                             uint8_t kind, uint16_t flags, uint32_t value,
                             uint16_t device, uint16_t *sequence)
{
    AstraInputState *input = &s->input;
    uint8_t next = (input->tail + 1u) & ASTRA_INPUT_QUEUE_MASK;
    uint32_t header = ((uint32_t)event_class << 24) |
                      ((uint32_t)kind << 16) | flags;
    AstraInputEvent *event;

    if (astra_input_is_motion(header) &&
        astra_input_coalesce(input, header, value,
                             (uint32_t)device << 16 |
                                 (uint16_t)(*sequence + 1u))) {
        ++*sequence;
        return true;
    }

    if (next == input->head) {
        input->overflow = true;
        input->dropped++;
        astra_update_irq(s);
        return false;
    }
    ++*sequence;
    event = &input->queue[input->tail];
    event->header = header;
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

            qatomic_inc(&block->durability_transitions);
            if (qatomic_read(&block->durability_transitions) ==
                block->cut_after_transition) {
                error_report("Astra68 block power cut: transition=%" PRIu64
                             " op=%s",
                             qatomic_read(&block->durability_transitions), op);
                fflush(stderr);
                _exit(86);
            } else if (block->trace_durability) {
                error_report("Astra68 block durability transition=%" PRIu64
                             " op=%s",
                             qatomic_read(&block->durability_transitions), op);
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
        qatomic_inc(&block->read_requests);
        qatomic_add(&block->read_sectors, request->sectors);
        qemu_iovec_init_buf(&request->qiov, buffer, bytes);
        request->qiov_initialized = true;
        request->aiocb = blk_aio_preadv(
            block->blk, (int64_t)request->lba * BLOCK_SECTOR_SIZE,
            &request->qiov, 0, astra_block_complete, request);
        break;
    case BLOCK_OP_WRITE:
        qatomic_inc(&block->write_requests);
        qatomic_add(&block->write_sectors, request->sectors);
        qemu_iovec_init_buf(&request->qiov, buffer, bytes);
        request->qiov_initialized = true;
        request->aiocb = blk_aio_pwritev(
            block->blk, (int64_t)request->lba * BLOCK_SECTOR_SIZE,
            &request->qiov, 0, astra_block_complete, request);
        break;
    default:
        qatomic_inc(&block->flush_requests);
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
                     qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
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

    if (job->reverse) {
        char hostname[NI_MAXHOST];

        status = getnameinfo((const struct sockaddr *)&job->reverse_address,
                             job->reverse_length, hostname, sizeof(hostname),
                             NULL, 0, NI_NAMEREQD);
        if (status == 0)
            job->name = g_strdup(hostname);
        job->resolver_status = status;
        return 0;
    }

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
    bool reverse = lduw_be_p(command + 6) ==
                   ASTRA_NETWORK_HOST_REVERSE_RESOLVE;
    uint32_t token = ldl_be_p(command + 24);
    uint32_t data_offset = ldl_be_p(command + 56);
    uint32_t data_length = ldl_be_p(command + 60);
    uint32_t data_capacity = ldl_be_p(command + 64);
    uint8_t *data;

    if (token == 0) {
        AstraNetworkResolveJob *job;

        job = g_new0(AstraNetworkResolveJob, 1);
        job->machine = s;
        job->reverse = reverse;
        if (reverse) {
            if (data_length != 0u ||
                !astra_network_guest_address(command + 28,
                                             &job->reverse_address,
                                             &job->reverse_length)) {
                g_free(job);
                stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
                return;
            }
        } else {
            data = astra_dma_data(s, physical, bytes, data_offset,
                                  data_length);
            if (data == NULL || data_length == 0 ||
                data_length > ASTRA_NETWORK_NAME_MAX ||
                memchr(data, 0, data_length) != NULL) {
                g_free(job);
                stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
                return;
            }
            job->name = g_strndup((const char *)data, data_length);
            job->addresses = g_array_new(false, false,
                                         sizeof(AstraNetworkAddress));
        }
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

        if (job == NULL || job->reverse != reverse ||
            job->host_generation != s->network.host_generation) {
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
        if (reverse) {
            count = strlen(job->name) + 1u;
            stl_be_p(command + 80, count);
            if (data_capacity < count) {
                stl_be_p(command + 68, ASTRA_NETWORK_BUFFER_TOO_SMALL);
                return;
            }
            data = astra_dma_data(s, physical, bytes, data_offset, count);
            if (data == NULL) {
                stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
                return;
            }
            memcpy(data, job->name, count);
            stl_be_p(command + 68, ASTRA_NETWORK_OK);
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

static void astra_network_execute_interfaces(Astra68State *s,
                                             uint8_t *command,
                                             uint32_t physical,
                                             uint32_t bytes)
{
#ifdef CONFIG_POSIX
    struct ifaddrs *head = NULL, *current;
    uint32_t data_offset = ldl_be_p(command + 56);
    uint32_t data_capacity = ldl_be_p(command + 64);
    uint32_t count = 0u;
    uint8_t *data;

    if (getifaddrs(&head) != 0) {
        stl_be_p(command + 68, astra_network_status_from_errno(errno));
        return;
    }
    for (current = head; current != NULL; current = current->ifa_next) {
        if (current->ifa_addr == NULL || current->ifa_name == NULL ||
            (current->ifa_addr->sa_family != AF_INET &&
             current->ifa_addr->sa_family != AF_INET6))
            continue;
        if (strlen(current->ifa_name) >=
                ASTRA_NETWORK_INTERFACE_NAME_SIZE ||
            count >= UINT32_MAX / ASTRA_NETWORK_INTERFACE_ADDRESS_SIZE) {
            stl_be_p(command + 68, ASTRA_NETWORK_RESOURCE_LIMIT);
            freeifaddrs(head);
            return;
        }
        ++count;
    }
    stl_be_p(command + 80, count);
    if (count * ASTRA_NETWORK_INTERFACE_ADDRESS_SIZE > data_capacity) {
        stl_be_p(command + 68, ASTRA_NETWORK_BUFFER_TOO_SMALL);
        freeifaddrs(head);
        return;
    }
    if (count == 0u) {
        stl_be_p(command + 68, ASTRA_NETWORK_OK);
        freeifaddrs(head);
        return;
    }
    data = astra_dma_data(s, physical, bytes, data_offset,
                          count * ASTRA_NETWORK_INTERFACE_ADDRESS_SIZE);
    if (data == NULL) {
        stl_be_p(command + 68, ASTRA_NETWORK_INVALID);
        freeifaddrs(head);
        return;
    }
    for (current = head; current != NULL; current = current->ifa_next) {
        socklen_t length;

        if (current->ifa_addr == NULL || current->ifa_name == NULL ||
            (current->ifa_addr->sa_family != AF_INET &&
             current->ifa_addr->sa_family != AF_INET6))
            continue;
        length = current->ifa_addr->sa_family == AF_INET ?
            sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
        memset(data, 0, ASTRA_NETWORK_INTERFACE_ADDRESS_SIZE);
        if (!astra_network_store_address(data, current->ifa_addr, length)) {
            stl_be_p(command + 68, ASTRA_NETWORK_IO);
            freeifaddrs(head);
            return;
        }
        memcpy(data + ASTRA_NETWORK_ADDRESS_SIZE, current->ifa_name,
               strlen(current->ifa_name) + 1u);
        data += ASTRA_NETWORK_INTERFACE_ADDRESS_SIZE;
    }
    stl_be_p(command + 68, ASTRA_NETWORK_OK);
    freeifaddrs(head);
#else
    (void)s;
    (void)physical;
    (void)bytes;
    stl_be_p(command + 68, ASTRA_NETWORK_UNSUPPORTED);
#endif
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
    if (operation == ASTRA_NETWORK_HOST_RESOLVE ||
        operation == ASTRA_NETWORK_HOST_REVERSE_RESOLVE) {
        astra_network_execute_resolve(s, command, physical, bytes);
        return;
    }
    if (operation == ASTRA_NETWORK_HOST_INTERFACES) {
        astra_network_execute_interfaces(s, command, physical, bytes);
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

static void astra_host_usage_changed_locked(Astra68State *s)
{
    if (s->host.usage_sequence != UINT64_MAX) ++s->host.usage_sequence;
}

static void astra_host_job_session(Astra68State *s, AstraHostJob *job,
                                    AstraHostSession *session)
{
    if (job == NULL) return;
    qemu_mutex_lock(&s->host.files_lock);
    if (job->session != session) {
        job->session = session;
        astra_host_usage_changed_locked(s);
    }
    qemu_mutex_unlock(&s->host.files_lock);
}

static void astra_host_description_release(AstraHostDescription *description)
{
    if (description != NULL && g_atomic_int_dec_and_test(&description->references)) {
        if (description->directory != NULL)
            closedir(description->directory);
        else
            close(description->fd);
        qemu_mutex_destroy(&description->lock);
        if (description->charged) {
            AstraHostState *host = &description->machine->host;
            qemu_mutex_lock(&host->files_lock);
            assert(host->fs_descriptors != 0u);
            --host->fs_descriptors;
            if (description->directory != NULL) {
                assert(host->fs_directories != 0u);
                --host->fs_directories;
            }
            qemu_mutex_unlock(&host->files_lock);
        }
        g_free(description);
    }
}

static void astra_host_release_shares(Astra68State *s, uint32_t owner,
                                      uint32_t session)
{
    GHashTableIter iterator;
    gpointer key, value;
    GPtrArray *released = g_ptr_array_new();

    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.shares);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostShare *share = value;

        if ((owner != 0u && share->owner != owner) ||
            (session != 0u && share->session != session))
            continue;
        g_hash_table_iter_steal(&iterator);
        astra_host_usage_changed_locked(s);
        g_ptr_array_add(released, share);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    for (guint index = 0; index < released->len; ++index) {
        AstraHostShare *share = g_ptr_array_index(released, index);

        astra_host_description_release(share->description);
        g_free(share);
    }
    g_ptr_array_free(released, true);
}

static void astra_host_file_free(gpointer opaque)
{
    AstraHostFile *file = opaque;

    if (file != NULL) {
        astra_host_description_release(file->description);
        g_free(file);
    }
}

static void astra_host_file_release(Astra68State *s, AstraHostFile *file)
{
    bool free_file;

    qemu_mutex_lock(&s->host.files_lock);
    assert(file->references != 0);
    --file->references;
    astra_host_usage_changed_locked(s);
    free_file = file->references == 0;
    qemu_mutex_unlock(&s->host.files_lock);
    if (free_file) {
        astra_host_file_free(file);
    }
}

/* Takes every handle of `owner`, or every handle when owner is 0, out of
 * the table. The caller holds files_lock; astra_host_free_files closes
 * them once it is dropped. */
static GPtrArray *astra_host_steal_files(Astra68State *s, uint32_t owner,
                                        bool disconnected)
{
    GHashTableIter iterator;
    gpointer key;
    gpointer value;
    GPtrArray *released = g_ptr_array_new();

    g_hash_table_iter_init(&iterator, s->host.files);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostFile *file = value;

        if (owner != 0 && file->owner != owner) {
            continue;
        }
        if (disconnected) {
            AstraHostFile *lost = g_new0(AstraHostFile, 1);
            lost->owner = file->owner;
            lost->process = file->process;
            lost->session = file->session;
            lost->identity = file->identity;
            g_hash_table_insert(s->host.lost_handles, key, lost);
            astra_host_usage_changed_locked(s);
        }
        file->closing = true;
        g_hash_table_iter_steal(&iterator);
        astra_host_usage_changed_locked(s);
        g_ptr_array_add(released, file);
    }
    return released;
}

static void astra_host_free_files(Astra68State *s, GPtrArray *released)
{
    for (guint index = 0; index < released->len; ++index) {
        astra_host_file_release(s, g_ptr_array_index(released, index));
    }
    g_ptr_array_free(released, true);
}

static void astra_host_release_files(Astra68State *s, uint32_t owner)
{
    GPtrArray *released;

    qemu_mutex_lock(&s->host.files_lock);
    released = astra_host_steal_files(s, owner, false);
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_free_files(s, released);
    astra_host_release_shares(s, owner, 0u);
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
        astra_host_usage_changed_locked(s);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    return file;
}

/* `handle`, locked, when it belongs to `session_id`; NULL otherwise. A
 * handle opened in one session is not usable in another, even by the same
 * owner (FS44). */
static AstraHostFile *astra_host_session_file(Astra68State *s,
                                              uint32_t owner,
                                              uint32_t session_id,
                                              uint32_t handle)
{
    AstraHostFile *file = astra_host_file_acquire(s, owner, handle);

    if (file != NULL && file->session != session_id) {
        astra_host_file_release(s, file);
        file = NULL;
    }
    if (file != NULL) {
        qemu_mutex_lock(&file->description->lock);
    }
    return file;
}

static void astra_host_file_unlock(Astra68State *s, AstraHostFile *file)
{
    qemu_mutex_unlock(&file->description->lock);
    astra_host_file_release(s, file);
}

static AstraHostSession *astra_host_session_acquire(Astra68State *s,
                                                    uint32_t owner,
                                                    uint32_t id)
{
    AstraHostSession *session;

    if (id == 0u)
        return NULL;
    qemu_mutex_lock(&s->host.files_lock);
    session = g_hash_table_lookup(s->host.sessions, GUINT_TO_POINTER(id));
    if (session == NULL || session->owner != owner || session->closing)
        session = NULL;
    else
        ++session->references;
    qemu_mutex_unlock(&s->host.files_lock);
    return session;
}

static void astra_host_session_release(Astra68State *s,
                                       AstraHostSession *session)
{
    bool last;

    qemu_mutex_lock(&s->host.files_lock);
    assert(session->references != 0u);
    last = --session->references == 0u;
    qemu_mutex_unlock(&s->host.files_lock);
    if (last) {
        if (session->root >= 0) {
            close(session->root);
            qemu_mutex_lock(&s->host.files_lock);
            assert(s->host.fs_descriptors != 0u);
            --s->host.fs_descriptors;
            qemu_mutex_unlock(&s->host.files_lock);
        }
        g_free(session);
    }
}

static bool astra_host_session_live(Astra68State *s, uint32_t owner,
                                    uint32_t id)
{
    AstraHostSession *session = astra_host_session_acquire(s, owner, id);

    if (session == NULL)
        return false;
    astra_host_session_release(s, session);
    return true;
}

/* Closes every handle tagged with `id`, of every owner: a session's
 * handles go with it, whoever opened them. */
static void astra_host_release_session_files(Astra68State *s, uint32_t id)
{
    GHashTableIter iterator;
    gpointer key;
    gpointer value;
    GPtrArray *released = g_ptr_array_new();

    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.files);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostFile *file = value;

        if (file->session != id || file->closing)
            continue;
        AstraHostFile *lost = g_new0(AstraHostFile, 1);
        lost->owner = file->owner;
        lost->session = file->session;
        lost->identity = file->identity;
        g_hash_table_insert(s->host.lost_handles, key, lost);
        astra_host_usage_changed_locked(s);
        file->closing = true;
        g_hash_table_iter_steal(&iterator);
        astra_host_usage_changed_locked(s);
        g_ptr_array_add(released, file);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    for (guint index = 0u; index < released->len; ++index)
        astra_host_file_release(s, g_ptr_array_index(released, index));
    g_ptr_array_free(released, true);
    astra_host_release_shares(s, 0u, id);
}

/* Closes every session of `owner`, or every session when owner is 0, and
 * every handle opened in each. */
typedef struct AstraHostStolenSessions {
    GPtrArray *sessions;
    GArray *ids;
} AstraHostStolenSessions;

/* Takes every session of `owner`, or every session when owner is 0, out of
 * the table and marks it closing. The caller holds files_lock, so a claim
 * that clears fs_owner in the same critical section takes exactly the
 * sessions opened under it -- never those of a re-claim that races the
 * sweep (Task 4 re-review N1). Session ids are never reused, so the files
 * swept by id afterwards are only these sessions' own. */
static AstraHostStolenSessions astra_host_steal_sessions(Astra68State *s,
                                                         uint32_t owner,
                                                         uint64_t mount,
                                                         uint64_t generation)
{
    AstraHostStolenSessions stolen = {
        g_ptr_array_new(), g_array_new(false, false, sizeof(uint32_t))
    };
    GHashTableIter iterator;
    gpointer key;
    gpointer value;

    g_hash_table_iter_init(&iterator, s->host.sessions);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostSession *session = value;
        uint32_t id = GPOINTER_TO_UINT(key);

        if (owner != 0u && session->owner != owner)
            continue;
        if (mount != 0u && (session->mount != mount ||
                           session->mount_generation != generation))
            continue;
        session->closing = true;
        g_hash_table_iter_steal(&iterator);
        astra_host_usage_changed_locked(s);
        g_ptr_array_add(stolen.sessions, session);
        g_array_append_val(stolen.ids, id);
    }
    return stolen;
}

/* Closes stolen sessions and every handle opened in each. */
static void astra_host_free_sessions(Astra68State *s,
                                     AstraHostStolenSessions stolen)
{
    for (guint index = 0u; index < stolen.sessions->len; ++index) {
        astra_host_release_session_files(s, g_array_index(stolen.ids,
                                                          uint32_t, index));
        astra_host_session_release(s,
                                   g_ptr_array_index(stolen.sessions, index));
    }
    g_ptr_array_free(stolen.sessions, true);
    g_array_free(stolen.ids, true);
}

static void astra_host_release_sessions(Astra68State *s, uint32_t owner)
{
    AstraHostStolenSessions stolen;

    qemu_mutex_lock(&s->host.files_lock);
    stolen = astra_host_steal_sessions(s, owner, 0u, 0u);
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_free_sessions(s, stolen);
}

static void astra_host_channel_drain(Astra68State *s, AstraHostChannel *channel)
{
    qemu_mutex_lock(&s->host.files_lock);
    channel->active = false;
    qemu_mutex_unlock(&s->host.files_lock);
    channel->completion_pending = false;
    while (channel->jobs != 0) {
        aio_poll(qemu_get_aio_context(), true);
    }
    if (channel->remote_desktop_fd >= 0)
        close(channel->remote_desktop_fd);
    if (channel->audio_fd >= 0)
        close(channel->audio_fd);
    g_free(channel->completed);
    qemu_mutex_lock(&s->host.files_lock);
    memset(channel, 0, sizeof(*channel));
    channel->remote_desktop_fd = -1;
    channel->audio_fd = -1;
    qemu_mutex_unlock(&s->host.files_lock);
}

static void astra_host_refresh_completion(Astra68State *s)
{
    s->host.completion_pending = s->audio.completion_pending;
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
            astra_host_channel_drain(s, &s->host.channels[slot]);
        }
    }
    for (uint32_t slot = 0; slot < ASTRA_AUDIO_MAILBOX_STREAMS; ++slot) {
        if (s->audio.streams[slot].active &&
            s->audio.streams[slot].owner == owner) {
            astra_audio_stream_stop(s, slot);
        }
    }
    astra_host_refresh_completion(s);
    astra_host_release_files(s, owner);
    /* The claimed owner gone: its sessions, every handle opened in them
     * and the claim go with it, so a restarted service starts clean and
     * no handle of the old sessions answers again (review M2). */
    qemu_mutex_lock(&s->host.files_lock);
    if (qatomic_read(&s->host.fs_owner) == owner)
        qatomic_set(&s->host.fs_owner, 0u);
    if (qatomic_read(&s->host.fs_lost_owner) == owner)
        qatomic_set(&s->host.fs_lost_owner, 0u);
    GHashTableIter iterator;
    gpointer value;
    g_hash_table_iter_init(&iterator, s->host.lost_handles);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        if (((AstraHostFile *)value)->owner == owner) {
            g_hash_table_iter_remove(&iterator);
            astra_host_usage_changed_locked(s);
        }
    }
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_release_sessions(s, owner);
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
    case EINVAL: return ASTRA_STATUS_INVALID;
    case ENAMETOOLONG: return ASTRA_STATUS_NAME_TOO_LONG;
    case ELOOP: return ASTRA_STATUS_LOOP;
    case EAGAIN:
    case EBUSY: return ASTRA_STATUS_BUSY;
    case ESTALE:
    case ENODEV: return ASTRA_STATUS_LOST;
    case EBADF: return ASTRA_STATUS_BAD_HANDLE;
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

static bool astra_host_fs_path_valid(int base, const char *path)
{
    return path != NULL && strlen(path) < ASTRA_HOST_FS_PATH_MAX &&
           (base < 0 ? path[0] == '/' : path[0] != '/');
}

/* Rooted opens/mutations execute once in the confined Linux provider. QEMU
 * owns the resulting descriptions, positions, shares and data I/O. */
static int astra_host_provider_path(Astra68State *s, const AstraHostSession *session,
                                    int root, int base, int second,
                                    const char *path, const char *path2,
                                    uint32_t operation, uint32_t flags, uint32_t mode,
                                    uint32_t resolve, uint32_t links, uint32_t capacity,
                                    AstraStoragePathReply *reply, int *result)
{
    AstraStoragePathRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = operation, .epoch = s->host.provider_epoch,
        .mount = session->mount, .generation = session->mount_generation,
        .fd_mask = 1u | (base >= 0 ? 2u : 0u) | (second >= 0 ? 4u : 0u),
        .flags = flags, .mode = mode, .resolve = resolve, .links = links,
        .capacity = capacity};
    int fds[3] = {root, base, second};
    int error;
    *result = -1;
    memset(reply, 0, sizeof(*reply));
    if (strlen(path) >= sizeof(request.path) || strlen(path2) >= sizeof(request.path2))
        return ENAMETOOLONG;
    strcpy(request.path, path);
    strcpy(request.path2, path2);
    qemu_mutex_lock(&s->host.provider_path_lock);
    if (qatomic_read(&s->host.provider_broken))
        error = ENODEV;
    else if (s->host.provider_path_serial == UINT64_MAX)
        error = EOVERFLOW;
    else {
        request.serial = ++s->host.provider_path_serial;
        error = astra_storage_path_call(s->host.provider_path, &request, fds, reply, result);
        if (error != 0)
            qatomic_set(&s->host.provider_broken, true);
        else
            error = reply->error;
    }
    qemu_mutex_unlock(&s->host.provider_path_lock);
    return error;
}

static void astra_host_resolution(const AstraStoragePathReply *reply,
                                  AstraResolution *resolved)
{
    *resolved = (AstraResolution){.parent = -1, .trailing = reply->trailing,
        .missing = reply->missing, .escape = reply->escape,
        .consumed = reply->consumed, .links = reply->links};
    memcpy(resolved->leaf, reply->leaf, sizeof(resolved->leaf));
    memcpy(resolved->continuation, reply->continuation, sizeof(resolved->continuation));
}

static uint32_t astra_host_path_status(int error,
                                      const AstraResolution *resolved)
{
    return error == EXDEV && resolved->escape != 0u ? ASTRA_STATUS_ESCAPED :
           astra_host_status_from_errno(error);
}

static int astra_host_open_resolved(Astra68State *s, const AstraHostSession *session,
                                    int root, int base, const char *path,
                                    int flags, mode_t mode, unsigned resolve,
                                    uint32_t links_left, AstraResolution *resolved)
{
    AstraStoragePathReply reply;
    int fd;
    if (!astra_host_fs_path_valid(base, path)) {
        errno = EINVAL;
        return -1;
    }
    int error = astra_host_provider_path(s, session, root, base, -1, path, "",
        ASTRA_STORAGE_PATH_OPEN, flags, mode, resolve, links_left, 0u, &reply, &fd);
    astra_host_resolution(&reply, resolved);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return fd;
}

static int astra_host_rooted(Astra68State *s, const AstraHostSession *session,
                             int root, int fd)
{
    AstraStoragePathReply reply;
    int result;
    return -astra_host_provider_path(s, session, root, fd, -1, "", "",
        ASTRA_STORAGE_PATH_ROOTED, 0u, 0u, 0u, 0u, 0u, &reply, &result);
}

static int astra_host_descriptor_path(Astra68State *s, const AstraHostSession *session,
                                     int root, int fd, char *path,
                                     uint32_t capacity, uint32_t *length)
{
    AstraStoragePathReply reply;
    int result;
    int error = astra_host_provider_path(s, session, root, fd, -1, "", "",
        ASTRA_STORAGE_PATH_NAME, 0u, 0u, 0u, 0u,
        MIN(capacity, ASTRA_STORAGE_PROVIDER_PATH_MAX), &reply, &result);
    if (error == 0) {
        memcpy(path, reply.data, reply.length);
        *length = reply.length;
    }
    return -error;
}

static int astra_host_provider_endpoint(const char *name)
{
    const char *text = g_getenv(name);
    char *end;
    struct ucred peer;
    socklen_t bytes = sizeof(peer);
    if (text == NULL || text[0] < '0' || text[0] > '9')
        return -EINVAL;
    errno = 0;
    long fd = strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || fd < 3 || fd > INT_MAX)
        return -EINVAL;
    int type = 0;
    socklen_t type_bytes = sizeof(type);
    if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_bytes) != 0 ||
        type != SOCK_SEQPACKET ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &bytes) != 0 ||
        bytes != sizeof(peer) || peer.pid != getppid() || peer.uid != 0u || peer.gid != 0u)
        return -EACCES;
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0)
        return -errno;
    return (int)fd;
}

static void astra_host_provider_init(Astra68State *s)
{
    const char *control = g_getenv("ASTRA_STORAGE_CONTROL_FD");
    const char *path = g_getenv("ASTRA_STORAGE_PATH_FD");
    if (g_getenv("ASTRA_HOSTFS_ROOT") != NULL) {
        error_report("ASTRA_HOSTFS_ROOT was replaced by the confined storage provider");
        exit(EXIT_FAILURE);
    }
    if (control == NULL && path == NULL)
        return;
    int a = astra_host_provider_endpoint("ASTRA_STORAGE_CONTROL_FD");
    int b = astra_host_provider_endpoint("ASTRA_STORAGE_PATH_FD");
    if (a < 0 || b < 0 || a == b || getuid() == 0 || geteuid() == 0) {
        error_report("Astra storage requires distinct inherited provider sockets and an unprivileged QEMU");
        exit(EXIT_FAILURE);
    }
    s->host.provider_control = a;
    s->host.provider_path = b;
    AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_SNAPSHOT, .serial = ++s->host.provider_control_serial};
    AstraStorageControlReply reply;
    int fd;
    if (astra_storage_control_call(a, &request, -1, &reply, &fd) != 0 || reply.error != 0)
        goto failed;
    s->host.provider_epoch = reply.epoch;
    return;
failed:
    error_report("cannot acquire coherent storage provider roots");
    exit(EXIT_FAILURE);
}

#define NODE_FIELD(field) (HOST_FIELD(node) + offsetof(AstraHostNode, field))

static uint16_t astra_host_kind(uint32_t mode)
{
    return S_ISREG(mode) ? ASTRA_VFS_KIND_FILE :
           S_ISDIR(mode) ? ASTRA_VFS_KIND_DIRECTORY :
           S_ISLNK(mode) ? ASTRA_VFS_KIND_SYMLINK : ASTRA_VFS_KIND_UNKNOWN;
}

static void astra_host_put_time(uint8_t *command, size_t field,
                                int64_t seconds, uint32_t nanoseconds)
{
    stl_be_p(command + field, (uint32_t)((uint64_t)seconds >> 32));
    stl_be_p(command + field + 4u, (uint32_t)seconds);
    stl_be_p(command + field + 8u, nanoseconds);
}

/*
 * No-follow metadata of `path` beneath `directory` -- of `directory` itself
 * when `path` is "" -- into the command's node record. statx(2) on Linux:
 * nanosecond times, the birth time that tells a reused inode number apart,
 * and the mount id where the C library knows it. Elsewhere: stat(2)'s
 * fields with whole seconds, and no birth time.
 */
static int astra_host_publish_node(uint8_t *command, int directory,
                                   const char *path, uint64_t mount)
{
    uint32_t valid = 0u;
#ifdef CONFIG_LINUX
    struct statx sx;
    unsigned mask = STATX_BASIC_STATS | STATX_BTIME;
    static const struct { unsigned statx; uint32_t node; } bits[] = {
        {STATX_TYPE, ASTRA_HOST_NODE_TYPE}, {STATX_MODE, ASTRA_HOST_NODE_MODE},
        {STATX_NLINK, ASTRA_HOST_NODE_NLINK}, {STATX_UID, ASTRA_HOST_NODE_UID},
        {STATX_GID, ASTRA_HOST_NODE_GID}, {STATX_SIZE, ASTRA_HOST_NODE_SIZE},
        {STATX_BLOCKS, ASTRA_HOST_NODE_BLOCKS},
        {STATX_ATIME, ASTRA_HOST_NODE_ATIME},
        {STATX_MTIME, ASTRA_HOST_NODE_MTIME},
        {STATX_CTIME, ASTRA_HOST_NODE_CTIME},
        {STATX_BTIME, ASTRA_HOST_NODE_BTIME}, {STATX_INO, ASTRA_HOST_NODE_INODE},
    };

# ifdef STATX_MNT_ID
    mask |= STATX_MNT_ID;
# endif
    if (statx(directory, path,
              AT_SYMLINK_NOFOLLOW | (path[0] == '\0' ? AT_EMPTY_PATH : 0),
              mask, &sx) != 0)
        return -1;
    for (size_t index = 0u; index < ARRAY_SIZE(bits); ++index)
        if ((sx.stx_mask & bits[index].statx) != 0u)
            valid |= bits[index].node;
    valid |= ASTRA_HOST_NODE_DEVICE | ASTRA_HOST_NODE_BLOCK_SIZE;
    stw_be_p(command + NODE_FIELD(kind), astra_host_kind(sx.stx_mode));
    stw_be_p(command + NODE_FIELD(mode), sx.stx_mode);
    stl_be_p(command + NODE_FIELD(nlink), sx.stx_nlink);
    stl_be_p(command + NODE_FIELD(uid), sx.stx_uid);
    stl_be_p(command + NODE_FIELD(gid), sx.stx_gid);
    stl_be_p(command + NODE_FIELD(block_size), sx.stx_blksize);
    astra_host_put64(command, NODE_FIELD(inode_hi), sx.stx_ino);
    astra_host_put64(command, NODE_FIELD(size_hi), sx.stx_size);
    astra_host_put64(command, NODE_FIELD(blocks_hi), sx.stx_blocks);
    astra_host_put_time(command, NODE_FIELD(atime), sx.stx_atime.tv_sec,
                        sx.stx_atime.tv_nsec);
    astra_host_put_time(command, NODE_FIELD(mtime), sx.stx_mtime.tv_sec,
                        sx.stx_mtime.tv_nsec);
    astra_host_put_time(command, NODE_FIELD(ctime), sx.stx_ctime.tv_sec,
                        sx.stx_ctime.tv_nsec);
    astra_host_put_time(command, NODE_FIELD(btime), sx.stx_btime.tv_sec,
                        sx.stx_btime.tv_nsec);
#else
    struct stat st;

    if ((path[0] == '\0' ? fstat(directory, &st) :
         fstatat(directory, path, &st, AT_SYMLINK_NOFOLLOW)) != 0)
        return -1;
    valid = ASTRA_HOST_NODE_TYPE | ASTRA_HOST_NODE_MODE |
            ASTRA_HOST_NODE_NLINK | ASTRA_HOST_NODE_UID |
            ASTRA_HOST_NODE_GID | ASTRA_HOST_NODE_SIZE |
            ASTRA_HOST_NODE_BLOCKS | ASTRA_HOST_NODE_ATIME |
            ASTRA_HOST_NODE_MTIME | ASTRA_HOST_NODE_CTIME |
            ASTRA_HOST_NODE_INODE | ASTRA_HOST_NODE_DEVICE |
            ASTRA_HOST_NODE_BLOCK_SIZE;
    stw_be_p(command + NODE_FIELD(kind), astra_host_kind(st.st_mode));
    stw_be_p(command + NODE_FIELD(mode), st.st_mode);
    stl_be_p(command + NODE_FIELD(nlink), st.st_nlink);
    stl_be_p(command + NODE_FIELD(uid), st.st_uid);
    stl_be_p(command + NODE_FIELD(gid), st.st_gid);
    stl_be_p(command + NODE_FIELD(block_size), st.st_blksize);
    astra_host_put64(command, NODE_FIELD(inode_hi), st.st_ino);
    astra_host_put64(command, NODE_FIELD(size_hi), st.st_size);
    astra_host_put64(command, NODE_FIELD(blocks_hi), st.st_blocks);
    astra_host_put_time(command, NODE_FIELD(atime), st.st_atime, 0u);
    astra_host_put_time(command, NODE_FIELD(mtime), st.st_mtime, 0u);
    astra_host_put_time(command, NODE_FIELD(ctime), st.st_ctime, 0u);
#endif
    /* Public identity is the native mount, never a recycled Linux loop
     * device number or private namespace mount id. */
    valid |= ASTRA_HOST_NODE_DEVICE | ASTRA_HOST_NODE_MOUNT;
    astra_host_put64(command, NODE_FIELD(device_hi), mount);
    astra_host_put64(command, NODE_FIELD(mount_hi), mount);
    stl_be_p(command + NODE_FIELD(valid), valid);
    return 0;
}

/* The caller holds files_lock; handle publication has one allocator. */
static uint32_t astra_host_insert_file_locked(Astra68State *s,
                                              AstraHostFile *file)
{
    uint32_t handle;

    do {
        astra_network_next(&s->host.next_handle);
        handle = s->host.next_handle;
    } while (g_hash_table_contains(s->host.files, GUINT_TO_POINTER(handle)) ||
             g_hash_table_contains(s->host.lost_handles, GUINT_TO_POINTER(handle)));
    if (file->description != NULL && file->identity.mount == 0u) {
        AstraHostSession *session = g_hash_table_lookup(s->host.sessions, GUINT_TO_POINTER(file->session));
        struct stat info;
        if (session != NULL) {
            file->identity.mount = session->mount;
            file->identity.mount_generation = session->mount_generation;
        }
        file->identity.flags = file->description->flags;
        file->identity.kind = file->description->directory != NULL ? ASTRA_VOLUME_USAGE_DIRECTORY : ASTRA_VOLUME_USAGE_FILE;
        if (fstat(file->description->fd, &info) == 0) file->identity.inode = info.st_ino;
    }
    g_hash_table_insert(s->host.files, GUINT_TO_POINTER(handle), file);
    astra_host_usage_changed_locked(s);
    return handle;
}

/* Namespace lock serializes object creation; files lock protects admission
 * against close, revocation and pending in-flight descriptor releases. */
static bool astra_host_admit_locked(Astra68State *s, uint32_t owner,
                                    uint32_t session, uint32_t scope_files,
                                    uint32_t objects, uint32_t owner_objects,
                                    uint32_t descriptors, uint32_t directories)
{
    uint64_t total = (uint64_t)g_hash_table_size(s->host.files) +
        g_hash_table_size(s->host.lost_handles) +
        g_hash_table_size(s->host.shares) + g_hash_table_size(s->host.sessions);
    uint64_t held = 0u;
    GHashTableIter iterator;
    gpointer value;

    if (total + objects > s->host.fs_max_descriptors ||
        s->host.fs_descriptors + descriptors > s->host.fs_max_descriptors ||
        s->host.fs_directories + directories > s->host.fs_max_directories)
        return false;
    if (scope_files != 0u && owner_objects != 0u) {
        GHashTable *files[] = {s->host.files, s->host.lost_handles};
        for (size_t table = 0u; table < G_N_ELEMENTS(files); ++table) {
            g_hash_table_iter_init(&iterator, files[table]);
            while (g_hash_table_iter_next(&iterator, NULL, &value)) {
                AstraHostFile *file = value;
                held += file->owner == owner && file->session == session;
            }
        }
        g_hash_table_iter_init(&iterator, s->host.shares);
        while (g_hash_table_iter_next(&iterator, NULL, &value)) {
            AstraHostShare *share = value;
            held += share->owner == owner && share->session == session;
        }
        if (held + owner_objects > scope_files)
            return false;
    }
    s->host.fs_descriptors += descriptors;
    s->host.fs_directories += directories;
    return true;
}

static bool astra_host_admit(Astra68State *s, uint32_t owner,
                             uint32_t session, uint32_t scope_files,
                             uint32_t descriptors, uint32_t directories)
{
    bool admitted;
    qemu_mutex_lock(&s->host.files_lock);
    admitted = astra_host_admit_locked(s, owner, session, scope_files,
                                      1u, 1u, descriptors, directories);
    qemu_mutex_unlock(&s->host.files_lock);
    return admitted;
}

/* Inserts an open file into `session`, or answers 0 and closes it when
 * the session closed while the open ran: a session's handles go with it,
 * including one whose open was in flight at the close (review M1). */
static uint32_t astra_host_insert_file(Astra68State *s, uint32_t owner, uint32_t process,
                                       AstraHostSession *session,
                                       uint32_t session_id, int fd,
                                       DIR *directory, uint16_t flags)
{
    AstraHostFile *file = NULL;
    uint32_t handle;

    file = g_new0(AstraHostFile, 1);
    file->description = g_new0(AstraHostDescription, 1);
    file->description->machine = s;
    file->description->fd = fd;
    file->description->directory = directory;
    file->description->references = 1;
    file->description->flags = flags;
    qemu_mutex_init(&file->description->lock);
    file->owner = owner;
    file->process = process;
    file->session = session_id;
    file->references = 1;
    qemu_mutex_lock(&s->host.files_lock);
    if (session->closing) {
        qemu_mutex_unlock(&s->host.files_lock);
        astra_host_file_free(file);
        return 0u;
    }
    handle = astra_host_insert_file_locked(s, file);
    file->description->charged = true;
    qemu_mutex_unlock(&s->host.files_lock);
    return handle;
}

static bool astra_host_close_file(Astra68State *s, uint32_t owner,
                                  uint32_t session_id, uint32_t handle)
{
    AstraHostFile *file = NULL;

    qemu_mutex_lock(&s->host.files_lock);
    file = g_hash_table_lookup(s->host.files, GUINT_TO_POINTER(handle));
    if (file == NULL || file->owner != owner || file->session != session_id ||
        file->closing) {
        qemu_mutex_unlock(&s->host.files_lock);
        return false;
    }
    file->closing = true;
    g_hash_table_steal(s->host.files, GUINT_TO_POINTER(handle));
    astra_host_usage_changed_locked(s);
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_file_release(s, file);
    return true;
}

static uint32_t astra_host_share_file(Astra68State *s, AstraHostFile *file,
                                      uint32_t owner, uint32_t handle,
                                      AstraHostSession *session,
                                      uint32_t session_id, uint32_t scope_files,
                                      uint64_t *token)
{
    AstraHostShare *share = g_new0(AstraHostShare, 1);

    do {
        while (getentropy(&share->token, sizeof(share->token)) != 0) {
            if (errno == EINTR)
                continue;
            g_free(share);
            return ASTRA_STATUS_IO;
        }
    } while (share->token == 0u);
    share->owner = owner;
    share->session = session_id;
    share->description = file != NULL ? file->description : NULL;
    qemu_mutex_lock(&s->host.files_lock);
    AstraHostFile *lost = g_hash_table_lookup(s->host.lost_handles, GUINT_TO_POINTER(handle));
    if (session->closing || (file != NULL ? file->closing :
            lost == NULL || lost->owner != owner)) {
        qemu_mutex_unlock(&s->host.files_lock);
        g_free(share);
        return ASTRA_STATUS_BAD_HANDLE;
    }
    if (!astra_host_admit_locked(s, owner, session_id, scope_files,
                                 1u, 1u, 0u, 0u)) {
        qemu_mutex_unlock(&s->host.files_lock);
        g_free(share);
        return ASTRA_STATUS_LIMIT;
    }
    if (g_hash_table_contains(s->host.shares, &share->token)) {
        qemu_mutex_unlock(&s->host.files_lock);
        g_free(share);
        return ASTRA_STATUS_BUSY;
    }
    share->identity = file != NULL ? file->identity : lost->identity;
    share->process = file != NULL ? file->process : lost->process;
    if (share->description != NULL)
        g_atomic_int_inc(&share->description->references);
    g_hash_table_insert(s->host.shares, &share->token, share);
    astra_host_usage_changed_locked(s);
    *token = share->token;
    qemu_mutex_unlock(&s->host.files_lock);
    return ASTRA_STATUS_OK;
}

static uint32_t astra_host_adopt_file(Astra68State *s, uint32_t owner, uint32_t process,
                                      AstraHostSession *session,
                                      uint32_t session_id, uint64_t token,
                                      bool discard, uint32_t scope_files,
                                      uint32_t *handle)
{
    AstraHostShare *share;
    AstraHostFile *file;

    *handle = 0u;
    qemu_mutex_lock(&s->host.files_lock);
    share = g_hash_table_lookup(s->host.shares, &token);
    if (share == NULL || session->closing) {
        qemu_mutex_unlock(&s->host.files_lock);
        return ASTRA_STATUS_BAD_HANDLE;
    }
    if (share->session != session_id || (discard && share->owner != owner)) {
        qemu_mutex_unlock(&s->host.files_lock);
        return ASTRA_STATUS_ACCESS;
    }
    if (!discard && !astra_host_admit_locked(s, owner, session_id, scope_files,
                                             0u, share->owner != owner, 0u, 0u)) {
        qemu_mutex_unlock(&s->host.files_lock);
        return ASTRA_STATUS_LIMIT;
    }
    g_hash_table_steal(s->host.shares, &token);
    astra_host_usage_changed_locked(s);
    if (!discard) {
        file = g_new0(AstraHostFile, 1);
        file->owner = owner;
        file->process = process;
        file->session = session_id;
        file->references = 1;
        file->description = share->description;
        file->identity = share->identity;
        *handle = astra_host_insert_file_locked(s, file);
        if (share->description == NULL) {
            g_hash_table_steal(s->host.files, GUINT_TO_POINTER(*handle));
            astra_host_usage_changed_locked(s);
            g_hash_table_insert(s->host.lost_handles, GUINT_TO_POINTER(*handle), file);
            astra_host_usage_changed_locked(s);
        }
    }
    qemu_mutex_unlock(&s->host.files_lock);
    if (discard)
        astra_host_description_release(share->description);
    g_free(share);
    return ASTRA_STATUS_OK;
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

/*
 * The path operands, copied out of the DMA span into NUL-terminated
 * buffers. A path is bytes, not a C string: an embedded NUL, a length of
 * ASTRA_HOST_FS_PATH_MAX or more, or a range outside the buffer is INVALID.
 */
static uint32_t astra_host_fs_operands(Astra68State *s, uint32_t physical,
                                       uint32_t bytes, uint32_t command_bytes,
                                       const uint8_t *command, char **path,
                                       char **path2)
{
    uint32_t offset = ldl_be_p(command + HOST_FIELD(path_offset));
    uint32_t first = lduw_be_p(command + HOST_FIELD(path_length));
    uint32_t second = lduw_be_p(command + HOST_FIELD(path2_length));
    uint8_t *span = NULL;

    *path = NULL;
    *path2 = NULL;
    if (first >= ASTRA_HOST_FS_PATH_MAX || second >= ASTRA_HOST_FS_PATH_MAX)
        return ASTRA_STATUS_INVALID;
    if (first + second == 0u) {
        if (offset != 0u)
            return ASTRA_STATUS_INVALID;
    } else {
        if (offset < command_bytes)
            return ASTRA_STATUS_INVALID;
        span = astra_dma_data(s, physical, bytes, offset, first + second);
        if (span == NULL)
            return ASTRA_STATUS_INVALID;
    }
    /* Copy first, then judge the copy: the guest may store into the span
     * while this runs, and only the copy is what the provider uses
     * (review M4). */
    *path = g_malloc(first + 1u);
    *path2 = g_malloc(second + 1u);
    if (first != 0u)
        memcpy(*path, span, first);
    (*path)[first] = '\0';
    if (second != 0u)
        memcpy(*path2, span + first, second);
    (*path2)[second] = '\0';
    if (memchr(*path, 0, first) != NULL || memchr(*path2, 0, second) != NULL)
        return ASTRA_STATUS_INVALID;
    return ASTRA_STATUS_OK;
}

static void astra_host_execute_fs(Astra68State *s, uint32_t owner,
                                  AstraHostChannel *channel,
                                  uint8_t *command, uint32_t physical,
                                  uint32_t bytes, uint32_t command_bytes,
                                  uint32_t expected_generation, AstraHostJob *job);

static void astra_host_metrics_put(uint8_t *snapshot, uint32_t metric,
                                   uint64_t value)
{
    size_t offset = offsetof(AstraHostMetricsSnapshot, values) +
                    metric * sizeof(AstraHostMetricValue);

    stl_be_p(snapshot + offset, value >> 32);
    stl_be_p(snapshot + offset + sizeof(uint32_t), value);
}

static void astra_host_execute_metrics(Astra68State *s, uint8_t *command,
                                       uint32_t physical, uint32_t bytes,
                                       uint32_t command_bytes,
                                       uint32_t expected_generation)
{
    uint8_t *snapshot = NULL;
    uint32_t status = ASTRA_STATUS_INVALID;
    uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));

    stl_be_p(command + HOST_FIELD(status), 0u);
    stl_be_p(command + HOST_FIELD(result_length), 0u);
    stl_be_p(command + HOST_FIELD(result_value), 0u);
    if (ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) !=
            ASTRA_HOST_COMMAND_VERSION ||
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_METRICS ||
        lduw_be_p(command + HOST_FIELD(operation)) !=
            ASTRA_HOST_METRICS_SNAPSHOT ||
        lduw_be_p(command + HOST_FIELD(flags)) != 0u ||
        ldl_be_p(command + HOST_FIELD(generation)) != expected_generation ||
        ldl_be_p(command + HOST_FIELD(handle)) != 0u ||
        ldl_be_p(command + HOST_FIELD(offset_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(offset_lo)) != 0u ||
        ldl_be_p(command + HOST_FIELD(value_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(value_lo)) != 0u ||
        ldl_be_p(command + HOST_FIELD(data_length)) != 0u ||
        capacity < ASTRA_HOST_METRICS_SNAPSHOT_SIZE)
        goto done;
    for (size_t index = HOST_FIELD(session); index < ASTRA_HOST_COMMAND_SIZE;
         ++index)
        if (command[index] != 0u)
            goto done;
    snapshot = astra_host_command_data(
        s, physical, bytes, command_bytes, command,
        ASTRA_HOST_METRICS_SNAPSHOT_SIZE);
    if (snapshot == NULL)
        goto done;
    memset(snapshot, 0, ASTRA_HOST_METRICS_SNAPSHOT_SIZE);
    stl_be_p(snapshot + offsetof(AstraHostMetricsSnapshot, size),
             ASTRA_HOST_METRICS_SNAPSHOT_SIZE);
    stw_be_p(snapshot + offsetof(AstraHostMetricsSnapshot, version),
             ASTRA_HOST_METRICS_VERSION);
    stw_be_p(snapshot + offsetof(AstraHostMetricsSnapshot, count),
             ASTRA_HOST_METRIC_COUNT);
    astra_host_metrics_put(
        snapshot, ASTRA_HOST_METRIC_TIMESTAMP_NS,
        qemu_clock_get_ns(QEMU_CLOCK_REALTIME));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_BLOCK_READ_REQUESTS,
                           qatomic_read(&s->block.read_requests));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_BLOCK_READ_SECTORS,
                           qatomic_read(&s->block.read_sectors));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_BLOCK_WRITE_REQUESTS,
                           qatomic_read(&s->block.write_requests));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_BLOCK_WRITE_SECTORS,
                           qatomic_read(&s->block.write_sectors));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_BLOCK_FLUSH_REQUESTS,
                           qatomic_read(&s->block.flush_requests));
    astra_host_metrics_put(
        snapshot, ASTRA_HOST_METRIC_BLOCK_DURABILITY_TRANSITIONS,
        qatomic_read(&s->block.durability_transitions));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_HOST_SUBMISSIONS,
                           qatomic_read(&s->host.submissions));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_HOST_COMMANDS,
                           qatomic_read(&s->host.commands));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_HOST_EXECUTION_NS,
                           qatomic_read(&s->host.execution_ns));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_HOST_INFLIGHT,
                           qatomic_read(&s->host.inflight));
    astra_host_metrics_put(snapshot, ASTRA_HOST_METRIC_HOST_MAX_INFLIGHT,
                           qatomic_read(&s->host.max_inflight));
    for (uint32_t operation = 0u;
         operation <= ASTRA_HOST_FS_MAX; ++operation) {
        astra_host_metrics_put(
            snapshot, ASTRA_HOST_METRIC_FS_COUNT_BASE + operation,
            qatomic_read(&s->host.operation_counts[operation]));
        astra_host_metrics_put(
            snapshot, ASTRA_HOST_METRIC_FS_EXECUTION_NS_BASE + operation,
            qatomic_read(&s->host.operation_execution_ns[operation]));
    }
    stl_be_p(command + HOST_FIELD(result_length),
             ASTRA_HOST_METRICS_SNAPSHOT_SIZE);
    status = ASTRA_STATUS_OK;

done:
    stl_be_p(command + HOST_FIELD(status), status);
}

static void astra_host_remote_close(AstraHostChannel *channel)
{
    if (channel->remote_desktop_fd >= 0)
        close(channel->remote_desktop_fd);
    channel->remote_desktop_fd = -1;
    channel->remote_desktop_generation = 0u;
}

static uint32_t astra_host_remote_receive(AstraHostChannel *channel,
                                          uint8_t *command)
{
    char reply[64];
    char *end;
    uint64_t value;
    ssize_t length;

    length = recv(channel->remote_desktop_fd, reply, sizeof(reply) - 1u,
                  MSG_DONTWAIT);
    if (length < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return channel->remote_desktop_generation != 0u ?
                ASTRA_STATUS_OK : ASTRA_STATUS_BUSY;
        stl_be_p(command + HOST_FIELD(result_value), errno);
        astra_host_remote_close(channel);
        return ASTRA_STATUS_PEER_DEAD;
    }
    if (length == 0) {
        astra_host_remote_close(channel);
        return ASTRA_STATUS_PEER_DEAD;
    }
    reply[length] = '\0';
    if (g_str_has_prefix(reply, "READY ")) {
        errno = 0;
        value = g_ascii_strtoull(reply + strlen("READY "), &end, 10);
        if (errno == 0 && value != 0u && value <= UINT32_MAX &&
            end == reply + length - 1 && *end == '\n') {
            channel->remote_desktop_generation = value;
            stl_be_p(command + HOST_FIELD(result_value), value);
            return ASTRA_STATUS_OK;
        }
    } else if (g_str_has_prefix(reply, "ERROR ")) {
        errno = 0;
        value = g_ascii_strtoull(reply + strlen("ERROR "), &end, 10);
        if (errno == 0 && value <= UINT32_MAX &&
            end == reply + length - 1 && *end == '\n') {
            stl_be_p(command + HOST_FIELD(result_value), value);
            astra_host_remote_close(channel);
            return ASTRA_STATUS_IO;
        }
    }
    astra_host_remote_close(channel);
    return ASTRA_STATUS_PROTOCOL;
}

static uint32_t astra_host_remote_connect(AstraHostChannel *channel,
                                          uint8_t *command)
{
    const char *path = g_getenv("ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET");
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    int fd;
    int result;

    if (path == NULL || path[0] == '\0')
        path = ASTRA_REMOTE_DESKTOP_CONTROL_DEFAULT;
    if (strlen(path) >= sizeof(address.sun_path))
        return ASTRA_STATUS_INVALID;
    fd = qemu_socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        stl_be_p(command + HOST_FIELD(result_value), errno);
        return ASTRA_STATUS_IO;
    }
    qemu_socket_set_nonblock(fd);
    memcpy(address.sun_path, path, strlen(path) + 1u);
    result = connect(fd, (struct sockaddr *)&address, sizeof(address));
    if (result != 0 && errno != EINPROGRESS && errno != EAGAIN &&
        errno != EWOULDBLOCK) {
        stl_be_p(command + HOST_FIELD(result_value), errno);
        close(fd);
        return ASTRA_STATUS_PEER_DEAD;
    }
    channel->remote_desktop_fd = fd;
    return astra_host_remote_receive(channel, command);
}

static void astra_host_execute_remote_desktop(
    Astra68State *s, AstraHostChannel *channel, uint8_t *command,
    uint32_t expected_generation)
{
    uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
    uint32_t status = ASTRA_STATUS_INVALID;
    uint32_t slot;

    stl_be_p(command + HOST_FIELD(status), 0u);
    stl_be_p(command + HOST_FIELD(result_length), 0u);
    stl_be_p(command + HOST_FIELD(result_value), 0u);
    if (channel == NULL ||
        ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) !=
            ASTRA_HOST_COMMAND_VERSION ||
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_REMOTE_DESKTOP ||
        (operation != ASTRA_HOST_REMOTE_DESKTOP_ACQUIRE &&
         operation != ASTRA_HOST_REMOTE_DESKTOP_STATUS) ||
        lduw_be_p(command + HOST_FIELD(flags)) != 0u ||
        ldl_be_p(command + HOST_FIELD(generation)) != expected_generation ||
        ldl_be_p(command + HOST_FIELD(handle)) != 0u ||
        ldl_be_p(command + HOST_FIELD(offset_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(offset_lo)) != 0u ||
        ldl_be_p(command + HOST_FIELD(value_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(value_lo)) != 0u ||
        ldl_be_p(command + HOST_FIELD(data_offset)) != 0u ||
        ldl_be_p(command + HOST_FIELD(data_length)) != 0u ||
        ldl_be_p(command + HOST_FIELD(data_capacity)) != 0u)
        goto done;
    for (size_t index = HOST_FIELD(session); index < ASTRA_HOST_COMMAND_SIZE;
         ++index)
        if (command[index] != 0u)
            goto done;

    slot = channel - s->host.channels;
    qemu_mutex_lock(&s->host.channel_socket_locks[slot]);
    if (operation == ASTRA_HOST_REMOTE_DESKTOP_ACQUIRE &&
        channel->remote_desktop_fd < 0)
        status = astra_host_remote_connect(channel, command);
    else if (channel->remote_desktop_fd < 0)
        status = ASTRA_STATUS_PEER_DEAD;
    else
        status = astra_host_remote_receive(channel, command);
    if (status == ASTRA_STATUS_OK)
        stl_be_p(command + HOST_FIELD(result_value),
                 channel->remote_desktop_generation);
    qemu_mutex_unlock(&s->host.channel_socket_locks[slot]);

done:
    stl_be_p(command + HOST_FIELD(status), status);
}

static void astra_host_execute_entropy(Astra68State *s, uint8_t *command,
                                       uint32_t physical, uint32_t bytes,
                                       uint32_t command_bytes,
                                       uint32_t expected_generation)
{
    uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
    uint32_t status = ASTRA_STATUS_INVALID;
    uint8_t *data;

    stl_be_p(command + HOST_FIELD(status), 0u);
    stl_be_p(command + HOST_FIELD(result_length), 0u);
    stl_be_p(command + HOST_FIELD(result_value), 0u);
    if (ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) !=
            ASTRA_HOST_COMMAND_VERSION ||
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_ENTROPY ||
        lduw_be_p(command + HOST_FIELD(operation)) !=
            ASTRA_HOST_ENTROPY_FILL ||
        lduw_be_p(command + HOST_FIELD(flags)) != 0u ||
        ldl_be_p(command + HOST_FIELD(generation)) != expected_generation ||
        ldl_be_p(command + HOST_FIELD(handle)) != 0u ||
        ldl_be_p(command + HOST_FIELD(offset_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(offset_lo)) != 0u ||
        ldl_be_p(command + HOST_FIELD(value_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(value_lo)) != 0u ||
        ldl_be_p(command + HOST_FIELD(data_length)) != 0u ||
        capacity == 0u || capacity > ASTRA_HOST_ENTROPY_MAX)
        goto done;
    for (size_t index = HOST_FIELD(session); index < ASTRA_HOST_COMMAND_SIZE;
         ++index)
        if (command[index] != 0u)
            goto done;
    data = astra_host_command_data(s, physical, bytes, command_bytes,
                                   command, capacity);
    if (data == NULL)
        goto done;
#ifdef CONFIG_POSIX
    while (getentropy(data, capacity) != 0) {
        if (errno == EINTR)
            continue;
        stl_be_p(command + HOST_FIELD(result_value), errno);
        status = ASTRA_STATUS_IO;
        goto done;
    }
    stl_be_p(command + HOST_FIELD(result_length), capacity);
    status = ASTRA_STATUS_OK;
#else
    status = ASTRA_STATUS_UNSUPPORTED;
#endif

done:
    stl_be_p(command + HOST_FIELD(status), status);
}

/* The audio operations whose reply carries data back into the area. */
static bool astra_host_audio_answers(uint32_t operation)
{
    return operation == ASTRA_HOST_AUDIO_CONVERT ||
           operation == ASTRA_HOST_AUDIO_FONT_END ||
           operation == ASTRA_HOST_AUDIO_FONT_LIST ||
           operation == ASTRA_HOST_AUDIO_MIDI_PRESETS ||
           operation == ASTRA_HOST_AUDIO_MIDI_STATUS;
}

static void astra_host_execute_audio(Astra68State *s,
                                     AstraHostChannel *channel,
                                     uint8_t *command, uint32_t physical,
                                     uint32_t bytes, uint32_t command_bytes,
                                     uint32_t expected_generation)
{
    uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
    uint32_t handle = ldl_be_p(command + HOST_FIELD(handle));
    uint32_t value = ldl_be_p(command + HOST_FIELD(value_lo));
    uint32_t value_hi = ldl_be_p(command + HOST_FIELD(value_hi));
    uint32_t length = ldl_be_p(command + HOST_FIELD(data_length));
    uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
    const uint32_t packet = ASTRA_AUDIO_HOST_PACKET_BYTES;
    uint32_t status = ASTRA_STATUS_INVALID;
    uint8_t *data = NULL;
    AstraAudioHostRequest request = {
        .magic = ASTRA_AUDIO_HOST_MAGIC,
        .version = ASTRA_AUDIO_HOST_VERSION,
        .operation = operation,
        .handle = handle,
        .value = value,
        .data_length = length,
    };
    AstraAudioHostReply reply;
    struct iovec parts[2];
    struct msghdr message = {0};
    struct iovec back[2];
    struct msghdr reply_message = {0};
    uint32_t answer = 0u;
    uint32_t slot;
    ssize_t moved;

    stl_be_p(command + HOST_FIELD(status), 0u);
    stl_be_p(command + HOST_FIELD(result_length), 0u);
    stl_be_p(command + HOST_FIELD(result_value), 0u);
    if (channel == NULL)
        goto done;
    if (ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) !=
            ASTRA_HOST_COMMAND_VERSION ||
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_AUDIO ||
        operation < ASTRA_HOST_AUDIO_OPEN ||
        operation > ASTRA_HOST_AUDIO_OPERATION_MAX ||
        lduw_be_p(command + HOST_FIELD(flags)) != 0u ||
        ldl_be_p(command + HOST_FIELD(generation)) !=
            expected_generation ||
        ldl_be_p(command + HOST_FIELD(offset_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(offset_lo)) != 0u ||
        (value_hi != 0u && operation != ASTRA_HOST_AUDIO_CONVERT_OPEN &&
         operation != ASTRA_HOST_AUDIO_MIDI_LOAD &&
         operation != ASTRA_HOST_AUDIO_MIDI_SET))
        goto done;
    for (size_t index = HOST_FIELD(session); index < ASTRA_HOST_COMMAND_SIZE;
         ++index)
        if (command[index] != 0u)
            goto done;
    if (operation == ASTRA_HOST_AUDIO_STATUS) {
        if (value != 0u || length != 0u ||
            capacity < sizeof(AstraHostAudioStatus))
            goto done;
        data = astra_host_command_data(s, physical, bytes, command_bytes,
                                       command,
                                       sizeof(AstraHostAudioStatus));
        if (data == NULL)
            goto done;
    } else {
        /*
         * QEMU bounds what crosses guest memory; the daemon judges the
         * operation. Up to a packet goes out, and the operations that
         * answer with data use up to data_capacity bytes of the same area.
         */
        answer = astra_host_audio_answers(operation) ? capacity : 0u;
        if (length > packet || capacity > packet || capacity < length ||
            (operation == ASTRA_HOST_AUDIO_CONVERT && capacity == 0u))
            goto done;
        if (capacity != 0u) {
            data = astra_host_command_data(s, physical, bytes, command_bytes,
                                           command, capacity);
            if (data == NULL)
                goto done;
        }
        request.value_hi = value_hi;
        request.capacity = answer;
    }

    slot = channel - s->host.channels;
    qemu_mutex_lock(&s->host.channel_socket_locks[slot]);
    if (channel->audio_fd < 0) {
        const char *path = g_getenv("ASTRA_AUDIO_HOST_SOCKET");
        struct sockaddr_un address = {.sun_family = AF_UNIX};
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 200000};
        int fd;

        if (path == NULL || path[0] == '\0')
            path = ASTRA_AUDIO_HOST_SOCKET;
        if (strlen(path) >= sizeof(address.sun_path))
            goto unlock;
        fd = qemu_socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (fd < 0)
            goto peer_dead;
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO,
                         &timeout, sizeof(timeout));
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                         &timeout, sizeof(timeout));
        memcpy(address.sun_path, path, strlen(path) + 1u);
        if (connect(fd, (struct sockaddr *)&address,
                    sizeof(address)) != 0) {
            close(fd);
            goto peer_dead;
        }
        channel->audio_fd = fd;
    }
    parts[0].iov_base = &request;
    parts[0].iov_len = sizeof(request);
    parts[1].iov_base = data;
    parts[1].iov_len = operation == ASTRA_HOST_AUDIO_STATUS ? 0u : length;
    message.msg_iov = parts;
    message.msg_iovlen = parts[1].iov_len != 0u ? 2u : 1u;
    moved = sendmsg(channel->audio_fd, &message, MSG_NOSIGNAL);
    if (moved != (ssize_t)(sizeof(request) + parts[1].iov_len))
        goto peer_dead;
    back[0].iov_base = &reply;
    back[0].iov_len = sizeof(reply);
    back[1].iov_base = data;
    back[1].iov_len = answer;
    reply_message.msg_iov = back;
    reply_message.msg_iovlen = answer != 0u ? 2u : 1u;
    moved = recvmsg(channel->audio_fd, &reply_message, 0);
    if (moved < (ssize_t)sizeof(reply) ||
        reply.magic != ASTRA_AUDIO_HOST_MAGIC ||
        reply.status > ASTRA_STATUS_SYSTEM_MAX ||
        reply.data_length > back[1].iov_len ||
        moved != (ssize_t)(sizeof(reply) + reply.data_length) ||
        (reply_message.msg_flags & MSG_TRUNC) != 0) {
        status = ASTRA_STATUS_PROTOCOL;
        goto disconnect;
    }
    status = reply.status;
    if (status == ASTRA_STATUS_OK) {
        if (operation == ASTRA_HOST_AUDIO_OPEN ||
            operation == ASTRA_HOST_AUDIO_CONVERT_OPEN ||
            operation == ASTRA_HOST_AUDIO_FONT_BEGIN ||
            operation == ASTRA_HOST_AUDIO_MIDI_OPEN)
            stl_be_p(command + HOST_FIELD(result_value), reply.handle);
        if (astra_host_audio_answers(operation))
            stl_be_p(command + HOST_FIELD(result_length), reply.data_length);
        if (operation == ASTRA_HOST_AUDIO_CONVERT)
            stl_be_p(command + HOST_FIELD(result_value),
                     reply.queued_frames);
        if (operation == ASTRA_HOST_AUDIO_MIDI_STATUS ||
            operation == ASTRA_HOST_AUDIO_FONT_LIST ||
            operation == ASTRA_HOST_AUDIO_MIDI_PRESETS)
            stl_be_p(command + HOST_FIELD(result_value), reply.value);
        if (operation == ASTRA_HOST_AUDIO_WRITE)
            stl_be_p(command + HOST_FIELD(result_length),
                     reply.queued_frames);
        if (operation == ASTRA_HOST_AUDIO_STATUS) {
            stl_be_p(data + offsetof(AstraHostAudioStatus, queued_frames),
                     reply.queued_frames);
            stl_be_p(data + offsetof(AstraHostAudioStatus, hardware_frames),
                     reply.hardware_frames);
            stl_be_p(data + offsetof(AstraHostAudioStatus, underruns),
                     reply.underruns);
            stl_be_p(data + offsetof(AstraHostAudioStatus, overflows),
                     reply.overflows);
            stl_be_p(data + offsetof(AstraHostAudioStatus, software_gaps),
                     reply.software_gaps);
            stl_be_p(command + HOST_FIELD(result_length),
                     sizeof(AstraHostAudioStatus));
        }
    }
    goto unlock;

peer_dead:
    status = ASTRA_STATUS_PEER_DEAD;
disconnect:
    close(channel->audio_fd);
    channel->audio_fd = -1;
unlock:
    qemu_mutex_unlock(&s->host.channel_socket_locks[slot]);
done:
    stl_be_p(command + HOST_FIELD(status), status);
}

static void astra_host_reconcile_mounts(Astra68State *s);
static uint32_t astra_host_volume_control(Astra68State *s, uint32_t owner,
    AstraHostChannel *channel, uint8_t *data);

static int astra_host_storage_control(Astra68State *s, AstraStorageControlRequest *request,
    int input, AstraStorageControlReply *reply, int *output)
{
    int error;
    *output = -1;
    qemu_mutex_lock(&s->host.provider_control_lock);
    if (qatomic_read(&s->host.provider_broken) || s->host.provider_control < 0)
        error = ENODEV;
    else if (s->host.provider_control_serial == UINT64_MAX)
        error = EOVERFLOW;
    else {
        request->serial = ++s->host.provider_control_serial;
        error = astra_storage_control_call(s->host.provider_control, request, input, reply, output);
        if (error != 0) qatomic_set(&s->host.provider_broken, true);
        else error = reply->error;
    }
    qemu_mutex_unlock(&s->host.provider_control_lock);
    return error;
}

static int astra_host_provider_root(Astra68State *s, uint64_t mount, uint64_t generation);

typedef struct AstraHostUsageObject {
    AstraHostIdentity identity;
    uint64_t handle;
    uint32_t process;
    uint32_t kind;
    uint32_t flags;
    int fd;
    const AstraStorageRecord *image;
} AstraHostUsageObject;

static int astra_host_usage_compare(const void *left, const void *right)
{
    const AstraHostUsageObject *a = left, *b = right;
    return astra_volume_usage_key_compare(a->kind, a->handle, a->process,
        b->kind, b->handle, b->process);
}

static const AstraStorageRecord *astra_host_usage_mount(const GArray *catalog,
    uint64_t mount, uint64_t generation)
{
    for (guint at = 0u; at < catalog->len; ++at) {
        const AstraStorageRecord *row = &g_array_index(catalog, AstraStorageRecord, at);
        if (row->id == mount && row->generation == generation &&
            (row->kind == ASTRA_VOLUME_MOUNT || row->kind == ASTRA_VOLUME_BINDING))
            return row;
    }
    return NULL;
}

/* Names derive from retained descriptors through the canonical privileged
 * resolver. No application pathname and no Linux descriptor escapes this port. */
static void astra_host_usage_name(Astra68State *s, const GArray *catalog,
    const AstraHostUsageObject *object, AstraVolumeUsageRecord *row)
{
    const AstraStorageRecord *mount = astra_host_usage_mount(catalog,
        row->volume.id, row->volume.generation);
    row->flags |= ASTRA_VOLUME_USAGE_UNNAMED;
    if (mount == NULL || (mount->state != ASTRA_VOLUME_MOUNTED && mount->state != ASTRA_VOLUME_QUIESCING)) {
        row->flags |= ASTRA_VOLUME_USAGE_LOST;
        return;
    }
    int fd = object->fd, image_fd = -1;
    if (object->image != NULL) {
        AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
            .operation = ASTRA_STORAGE_CONTROL_BACKING, .epoch = row->volume.epoch,
            .target = object->image->id, .generation = object->image->generation};
        AstraStorageControlReply reply;
        if (astra_host_storage_control(s, &request, -1, &reply, &image_fd) != 0) return;
        fd = image_fd;
    }
    if (fd < 0) return;
    int root = astra_host_provider_root(s, row->volume.id, row->volume.generation);
    if (root >= 0) {
        char relative[ASTRA_HOST_FS_PATH_MAX];
        uint32_t length = 0u;
        AstraHostSession authority = {.mount = row->volume.id, .mount_generation = row->volume.generation};
        int status = astra_host_descriptor_path(s, &authority, root, fd, relative, sizeof(relative), &length);
        size_t prefix = strlen(mount->name);
        if (status == 0 && prefix != 0u && length != 0u && relative[0] == '/' &&
            prefix + length < sizeof(row->path)) {
            memcpy(row->path, mount->name, prefix);
            memcpy(row->path + prefix, relative, length);
            row->path_length = prefix + length;
            row->path[row->path_length] = '\0';
            if (g_utf8_validate(row->path, row->path_length, NULL))
                row->flags &= ~ASTRA_VOLUME_USAGE_UNNAMED;
            else {
                row->path_length = 0u; row->path[0] = '\0';
                row->flags |= ASTRA_VOLUME_USAGE_INVALID_NAME;
            }
        } else if (status == -ERANGE || status == -ENAMETOOLONG ||
            (status == 0 && prefix + length >= sizeof(row->path)))
            row->flags |= ASTRA_VOLUME_USAGE_INVALID_NAME;
        close(root);
    }
    if (image_fd >= 0) close(image_fd);
}

/* A page samples the existing resource objects while their canonical lock
 * is held. Separate provider and resource generations reject changed tails. */
static uint32_t astra_host_volume_usage(Astra68State *s, uint32_t owner,
    AstraHostChannel *channel, uint8_t *wire)
{
    if (channel == NULL || !channel->provider || channel->bound || owner == 0u ||
        owner != qatomic_read(&s->host.fs_owner)) return ASTRA_STATUS_ACCESS;
    AstraVolumeUsageQuery query = {
        .version = ldl_be_p(wire), .size = ldl_be_p(wire + 4u),
        .volume = {ldq_be_p(wire + 8u), ldq_be_p(wire + 16u), ldq_be_p(wire + 24u)},
        .sequence = ldq_be_p(wire + 32u), .catalog_sequence = ldq_be_p(wire + 40u),
        .after = ldq_be_p(wire + 48u), .after_kind = ldl_be_p(wire + 56u),
        .after_process = ldl_be_p(wire + 60u)};
    if (query.version != ASTRA_VOLUME_USAGE_VERSION || query.size != sizeof(query) ||
        ((query.volume.id == 0u) != (query.volume.generation == 0u)) ||
        (query.volume.id != 0u && query.volume.epoch == 0u) ||
        ((query.sequence == 0u) != (query.catalog_sequence == 0u)) ||
        ((query.after == 0u) != (query.after_kind == 0u)) ||
        (query.after == 0u && query.after_process != 0u) ||
        query.after_kind > ASTRA_VOLUME_USAGE_IMAGE ||
        (query.volume.epoch == 0u && query.sequence != 0u) ||
        (query.sequence == 0u && query.after != 0u)) return ASTRA_STATUS_INVALID;
    if (query.volume.epoch != 0u && query.volume.epoch != s->host.provider_epoch)
        return ASTRA_STATUS_LOST;
    GArray *catalog = g_array_new(false, false, sizeof(AstraStorageRecord));
    GArray *objects = g_array_new(false, false, sizeof(AstraHostUsageObject));
    AstraVolumeUsagePage *page = g_new0(AstraVolumeUsagePage, 1);
    uint32_t status = ASTRA_STATUS_OK;
    int output = -1, error = 0;
    uint64_t catalog_sequence = 0u;
    qemu_mutex_lock(&s->host.namespace_lock);
    AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_SNAPSHOT, .epoch = s->host.provider_epoch};
    AstraStorageControlReply response;
    do {
        error = astra_host_storage_control(s, &request, -1, &response, &output);
        if (output >= 0) { close(output); output = -1; }
        if (error != 0) goto out;
        catalog_sequence = response.sequence;
        g_array_append_vals(catalog, response.records, response.count);
        request.sequence = response.sequence; request.after = response.next;
    } while (request.after != 0u);
    if (query.catalog_sequence != 0u && query.catalog_sequence != catalog_sequence) {
        error = EBUSY; goto out;
    }
    qemu_mutex_lock(&s->host.files_lock);
    if (s->host.usage_sequence == UINT64_MAX) { status = ASTRA_STATUS_LIMIT; goto unlocked; }
    if (query.sequence != 0u && query.sequence != s->host.usage_sequence) {
        status = ASTRA_STATUS_BUSY; goto unlocked;
    }
    GHashTable *tables[] = {s->host.files, s->host.lost_handles};
    GHashTableIter iterator;
    gpointer key, value;
    for (size_t at = 0u; at < G_N_ELEMENTS(tables); ++at) {
        g_hash_table_iter_init(&iterator, tables[at]);
        while (g_hash_table_iter_next(&iterator, &key, &value)) {
            AstraHostFile *file = value;
            AstraHostUsageObject object = {.identity = file->identity,
                .handle = GPOINTER_TO_UINT(key), .process = file->process,
                .kind = file->identity.kind, .fd = file->description != NULL ? file->description->fd : -1,
                .flags = at != 0u ? ASTRA_VOLUME_USAGE_LOST | ASTRA_VOLUME_USAGE_UNNAMED :
                    file->references > 1u ? ASTRA_VOLUME_USAGE_ACTIVE : 0u};
            g_array_append_val(objects, object);
        }
    }
    g_hash_table_iter_init(&iterator, s->host.shares);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        AstraHostShare *share = value;
        AstraHostUsageObject object = {.identity = share->identity, .handle = share->token,
            .process = share->process, .kind = ASTRA_VOLUME_USAGE_SHARE,
            .fd = share->description != NULL ? share->description->fd : -1,
            .flags = share->description == NULL ? ASTRA_VOLUME_USAGE_LOST | ASTRA_VOLUME_USAGE_UNNAMED : 0u};
        g_array_append_val(objects, object);
    }
    g_hash_table_iter_init(&iterator, s->host.sessions);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostSession *session = value;
        if (session->root < 0) continue;
        struct stat info;
        AstraHostUsageObject object = {.identity = {.mount = session->mount,
            .mount_generation = session->mount_generation,
            .flags = ASTRA_VFS_OPEN_READ | ((session->flags & ASTRA_HOST_FS_SESSION_READ_ONLY) == 0u ? ASTRA_VFS_OPEN_WRITE : 0u)},
            .handle = GPOINTER_TO_UINT(key), .process = session->requester,
            .kind = ASTRA_VOLUME_USAGE_ROOT, .fd = session->root};
        if (fstat(session->root, &info) == 0) object.identity.inode = info.st_ino;
        g_array_append_val(objects, object);
    }
    for (AstraHostJob *job = s->host.jobs; job != NULL; job = job->next) {
        if (job->session == NULL) continue;
        AstraHostUsageObject object = {.identity = {.mount = job->session->mount,
            .mount_generation = job->session->mount_generation}, .handle = job->id,
            .process = job->process, .kind = ASTRA_VOLUME_USAGE_OPERATION, .fd = -1,
            .flags = ASTRA_VOLUME_USAGE_ACTIVE | ASTRA_VOLUME_USAGE_UNNAMED};
        if (job->session->closing) object.flags |= ASTRA_VOLUME_USAGE_LOST | ASTRA_VOLUME_USAGE_CLEANUP_PENDING;
        g_array_append_val(objects, object);
    }
    for (guint at = 0u; at < catalog->len; ++at) {
        const AstraStorageRecord *image = &g_array_index(catalog, AstraStorageRecord, at);
        /* Loss publication precedes backing/probe/lease cleanup. The canonical
         * provider removes this object only after its real resources retire. */
        if (image->kind != ASTRA_VOLUME_SOURCE || image->origin != ASTRA_VOLUME_ORIGIN_IMAGE) continue;
        AstraHostUsageObject object = {.identity = {.mount = image->parent,
            .mount_generation = image->parent_generation, .inode = image->backing_object,
            .flags = ASTRA_VFS_OPEN_READ}, .handle = image->id, .kind = ASTRA_VOLUME_USAGE_IMAGE,
            .fd = -1, .image = image,
            .flags = image->state == ASTRA_VOLUME_LOST ? ASTRA_VOLUME_USAGE_LOST | ASTRA_VOLUME_USAGE_CLEANUP_PENDING :
                (image->flags & ASTRA_VOLUME_CLEANUP_PENDING) != 0u ? ASTRA_VOLUME_USAGE_CLEANUP_PENDING : 0u};
        g_array_append_val(objects, object);
    }
    if (query.volume.id != 0u && astra_host_usage_mount(catalog,
            query.volume.id, query.volume.generation) == NULL) {
        bool retained = false;
        for (guint at = 0u; at < objects->len; ++at) {
            const AstraHostUsageObject *object = &g_array_index(objects, AstraHostUsageObject, at);
            if (object->identity.mount == query.volume.id && object->identity.mount_generation == query.volume.generation) {
                retained = true; break;
            }
        }
        /* Retained lost objects remain inspectable by their exact identity.
         * An unknown/stale mount is never reported as a successful empty set. */
        if (!retained) { status = ASTRA_STATUS_LOST; goto unlocked; }
    }
    g_array_sort(objects, astra_host_usage_compare);
    page->version = ASTRA_VOLUME_USAGE_VERSION; page->size = sizeof(*page);
    page->volume = (AstraVolumeReference){s->host.provider_epoch, query.volume.id, query.volume.generation};
    page->sequence = s->host.usage_sequence; page->catalog_sequence = catalog_sequence;
    for (guint at = 0u; at < objects->len; ++at) {
        const AstraHostUsageObject *object = &g_array_index(objects, AstraHostUsageObject, at);
        if (object->identity.mount == 0u || object->identity.mount_generation == 0u ||
            (query.volume.id != 0u && (object->identity.mount != query.volume.id ||
                object->identity.mount_generation != query.volume.generation)) ||
            astra_volume_usage_key_compare(object->kind, object->handle, object->process,
                query.after_kind, query.after, query.after_process) <= 0) continue;
        if (page->count == ASTRA_VOLUME_USAGE_PAGE_RECORDS) {
            page->next = page->records[page->count - 1u].handle;
            page->next_kind = page->records[page->count - 1u].kind;
            page->next_process = page->records[page->count - 1u].process;
            break;
        }
        AstraVolumeUsageRecord *row = &page->records[page->count++];
        row->volume = (AstraVolumeReference){s->host.provider_epoch, object->identity.mount, object->identity.mount_generation};
        row->handle = object->handle; row->inode = object->identity.inode;
        row->process = object->process; row->kind = object->kind; row->flags = object->flags;
        row->rights = ((object->identity.flags & ASTRA_VFS_OPEN_READ) != 0u ? ASTRA_RIGHT_READ : 0u) |
            ((object->identity.flags & ASTRA_VFS_OPEN_WRITE) != 0u ? ASTRA_RIGHT_WRITE : 0u);
        astra_host_usage_name(s, catalog, object, row);
    }
unlocked:
    qemu_mutex_unlock(&s->host.files_lock);
    if (status == ASTRA_STATUS_OK) {
        request.sequence = 0u; request.after = 0u;
        error = astra_host_storage_control(s, &request, -1, &response, &output);
        if (output >= 0) { close(output); output = -1; }
        if (error == 0 && response.sequence != catalog_sequence) error = EBUSY;
    }
out:
    qemu_mutex_unlock(&s->host.namespace_lock);
    if (error != 0) status = astra_host_status_from_errno(error);
    if (status == ASTRA_STATUS_OK) {
        memset(wire, 0, sizeof(*page));
#define USAGE32(field) stl_be_p(wire + offsetof(AstraVolumeUsagePage, field), page->field)
#define USAGE64(field) stq_be_p(wire + offsetof(AstraVolumeUsagePage, field), page->field)
        USAGE32(version); USAGE32(size); USAGE64(volume.epoch); USAGE64(volume.id);
        USAGE64(volume.generation); USAGE64(sequence); USAGE64(catalog_sequence);
        USAGE64(next); USAGE32(next_kind); USAGE32(count); USAGE32(next_process);
#undef USAGE32
#undef USAGE64
        for (uint32_t at = 0u; at < page->count; ++at) {
            const AstraVolumeUsageRecord *record = &page->records[at];
            uint8_t *row = wire + offsetof(AstraVolumeUsagePage, records) + at * sizeof(*record);
#define ROW32(field) stl_be_p(row + offsetof(AstraVolumeUsageRecord, field), record->field)
#define ROW64(field) stq_be_p(row + offsetof(AstraVolumeUsageRecord, field), record->field)
            ROW64(volume.epoch); ROW64(volume.id); ROW64(volume.generation); ROW64(handle); ROW64(inode);
            ROW32(process); ROW32(kind); ROW32(rights); ROW32(flags); ROW32(path_length);
#undef ROW32
#undef ROW64
            memcpy(row + offsetof(AstraVolumeUsageRecord, path), record->path, record->path_length);
        }
    }
    g_free(page); g_array_free(objects, true); g_array_free(catalog, true);
    return status;
}

/* Public metadata is separate from rooted data authority and administrator
 * control. One snapshot comes from the provider's canonical catalog. */
static void astra_host_execute_volumes(Astra68State *s, uint32_t owner,
                                       AstraHostChannel *channel, uint8_t *command,
                                       uint32_t physical, uint32_t bytes,
                                       uint32_t command_bytes, uint32_t generation)
{
    uint32_t status = ASTRA_STATUS_INVALID;
    stl_be_p(command + HOST_FIELD(result_length), 0u);
    stl_be_p(command + HOST_FIELD(result_value), 0u);
    if (ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) != ASTRA_HOST_COMMAND_VERSION ||
        ldl_be_p(command + HOST_FIELD(generation)) != generation ||
        (lduw_be_p(command + HOST_FIELD(operation)) != ASTRA_HOST_VOLUME_CATALOG &&
         lduw_be_p(command + HOST_FIELD(operation)) != ASTRA_HOST_VOLUME_CONTROL &&
         lduw_be_p(command + HOST_FIELD(operation)) != ASTRA_HOST_VOLUME_USAGE &&
         lduw_be_p(command + HOST_FIELD(operation)) != ASTRA_HOST_VOLUME_HOLDERS_CHANGED) ||
        lduw_be_p(command + HOST_FIELD(flags)) != 0u ||
        ldl_be_p(command + HOST_FIELD(handle)) != 0u ||
        ldl_be_p(command + HOST_FIELD(session)) != 0u ||
        astra_host_get64(command, HOST_FIELD(offset_hi)) != 0u ||
        astra_host_get64(command, HOST_FIELD(value_hi)) != 0u ||
        ldl_be_p(command + HOST_FIELD(path_offset)) != 0u ||
        lduw_be_p(command + HOST_FIELD(path_length)) != 0u ||
        lduw_be_p(command + HOST_FIELD(path2_length)) != 0u)
        goto done;
    for (size_t at = HOST_FIELD(reserved); at < ASTRA_HOST_COMMAND_SIZE; ++at)
        if (command[at] != 0u)
            goto done;
    if (lduw_be_p(command + HOST_FIELD(operation)) == ASTRA_HOST_VOLUME_HOLDERS_CHANGED) {
        if (channel == NULL || !channel->provider || channel->bound || owner == 0u ||
            owner != qatomic_read(&s->host.fs_owner)) { status = ASTRA_STATUS_ACCESS; goto done; }
        if (ldl_be_p(command + HOST_FIELD(data_offset)) != 0u ||
            ldl_be_p(command + HOST_FIELD(data_length)) != 0u ||
            ldl_be_p(command + HOST_FIELD(data_capacity)) != 0u) goto done;
        qemu_mutex_lock(&s->host.files_lock);
        status = s->host.usage_sequence == UINT64_MAX ? ASTRA_STATUS_LIMIT : ASTRA_STATUS_OK;
        if (status == ASTRA_STATUS_OK) astra_host_usage_changed_locked(s);
        qemu_mutex_unlock(&s->host.files_lock);
        goto done;
    }
    if (lduw_be_p(command + HOST_FIELD(operation)) == ASTRA_HOST_VOLUME_USAGE) {
        if (ldl_be_p(command + HOST_FIELD(data_length)) != sizeof(AstraVolumeUsageQuery) ||
            ldl_be_p(command + HOST_FIELD(data_capacity)) < sizeof(AstraVolumeUsagePage)) goto done;
        uint8_t *data = astra_host_command_data(s, physical, bytes, command_bytes, command,
            sizeof(AstraVolumeUsagePage));
        if (data == NULL) goto done;
        status = astra_host_volume_usage(s, owner, channel, data);
        if (status == ASTRA_STATUS_OK) stl_be_p(command + HOST_FIELD(result_length), sizeof(AstraVolumeUsagePage));
        goto done;
    }
    if (lduw_be_p(command + HOST_FIELD(operation)) == ASTRA_HOST_VOLUME_CONTROL) {
        if (ldl_be_p(command + HOST_FIELD(data_length)) != sizeof(AstraHostVolumeControl) ||
            ldl_be_p(command + HOST_FIELD(data_capacity)) < sizeof(AstraVolumeReference)) goto done;
        uint8_t *data = astra_host_command_data(s, physical, bytes, command_bytes,
            command, sizeof(AstraHostVolumeControl));
        if (data == NULL) goto done;
        status = astra_host_volume_control(s, owner, channel, data);
        if (status == ASTRA_STATUS_OK)
            stl_be_p(command + HOST_FIELD(result_length), sizeof(AstraVolumeReference));
        goto done;
    }
    if (ldl_be_p(command + HOST_FIELD(data_length)) != sizeof(AstraVolumeCatalogQuery) ||
        ldl_be_p(command + HOST_FIELD(data_capacity)) < sizeof(AstraVolumeCatalogPage)) goto done;
    uint8_t *data = astra_host_command_data(s, physical, bytes, command_bytes,
        command, sizeof(AstraVolumeCatalogPage));
    if (data == NULL || ldl_be_p(data) != ASTRA_VOLUME_CATALOG_VERSION ||
        ldl_be_p(data + 4u) != sizeof(AstraVolumeCatalogQuery))
        goto done;
    AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_SNAPSHOT,
        .epoch = ldq_be_p(data + offsetof(AstraVolumeCatalogQuery, epoch)),
        .sequence = ldq_be_p(data + offsetof(AstraVolumeCatalogQuery, sequence)),
        .after = ldq_be_p(data + offsetof(AstraVolumeCatalogQuery, after))};
    if ((request.epoch == 0u && (request.sequence != 0u || request.after != 0u)) ||
        (request.sequence == 0u && request.after != 0u))
        goto done;
    AstraStorageControlReply reply;
    int fd = -1, error;
    error = astra_host_storage_control(s, &request, -1, &reply, &fd);
    astra_host_reconcile_mounts(s);
    if (error != 0) {
        status = astra_host_status_from_errno(error);
        goto done;
    }
    memset(data, 0, sizeof(AstraVolumeCatalogPage));
    stl_be_p(data, ASTRA_VOLUME_CATALOG_VERSION);
    stl_be_p(data + 4u, sizeof(AstraVolumeCatalogPage));
    stq_be_p(data + offsetof(AstraVolumeCatalogPage, epoch), reply.epoch);
    stq_be_p(data + offsetof(AstraVolumeCatalogPage, sequence), reply.sequence);
    stq_be_p(data + offsetof(AstraVolumeCatalogPage, next), reply.next);
    stl_be_p(data + offsetof(AstraVolumeCatalogPage, count), reply.count);
    for (uint32_t at = 0u; at < reply.count; ++at) {
        const AstraStorageRecord *record = &reply.records[at];
        uint8_t *row = data + offsetof(AstraVolumeCatalogPage, records) + at * sizeof(AstraVolumeRecord);
#define VOLUME64(field) stq_be_p(row + offsetof(AstraVolumeRecord, field), record->field)
#define VOLUME32(field) stl_be_p(row + offsetof(AstraVolumeRecord, field), record->field)
        VOLUME64(id); VOLUME64(generation); VOLUME64(parent); VOLUME64(parent_generation);
        VOLUME64(byte_limit); VOLUME64(inode_limit); VOLUME64(backing_object);
        VOLUME32(kind); VOLUME32(class_id); VOLUME32(state); VOLUME32(format);
        VOLUME32(origin); VOLUME32(flags);
#undef VOLUME64
#undef VOLUME32
        stl_be_p(row + offsetof(AstraVolumeRecord, status), astra_host_status_from_errno(record->error));
        memcpy(row + offsetof(AstraVolumeRecord, name), record->name, sizeof(record->name));
        memcpy(row + offsetof(AstraVolumeRecord, uuid), record->uuid, sizeof(record->uuid));
        memcpy(row + offsetof(AstraVolumeRecord, label), record->label, sizeof(record->label));
    }
    stl_be_p(command + HOST_FIELD(result_length), sizeof(AstraVolumeCatalogPage));
    status = ASTRA_STATUS_OK;
done:
    stl_be_p(command + HOST_FIELD(status), status);
}

static void astra_host_execute_command(Astra68State *s, uint32_t owner,
                                       AstraHostChannel *channel,
                                       uint8_t *command,
                                       uint32_t physical, uint32_t bytes,
                                       uint32_t command_bytes,
                                       uint32_t expected_generation, AstraHostJob *job)
{
    uint16_t service = lduw_be_p(command + HOST_FIELD(service));
    bool non_fs = service == ASTRA_HOST_SERVICE_METRICS ||
        service == ASTRA_HOST_SERVICE_REMOTE_DESKTOP ||
        service == ASTRA_HOST_SERVICE_ENTROPY ||
        service == ASTRA_HOST_SERVICE_VOLUMES || service == ASTRA_HOST_SERVICE_AUDIO;
    int64_t started = non_fs ? qemu_clock_get_ns(QEMU_CLOCK_REALTIME) : 0;
    if (channel != NULL && channel->bound &&
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_FILESYSTEM) {
        stl_be_p(command + HOST_FIELD(status), ASTRA_STATUS_ACCESS);
        goto done;
    }
    if (lduw_be_p(command + HOST_FIELD(service)) ==
            ASTRA_HOST_SERVICE_METRICS)
        astra_host_execute_metrics(s, command, physical, bytes,
                                   command_bytes, expected_generation);
    else if (lduw_be_p(command + HOST_FIELD(service)) ==
                 ASTRA_HOST_SERVICE_REMOTE_DESKTOP)
        astra_host_execute_remote_desktop(s, channel, command,
                                          expected_generation);
    else if (lduw_be_p(command + HOST_FIELD(service)) ==
                 ASTRA_HOST_SERVICE_ENTROPY)
        astra_host_execute_entropy(s, command, physical, bytes,
                                   command_bytes, expected_generation);
    else if (lduw_be_p(command + HOST_FIELD(service)) ==
                 ASTRA_HOST_SERVICE_VOLUMES)
        astra_host_execute_volumes(s, owner, channel, command, physical, bytes,
                                   command_bytes, expected_generation);
    else if (lduw_be_p(command + HOST_FIELD(service)) ==
                 ASTRA_HOST_SERVICE_AUDIO)
        astra_host_execute_audio(s, channel, command, physical, bytes,
                                 command_bytes, expected_generation);
    else
        astra_host_execute_fs(s, owner, channel, command, physical, bytes,
                              command_bytes, expected_generation, job);
done:
    if (non_fs) {
        qatomic_inc(&s->host.non_fs_commands);
        qatomic_add(&s->host.non_fs_execution_ns,
            qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - started);
    }
}

static int astra_host_native_open_flags(uint16_t flags, int *native_flags)
{
    const uint32_t allowed =
        ASTRA_VFS_OPEN_MASK;
    int native;

    if (native_flags == NULL || (flags & ~allowed) != 0 ||
        (flags & (ASTRA_VFS_OPEN_READ | ASTRA_VFS_OPEN_WRITE)) == 0)
        return 0;
    native = (flags & ASTRA_VFS_OPEN_WRITE) == 0 ? O_RDONLY :
             (flags & ASTRA_VFS_OPEN_READ) != 0 ? O_RDWR : O_WRONLY;
    if ((flags & ASTRA_VFS_OPEN_CREATE) != 0) native |= O_CREAT;
    if ((flags & ASTRA_VFS_OPEN_TRUNCATE) != 0) native |= O_TRUNC;
    if ((flags & ASTRA_VFS_OPEN_EXCLUSIVE) != 0) native |= O_EXCL;
    if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0) native |= O_DIRECTORY;
    if ((flags & ASTRA_VFS_OPEN_NONBLOCK) != 0) native |= O_NONBLOCK;
    *native_flags = native;
    return 1;
}

static uint32_t astra_host_publish_open(Astra68State *s, uint32_t owner, uint32_t process,
                                        AstraHostSession *session,
                                        uint32_t session_id,
                                        uint8_t *command, int fd,
                                        uint16_t flags)
{
    DIR *directory = NULL;
    struct stat st;
    uint32_t handle;

    if (fstat(fd, &st) < 0 || astra_host_publish_node(command, fd, "", session->mount) < 0) {
        uint32_t status = astra_host_status_from_errno(errno);

        close(fd);
        return status;
    }
    if ((flags & ASTRA_VFS_OPEN_DIRECTORY) == 0 && S_ISDIR(st.st_mode)) {
        close(fd);
        return ASTRA_STATUS_IS_DIR;
    }
    if ((flags & ASTRA_VFS_OPEN_DIRECTORY) != 0) {
        directory = fdopendir(fd);
        if (directory == NULL) {
            uint32_t status = astra_host_status_from_errno(errno);

            close(fd);
            return status;
        }
    }
    handle = astra_host_insert_file(s, owner, process, session, session_id, fd,
                                    directory, flags);
    if (handle == 0u) {
        /* The node record describes nothing the guest holds. */
        memset(command + HOST_FIELD(node), 0, sizeof(AstraHostNode));
        return ASTRA_STATUS_BAD_HANDLE;
    }
    stl_be_p(command + HOST_FIELD(handle), handle);
    return ASTRA_STATUS_OK;
}

static void astra_host_publish_filesystem_info(
    uint8_t *data, const struct statvfs *about)
{
    memset(data, 0, ASTRA_HOST_FILESYSTEM_INFO_SIZE);
    stl_be_p(data + offsetof(AstraHostFilesystemInfo, size),
             ASTRA_HOST_FILESYSTEM_INFO_SIZE);
    stl_be_p(data + offsetof(AstraHostFilesystemInfo, flags),
             (uint32_t)about->f_flag);
    stl_be_p(data + offsetof(AstraHostFilesystemInfo, block_size),
             (uint32_t)about->f_bsize);
    stl_be_p(data + offsetof(AstraHostFilesystemInfo, fragment_size),
             (uint32_t)about->f_frsize);
    astra_host_put64(data, offsetof(AstraHostFilesystemInfo, blocks_hi),
                     about->f_blocks);
    astra_host_put64(data,
                     offsetof(AstraHostFilesystemInfo, blocks_free_hi),
                     about->f_bfree);
    astra_host_put64(data,
                     offsetof(AstraHostFilesystemInfo, blocks_available_hi),
                     about->f_bavail);
    astra_host_put64(data, offsetof(AstraHostFilesystemInfo, files_hi),
                     about->f_files);
    astra_host_put64(data, offsetof(AstraHostFilesystemInfo, files_free_hi),
                     about->f_ffree);
    stl_be_p(data + offsetof(AstraHostFilesystemInfo, name_max),
             (uint32_t)about->f_namemax);
}

/* The scope `word` names on `channel`, copied out under files_lock. */
static bool astra_host_channel_scope(Astra68State *s,
                                     AstraHostChannel *channel,
                                     uint32_t word,
                                     AstraHostChannelScope *out)
{
    bool found = false;

    qemu_mutex_lock(&s->host.files_lock);
    for (uint32_t at = 0; at < channel->scope_count; ++at)
        if (channel->scopes[at].scope == word) {
            *out = channel->scopes[at];
            found = true;
            break;
        }
    qemu_mutex_unlock(&s->host.files_lock);
    return found;
}

static bool astra_host_fs_mutates(uint16_t operation, uint16_t flags)
{
    switch (operation) {
    case ASTRA_HOST_FS_OPEN:
        return (flags & (ASTRA_VFS_OPEN_WRITE | ASTRA_VFS_OPEN_CREATE |
                         ASTRA_VFS_OPEN_TRUNCATE | ASTRA_VFS_OPEN_APPEND)) != 0u;
    case ASTRA_HOST_FS_WRITE: case ASTRA_HOST_FS_TRUNCATE:
    case ASTRA_HOST_FS_MKDIR: case ASTRA_HOST_FS_UNLINK:
    case ASTRA_HOST_FS_RENAME: case ASTRA_HOST_FS_CHMOD:
    case ASTRA_HOST_FS_SYMLINK: case ASTRA_HOST_FS_LINK:
        return true;
    default:
        return false;
    }
}

/*
 * One owner speaks for the filesystem: the service that enforces the
 * grants (review M1), and only from a channel the kernel stamped PROVIDER
 * -- the caller checks that (review I1). A second claim by the holder is
 * idempotent; RELEASE gives it up with every session and handle the holder
 * opened. fs_owner changes only under files_lock, where SESSION_OPEN
 * re-checks it before inserting, so no session outlives the claim it was
 * opened under (review M2).
 */
static uint32_t astra_host_fs_claim(Astra68State *s, uint32_t owner,
                                    uint16_t flags, uint8_t *command)
{
    uint32_t holder;

    if (owner == 0u || (flags & ~ASTRA_HOST_FS_CLAIM_RELEASE) != 0u)
        return ASTRA_STATUS_INVALID;
    qemu_mutex_lock(&s->host.files_lock);
    holder = qatomic_read(&s->host.fs_owner);
    if ((flags & ASTRA_HOST_FS_CLAIM_RELEASE) != 0u) {
        if (holder != owner) {
            qemu_mutex_unlock(&s->host.files_lock);
            return ASTRA_STATUS_ACCESS;
        }
        GPtrArray *files;
        AstraHostStolenSessions sessions;

        /* The claim and everything opened under it leave together: a
         * re-claim racing this release starts with nothing of ours and
         * keeps everything of its own (Task 4 re-review N1). */
        qatomic_set(&s->host.fs_owner, 0u);
        files = astra_host_steal_files(s, owner, false);
        sessions = astra_host_steal_sessions(s, owner, 0u, 0u);
        qemu_mutex_unlock(&s->host.files_lock);
        astra_host_free_files(s, files);
        astra_host_free_sessions(s, sessions);
        return ASTRA_STATUS_OK;
    }
    if (holder == 0u)
        qatomic_set(&s->host.fs_owner, owner);
    if (holder == 0u || holder == owner) {
        qatomic_set(&s->host.fs_lost_owner, 0u);
        stl_be_p(command + HOST_FIELD(result_value), s->host.fs_epoch);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    return holder == 0u || holder == owner ? ASTRA_STATUS_OK :
                                             ASTRA_STATUS_ACCESS;
}

/*
 * Whole-mount sessions retain native identity only, so idle grants do not
 * prevent safe removal. A subdirectory session retains its exact O_PATH
 * object and inherits read-only. Every operation validates the generation
 * through the provider before using its root. Ids are never reused (next_session is never reset), so a
 * scope or handle of a closed session never names a later one (review M2).
 */
static int astra_host_provider_root(Astra68State *s, uint64_t mount, uint64_t generation)
{
    AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .operation = ASTRA_STORAGE_CONTROL_ROOT, .epoch = s->host.provider_epoch,
        .target = mount, .generation = generation};
    AstraStorageControlReply reply;
    int fd = -1, error;
    error = astra_host_storage_control(s, &request, -1, &reply, &fd);
    if (error != 0) {
        errno = error;
        return -1;
    }
    return fd;
}

/* Reuse canonical session retirement on media loss. Catalog polling also
 * retires idle descriptors; no subsequent application I/O is required.
 * QUIESCING is temporary admission refusal, never source loss. */
static void astra_host_reconcile_mounts(Astra68State *s)
{
    GArray *references = g_array_new(false, false, sizeof(AstraVolumeReference));
    GHashTableIter iterator;
    gpointer value;
    qemu_mutex_lock(&s->host.namespace_lock);
    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.sessions);
    while (g_hash_table_iter_next(&iterator, NULL, &value)) {
        AstraHostSession *session = value;
        AstraVolumeReference reference = {s->host.provider_epoch,
                                          session->mount, session->mount_generation};
        bool found = false;
        for (guint at = 0u; at < references->len; ++at) {
            const AstraVolumeReference *held = &g_array_index(references, AstraVolumeReference, at);
            if (held->id == reference.id && held->generation == reference.generation) {
                found = true;
                break;
            }
        }
        if (!found) g_array_append_val(references, reference);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    for (guint at = 0u; at < references->len; ++at) {
        const AstraVolumeReference *reference = &g_array_index(references, AstraVolumeReference, at);
        int root = astra_host_provider_root(s, reference->id, reference->generation);
        int error = errno;
        if (root >= 0) {
            close(root);
            continue;
        }
        if (error != ESTALE && error != ENODEV) continue;
        qemu_mutex_lock(&s->host.files_lock);
        AstraHostStolenSessions stolen = astra_host_steal_sessions(s, 0u,
            reference->id, reference->generation);
        qemu_mutex_unlock(&s->host.files_lock);
        astra_host_free_sessions(s, stolen);
    }
    qemu_mutex_unlock(&s->host.namespace_lock);
    g_array_free(references, true);
}

/* Safe removal fences new admission before examining the one canonical
 * description/session/share set. Active borrowers count until actual exit. */
static bool astra_host_mount_busy(Astra68State *s, uint64_t mount, uint64_t generation)
{
    GHashTableIter iterator;
    gpointer key, value;
    bool busy = false;
    qemu_mutex_lock(&s->host.files_lock);
    g_hash_table_iter_init(&iterator, s->host.sessions);
    while (g_hash_table_iter_next(&iterator, &key, &value)) {
        AstraHostSession *session = value;
        if (session->mount != mount || session->mount_generation != generation) continue;
        if (session->root >= 0 || session->references > 1u) { busy = true; break; }
        uint32_t id = GPOINTER_TO_UINT(key);
        GHashTableIter held;
        gpointer item;
        g_hash_table_iter_init(&held, s->host.files);
        while (g_hash_table_iter_next(&held, NULL, &item))
            if (((AstraHostFile *)item)->session == id) { busy = true; break; }
        if (busy) break;
        g_hash_table_iter_init(&held, s->host.shares);
        while (g_hash_table_iter_next(&held, NULL, &item))
            if (((AstraHostShare *)item)->session == id) { busy = true; break; }
        if (busy) break;
    }
    qemu_mutex_unlock(&s->host.files_lock);
    return busy;
}

/* The caller holds files_lock. Native PID names the requester, while the
 * kernel-stamped resource owner identifies its incarnation. A current channel
 * must confirm both; a recycled PID cannot claim a retained older token. */
static bool astra_host_share_requester(Astra68State *s, const AstraHostShare *share,
    uint32_t requester)
{
    bool held = false;
    if (share == NULL || share->process != requester) return false;
    for (uint32_t at = 0u; at < ASTRA_HOST_CHANNEL_COUNT; ++at) {
        const AstraHostChannel *channel = &s->host.channels[at];
        if (!channel->active || channel->process != requester) continue;
        if (channel->owner != share->owner) return false;
        held = true;
    }
    return held;
}

static uint32_t astra_host_volume_control(Astra68State *s, uint32_t owner,
    AstraHostChannel *channel, uint8_t *data)
{
    if (channel == NULL || !channel->provider || channel->bound || owner == 0u ||
        owner != qatomic_read(&s->host.fs_owner)) return ASTRA_STATUS_ACCESS;
    uint32_t requester = ldl_be_p(data + 56u);
    uint32_t operation = ldl_be_p(data + 24u);
    uint64_t image_share = ldq_be_p(data + 48u);
    AstraStorageControlRequest request = {.version = ASTRA_STORAGE_PROVIDER_VERSION,
        .epoch = ldq_be_p(data), .target = ldq_be_p(data + 8u),
        .generation = ldq_be_p(data + 16u), .flags = ldl_be_p(data + 28u),
        .byte_limit = ldq_be_p(data + 32u), .inode_limit = ldq_be_p(data + 40u)};
    AstraStorageControlReply reply;
    AstraVolumeReference reference = {0};
    AstraHostDescription *description = NULL;
    int output = -1, input = -1, error = EINVAL;
    if (requester == 0u || ldl_be_p(data + 60u) != 0u) return ASTRA_STATUS_INVALID;
    qemu_mutex_lock(&s->host.namespace_lock);
    if (operation == ASTRA_VOLUME_IMAGE) {
        if (request.epoch || request.target || request.generation || request.flags ||
            request.byte_limit || request.inode_limit || !image_share) goto done;
        qemu_mutex_lock(&s->host.files_lock);
        AstraHostShare *share = g_hash_table_lookup(s->host.shares, &image_share);
        AstraHostSession *session = share == NULL ? NULL :
            g_hash_table_lookup(s->host.sessions, GUINT_TO_POINTER(share->session));
        if (!astra_host_share_requester(s, share, requester) || share->description == NULL ||
            session == NULL || session->closing) error = EACCES;
        else if ((share->description->flags & ASTRA_VFS_OPEN_WRITE) != 0u ||
                 (share->description->flags & ASTRA_VFS_OPEN_READ) == 0u ||
                 share->description->directory != NULL) error = EACCES;
        else {
            description = share->description;
            g_atomic_int_inc(&description->references);
            request.epoch = s->host.provider_epoch;
            request.target = session->mount; request.generation = session->mount_generation;
            struct stat info;
            error = fstat(description->fd, &info) == 0 && S_ISREG(info.st_mode) ? 0 : EINVAL;
            if (error == 0) { request.backing_object = info.st_ino; input = description->fd; }
        }
        qemu_mutex_unlock(&s->host.files_lock);
        if (error != 0) goto done;
        request.operation = ASTRA_STORAGE_CONTROL_IMAGE;
    } else {
        if (image_share || !request.epoch || !request.target || !request.generation) goto done;
        if (operation == ASTRA_VOLUME_MOUNT_SOURCE) {
            if (request.flags & ~ASTRA_VOLUME_READ_ONLY) goto done;
            request.operation = ASTRA_STORAGE_CONTROL_MOUNT;
        } else if (operation == ASTRA_VOLUME_UNMOUNT) {
            if (request.flags || request.byte_limit || request.inode_limit) goto done;
            request.operation = ASTRA_STORAGE_CONTROL_QUIESCE;
            error = astra_host_storage_control(s, &request, -1, &reply, &output);
            if (error != 0) goto done;
            if (astra_host_mount_busy(s, request.target, request.generation)) {
                request.operation = ASTRA_STORAGE_CONTROL_RESUME;
                error = astra_host_storage_control(s, &request, -1, &reply, &output);
                if (error == 0) error = EBUSY;
                goto done;
            }
            request.operation = ASTRA_STORAGE_CONTROL_UNMOUNT;
        } else if (operation == ASTRA_VOLUME_EJECT || operation == ASTRA_VOLUME_RELEASE_IMAGE_SOURCE) {
            if (request.flags || request.byte_limit || request.inode_limit) goto done;
            request.operation = operation == ASTRA_VOLUME_EJECT ? ASTRA_STORAGE_CONTROL_EJECT :
                ASTRA_STORAGE_CONTROL_RELEASE_IMAGE_SOURCE;
        } else goto done;
    }
    error = astra_host_storage_control(s, &request, input, &reply, &output);
    if (error != 0 && operation == ASTRA_VOLUME_UNMOUNT) {
        int first = error;
        request.operation = ASTRA_STORAGE_CONTROL_RESUME;
        (void)astra_host_storage_control(s, &request, -1, &reply, &output);
        error = first;
    }
    if (error == 0) {
        if (operation == ASTRA_VOLUME_UNMOUNT || operation == ASTRA_VOLUME_EJECT ||
            operation == ASTRA_VOLUME_RELEASE_IMAGE_SOURCE)
            reference = (AstraVolumeReference){request.epoch, request.target, request.generation};
        else if (reply.count != 1u || reply.records[0].id != reply.object ||
            reply.records[0].generation == 0u || reply.epoch != s->host.provider_epoch)
            error = EPROTO;
        else reference = (AstraVolumeReference){reply.epoch, reply.records[0].id, reply.records[0].generation};
        if (error == 0) {
            stq_be_p(data, reference.epoch); stq_be_p(data + 8u, reference.id);
            stq_be_p(data + 16u, reference.generation);
        }
    }
done:
    if (output >= 0) close(output);
    if (description != NULL) astra_host_description_release(description);
    qemu_mutex_unlock(&s->host.namespace_lock);
    return astra_host_status_from_errno(error);
}

static uint32_t astra_host_fs_session_open(Astra68State *s, uint32_t owner,
                                           uint8_t *command, uint16_t flags,
                                           const char *path, const uint8_t *target)
{
    uint32_t parent_id = ldl_be_p(command + HOST_FIELD(session));
    AstraHostSession *parent = NULL;
    AstraHostSession *session;
    AstraHostSession authority = {0};
    uint32_t id;
    int base = -1, root = -1, error = 0;
    bool borrowed = false, lazy = false;
    AstraResolution resolved;
    const int directory_flags = O_PATH | O_DIRECTORY;

    if ((flags & ~ASTRA_HOST_FS_SESSION_READ_ONLY) != 0u ||
        ldl_be_p(command + HOST_FIELD(handle)) != 0u ||
        astra_host_get64(command, HOST_FIELD(value_hi)) != 0u)
        return ASTRA_STATUS_INVALID;
    if (parent_id == 0u) {
        if (target == NULL || ldq_be_p(target) != s->host.provider_epoch)
            return target == NULL ? ASTRA_STATUS_INVALID : ASTRA_STATUS_LOST;
        authority.mount = ldq_be_p(target + 8u);
        authority.mount_generation = ldq_be_p(target + 16u);
        if (authority.mount == 0u || authority.mount_generation == 0u)
            return ASTRA_STATUS_INVALID;
        lazy = path[0] == '\0' || strcmp(path, "/") == 0;
    } else {
        if (target == NULL || ldq_be_p(target) != 0u || ldq_be_p(target + 8u) != 0u ||
            ldq_be_p(target + 16u) != 0u)
            return ASTRA_STATUS_INVALID;
        parent = astra_host_session_acquire(s, owner, parent_id);
        if (parent == NULL)
            return ASTRA_STATUS_BAD_HANDLE;
        flags |= parent->flags & ASTRA_HOST_FS_SESSION_READ_ONLY;
        authority.mount = parent->mount;
        authority.mount_generation = parent->mount_generation;
        base = parent->root;
        lazy = base < 0 && (path[0] == '\0' || strcmp(path, "/") == 0);
    }
    if (base < 0) {
        base = astra_host_provider_root(s, authority.mount, authority.mount_generation);
        borrowed = true;
    }
    if (base >= 0) {
        struct statvfs about;
        if (fstatvfs(base, &about) != 0) {
            error = errno;
            if (borrowed) close(base);
            if (parent != NULL) astra_host_session_release(s, parent);
            return astra_host_status_from_errno(error);
        }
        if (about.f_flag & ST_RDONLY)
            flags |= ASTRA_HOST_FS_SESSION_READ_ONLY;
        if (!lazy)
            root = astra_host_open_resolved(s, &authority, base, -1,
                path[0] == '\0' ? "/" : path, directory_flags, 0u,
                ASTRA_RESOLVE_FOLLOW_FINAL, ASTRA_RESOLVE_LINK_MAX, &resolved);
        if (!lazy && root < 0)
            error = errno;
        if (borrowed) close(base);
    } else
        error = errno;
    if (parent != NULL)
        astra_host_session_release(s, parent);
    if (base < 0 || (!lazy && root < 0))
        return error == EXDEV ? ASTRA_STATUS_ACCESS : astra_host_status_from_errno(error);
    session = g_new0(AstraHostSession, 1);
    session->root = root;
    session->owner = owner;
    session->requester = ldl_be_p(target + 24u);
    session->mount = authority.mount;
    session->mount_generation = authority.mount_generation;
    session->flags = flags;
    session->references = 1u;
    qemu_mutex_lock(&s->host.files_lock);
    /* The claim may have gone since the command's check (review M2). */
    if (qatomic_read(&s->host.fs_owner) != owner) {
        qemu_mutex_unlock(&s->host.files_lock);
        if (root >= 0) close(root);
        g_free(session);
        return ASTRA_STATUS_ACCESS;
    }
    /* A stale scope must never name a later session, even after exhaustion. */
    if (s->host.next_session == UINT32_MAX) {
        qemu_mutex_unlock(&s->host.files_lock);
        if (root >= 0) close(root);
        g_free(session);
        return ASTRA_STATUS_LIMIT;
    }
    id = ++s->host.next_session;
    if (lazy) {
        /* Admission charged the root loan; no descriptor remains retained. */
        assert(s->host.fs_descriptors != 0u);
        --s->host.fs_descriptors;
    }
    g_hash_table_insert(s->host.sessions, GUINT_TO_POINTER(id), session);
    astra_host_usage_changed_locked(s);
    qemu_mutex_unlock(&s->host.files_lock);
    stl_be_p(command + HOST_FIELD(result_value), id);
    return ASTRA_STATUS_OK;
}

/* The session goes, and every handle opened in it, whoever opened them:
 * revoking a grant leaves nothing usable behind. */
static uint32_t astra_host_fs_session_close(Astra68State *s, uint32_t owner,
                                            uint32_t id)
{
    AstraHostSession *session;

    qemu_mutex_lock(&s->host.files_lock);
    session = g_hash_table_lookup(s->host.sessions, GUINT_TO_POINTER(id));
    if (session == NULL || session->owner != owner || session->closing) {
        qemu_mutex_unlock(&s->host.files_lock);
        return ASTRA_STATUS_BAD_HANDLE;
    }
    session->closing = true;
    g_hash_table_steal(s->host.sessions, GUINT_TO_POINTER(id));
    astra_host_usage_changed_locked(s);
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_release_session_files(s, id);
    astra_host_session_release(s, session);
    return ASTRA_STATUS_OK;
}

/*
 * RENAME's and LINK's second base: an open directory of this session,
 * held by reference only. Its descriptor cannot close while referenced,
 * and taking no second file mutex means two renames naming the same two
 * directories in opposite order cannot deadlock.
 */
static AstraHostFile *astra_host_fs_base(Astra68State *s, uint32_t owner,
                                         uint32_t session_id,
                                         uint32_t handle)
{
    AstraHostFile *file = astra_host_file_acquire(s, owner, handle);

    if (file != NULL &&
        (file->session != session_id || file->description->directory == NULL)) {
        astra_host_file_release(s, file);
        file = NULL;
    }
    return file;
}

static bool astra_host_lost_file(Astra68State *s, uint32_t owner,
                                 uint32_t handle, bool close_handle)
{
    bool lost;

    qemu_mutex_lock(&s->host.files_lock);
    AstraHostFile *identity = g_hash_table_lookup(s->host.lost_handles, GUINT_TO_POINTER(handle));
    lost = owner != 0u && identity != NULL && identity->owner == owner;
    if (lost && close_handle) {
        g_hash_table_remove(s->host.lost_handles, GUINT_TO_POINTER(handle));
        astra_host_usage_changed_locked(s);
    }
    qemu_mutex_unlock(&s->host.files_lock);
    return lost;
}

/* The parent directory and leaf of `path`: relative beneath the open
 * directory `base`, or absolute beneath the session root when base < 0. */
static void astra_host_execute_fs(Astra68State *s, uint32_t owner,
                                  AstraHostChannel *channel,
                                  uint8_t *command,
                                  uint32_t physical, uint32_t bytes,
                                  uint32_t command_bytes,
                                  uint32_t expected_generation, AstraHostJob *job)
{
    uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
    uint16_t flags = lduw_be_p(command + HOST_FIELD(flags));
    uint32_t handle = ldl_be_p(command + HOST_FIELD(handle));
    uint32_t session_id = ldl_be_p(command + HOST_FIELD(session));
    mode_t mode = ldl_be_p(command + HOST_FIELD(value_lo)) & 07777u;
    uint32_t operation_index = 0u;
    uint64_t started_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    bool bound = channel != NULL && channel->bound;
    AstraHostChannelScope scope = {0};
    AstraHostSession *session = NULL;
    AstraHostFile *file = NULL;
    uint32_t session_owner;
    char *path = NULL;
    char *path2 = NULL;
    uint32_t status = ASTRA_STATUS_INVALID;
    int root = -1;
    bool borrowed_root = false;
    int base;
    bool namespace_locked = false;
    bool descriptor_reserved = false;
    bool directory_reserved = false;
    AstraResolution resolved = { .parent = -1 };

    stl_be_p(command + HOST_FIELD(status), 0u);
    stl_be_p(command + HOST_FIELD(result_length), 0u);
    stl_be_p(command + HOST_FIELD(result_value), 0u);
    memset(command + HOST_FIELD(node), 0, sizeof(AstraHostNode));
    if (ldl_be_p(command + HOST_FIELD(size)) != ASTRA_HOST_COMMAND_SIZE ||
        lduw_be_p(command + HOST_FIELD(version)) !=
            ASTRA_HOST_COMMAND_VERSION ||
        lduw_be_p(command + HOST_FIELD(service)) !=
            ASTRA_HOST_SERVICE_FILESYSTEM ||
        ldl_be_p(command + HOST_FIELD(generation)) != expected_generation) {
        qatomic_inc(&s->host.operation_counts[0]);
        goto done;
    }
    /* Unknown before claimed: an operation code is not authority, and the
     * raw channel benchmarks time exactly this refusal. */
    if (operation == 0u || operation > ASTRA_HOST_FS_MAX) {
        qatomic_inc(&s->host.operation_counts[0]);
        status = ASTRA_STATUS_UNSUPPORTED;
        goto done;
    }
    operation_index = operation;
    qatomic_inc(&s->host.operation_counts[operation_index]);
    for (size_t index = HOST_FIELD(reserved);
         index < ASTRA_HOST_COMMAND_SIZE; ++index)
        if (command[index] != 0u)
            goto done;
    if (bound) {
        /* A bound channel names one of its kernel-attached scopes by the
         * command's session, never the claim or another session's; its
         * owner field does not exist, and the submitting owner keys only
         * the handles it opens (FS15, FS44). */
        if (operation == ASTRA_HOST_FS_CLAIM ||
            operation == ASTRA_HOST_FS_SESSION_OPEN ||
            operation == ASTRA_HOST_FS_SESSION_CLOSE ||
            !astra_host_channel_scope(s, channel, session_id, &scope)) {
            status = ASTRA_STATUS_ACCESS;
            goto done;
        }
        if (!scope.live || scope.owner != qatomic_read(&s->host.fs_owner)) {
            status = ASTRA_STATUS_LOST;
            stl_be_p(command + HOST_FIELD(result_value), s->host.fs_epoch);
            goto done;
        }
        if ((scope.flags & ASTRA_HOST_SCOPE_READ_ONLY) != 0u &&
            astra_host_fs_mutates(operation, flags)) {
            status = ASTRA_STATUS_READ_ONLY;
            goto done;
        }
        qatomic_inc(&s->host.scoped_commands);
        session_owner = scope.owner;
    } else {
        if (operation == ASTRA_HOST_FS_CLAIM) {
            /* The kernel names the provider (review I1): HOST_EXECUTE and
             * every other channel are refused, claimed or not. */
            status = channel != NULL && channel->provider ?
                     astra_host_fs_claim(s, owner, flags, command) :
                     ASTRA_STATUS_ACCESS;
            goto done;
        }
        /* One owner speaks for the filesystem: the service that enforces
         * the grants. Every other HOST_DEVICE holder is refused here
         * (review M1). */
        if (owner == 0u || owner != qatomic_read(&s->host.fs_owner)) {
            status = owner != 0u && owner == qatomic_read(&s->host.fs_lost_owner) ?
                ASTRA_STATUS_LOST : ASTRA_STATUS_ACCESS;
            goto done;
        }
        session_owner = owner;
    }
    status = astra_host_fs_operands(s, physical, bytes, command_bytes,
                                    command, &path, &path2);
    if (status != ASTRA_STATUS_OK)
        goto done;
    if (operation == ASTRA_HOST_FS_SESSION_OPEN) {
        status = ASTRA_STATUS_INVALID;
        qemu_mutex_lock(&s->host.namespace_lock);
        namespace_locked = true;
        if (!astra_host_admit(s, owner, 0u, 0u, 1u, 0u)) {
            status = ASTRA_STATUS_LIMIT;
            goto done;
        }
        descriptor_reserved = true;
        uint32_t target_bytes = ldl_be_p(command + HOST_FIELD(data_length));
        const uint8_t *target = NULL;
        {
            if (target_bytes != sizeof(AstraHostSessionOpen))
                goto done;
            target = astra_host_command_data(s, physical, bytes, command_bytes, command,
                sizeof(AstraHostSessionOpen));
            if (target == NULL || ldl_be_p(target + 28u) != 0u)
                goto done;
        }
        status = astra_host_fs_session_open(s, owner, command, flags, path, target);
        descriptor_reserved = status != ASTRA_STATUS_OK;
        goto done;
    }
    if (operation == ASTRA_HOST_FS_CLOSE && flags == 0u &&
        path[0] == '\0' && path2[0] == '\0' &&
        astra_host_lost_file(s, owner, handle, true)) {
        status = ASTRA_STATUS_OK;
        goto done;
    }
    session = astra_host_session_acquire(s, session_owner, session_id);
    if (session == NULL) {
        status = bound ? ASTRA_STATUS_LOST : ASTRA_STATUS_BAD_HANDLE;
        if (bound)
            stl_be_p(command + HOST_FIELD(result_value), s->host.fs_epoch);
        goto done;
    }
    astra_host_job_session(s, job, session);
    if (operation == ASTRA_HOST_FS_SESSION_CLOSE) {
        status = astra_host_fs_session_close(s, owner, session_id);
        goto done;
    }
    if ((session->flags & ASTRA_HOST_FS_SESSION_READ_ONLY) != 0u &&
        astra_host_fs_mutates(operation, flags)) {
        status = ASTRA_STATUS_READ_ONLY;
        goto done;
    }
    /* Paths resolve beneath the session root, or beneath an open
     * directory of this session; handles answer only in their session. */
    root = session->root;
    if (operation != ASTRA_HOST_FS_CLOSE) {
        int admitted = astra_host_provider_root(s, session->mount, session->mount_generation);
        if (admitted < 0) {
            status = astra_host_status_from_errno(errno);
            goto done;
        }
        if (root < 0) {
            root = admitted;
            borrowed_root = true;
        } else close(admitted);
    }
    if (operation != ASTRA_HOST_FS_CLOSE && operation != ASTRA_HOST_FS_READ &&
        operation != ASTRA_HOST_FS_SYNC &&
        operation != ASTRA_HOST_FS_SEEK && operation != ASTRA_HOST_FS_FILE_FLAGS) {
        /* Table locking cannot span these calls; publishing an open reenters it.
         * Acquire namespace before any per-file lock, including rename bases. */
        qemu_mutex_lock(&s->host.namespace_lock);
        namespace_locked = true;
    }
    status = ASTRA_STATUS_INVALID;
    /* Fork may inherit a disconnected description; it never reopens a path. */
    if (operation == ASTRA_HOST_FS_SHARE && flags == 0u && path[0] == '\0' &&
        path2[0] == '\0' && astra_host_lost_file(s, owner, handle, false)) {
        uint64_t token;

        status = astra_host_share_file(s, NULL, owner, handle, session, session_id,
                                       bound ? scope.files : 0u, &token);
        if (status == ASTRA_STATUS_OK)
            astra_host_put64(command, HOST_FIELD(value_hi), token);
        goto done;
    }
    if (handle != 0u &&
        operation != ASTRA_HOST_FS_CLOSE && operation != ASTRA_HOST_FS_READ &&
        operation != ASTRA_HOST_FS_WRITE && operation != ASTRA_HOST_FS_SYNC &&
        operation != ASTRA_HOST_FS_TRUNCATE &&
        operation != ASTRA_HOST_FS_READDIR) {
        file = astra_host_session_file(s, owner, session_id, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
            goto done;
        }
        if (path[0] != '\0' && file->description->directory == NULL) {
            status = ASTRA_STATUS_NOT_DIR;
            goto done;
        }
    }
    if (operation != ASTRA_HOST_FS_CLOSE) {
        int live = astra_host_rooted(s, session, root, root);
        if (live != 0) {
            status = astra_host_status_from_errno(-live);
            goto done;
        }
    }
    base = file != NULL ? file->description->fd : -1;
    if (file != NULL && file->description->directory != NULL &&
        operation != ASTRA_HOST_FS_PATH) {
        int confined = astra_host_rooted(s, session, root, base);

        if (confined < 0) {
            status = astra_host_status_from_errno(-confined);
            goto done;
        }
    }

    switch (operation) {
    case ASTRA_HOST_FS_PATH: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);
        uint32_t length = 0u;
        int result;

        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
            break;
        }
        if (file->description->directory == NULL) {
            status = ASTRA_STATUS_NOT_DIR;
            break;
        }
        if (data == NULL || capacity == 0u || flags != 0u ||
            path[0] != '\0' || path2[0] != '\0')
            break;
        result = astra_host_descriptor_path(s, session, root, file->description->fd,
                                               (char *)data, capacity,
                                               &length);
        status = result == 0 ? ASTRA_STATUS_OK : result == -ERANGE ?
            ASTRA_STATUS_BUFFER_TOO_SMALL :
            astra_host_status_from_errno(-result);
        if (status == ASTRA_STATUS_OK &&
            !g_utf8_validate((char *)data, length, NULL))
            status = ASTRA_STATUS_INVALID;
        if (status == ASTRA_STATUS_OK)
            stl_be_p(command + HOST_FIELD(result_length), length);
        break;
    }
    case ASTRA_HOST_FS_OPEN: {
        int native_flags;
        int fd;

        if (!astra_host_native_open_flags(flags, &native_flags))
            break;
        directory_reserved = (flags & ASTRA_VFS_OPEN_DIRECTORY) != 0u;
        if (!astra_host_admit(s, owner, session_id, bound ? scope.files : 0u,
                              1u, directory_reserved)) {
            directory_reserved = false;
            status = ASTRA_STATUS_LIMIT;
            break;
        }
        descriptor_reserved = true;
        fd = astra_host_open_resolved(s, session, root, base, path, native_flags, mode,
            (flags & ASTRA_VFS_OPEN_NOFOLLOW) ||
            ((flags & (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ==
                (ASTRA_VFS_OPEN_CREATE | ASTRA_VFS_OPEN_EXCLUSIVE)) ? 0u :
                ASTRA_RESOLVE_FOLLOW_FINAL, ASTRA_RESOLVE_LINK_MAX, &resolved);
        if (fd < 0) {
            status = astra_host_path_status(errno, &resolved);
            break;
        }
        status = astra_host_publish_open(s, owner, channel != NULL ? channel->process : 0u, session, session_id,
                                         command, fd, flags);
        if (status == ASTRA_STATUS_OK)
            descriptor_reserved = directory_reserved = false;
        break;
    }
    case ASTRA_HOST_FS_CLOSE:
        status = astra_host_close_file(s, owner, session_id, handle) ?
                 ASTRA_STATUS_OK : ASTRA_STATUS_BAD_HANDLE;
        break;
    case ASTRA_HOST_FS_READ: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);
        uint64_t offset = astra_host_get64(command, HOST_FIELD(offset_hi));
        bool sequential = flags & ASTRA_HOST_FS_IO_SEQUENTIAL;
        ssize_t moved;
        off_t position;

        if (flags & ~ASTRA_HOST_FS_IO_SEQUENTIAL || path[0] || path2[0] ||
            (!sequential && offset > INT64_MAX))
            break;
        file = astra_host_session_file(s, owner, session_id, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
        } else if ((file->description->flags & ASTRA_VFS_OPEN_READ) == 0) {
            status = ASTRA_STATUS_ACCESS;
        } else if (file->description->directory != NULL) {
            status = ASTRA_STATUS_IS_DIR;
        } else if (data == NULL) {
            status = ASTRA_STATUS_INVALID;
        } else {
            moved = sequential ? read(file->description->fd, data, capacity) :
                    pread(file->description->fd, data, capacity, offset);
            if (moved < 0) {
                status = astra_host_path_status(errno, &resolved);
            } else {
                position = sequential ? lseek(file->description->fd, 0, SEEK_CUR) :
                                        (off_t)offset + moved;
                if (position < 0) {
                    status = astra_host_path_status(errno, &resolved);
                } else {
                    stl_be_p(command + HOST_FIELD(result_length), moved);
                    astra_host_put64(command, HOST_FIELD(value_hi), position);
                    status = ASTRA_STATUS_OK;
                }
            }
        }
        break;
    }
    case ASTRA_HOST_FS_WRITE: {
        uint32_t length = ldl_be_p(command + HOST_FIELD(data_length));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, length);
        uint64_t offset = astra_host_get64(command, HOST_FIELD(offset_hi));
        uint64_t max_end = astra_host_get64(command, HOST_FIELD(value_hi));
        bool sequential = flags & ASTRA_HOST_FS_IO_SEQUENTIAL;
        bool append;
        ssize_t moved;
        off_t position;
        off_t saved = 0;

        if (flags & ~(ASTRA_HOST_FS_IO_SEQUENTIAL | ASTRA_HOST_FS_WRITE_APPEND) ||
            path[0] || path2[0] || (!sequential && offset > INT64_MAX))
            break;
        file = astra_host_session_file(s, owner, session_id, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
        } else if ((file->description->flags & ASTRA_VFS_OPEN_WRITE) == 0) {
            status = ASTRA_STATUS_ACCESS;
        } else if (file->description->directory != NULL) {
            status = ASTRA_STATUS_IS_DIR;
        } else if (data == NULL && length != 0u) {
            status = ASTRA_STATUS_INVALID;
        } else {
            append = flags & ASTRA_HOST_FS_WRITE_APPEND ||
                     (sequential && file->description->flags & ASTRA_VFS_OPEN_APPEND);
            if (length != 0u && max_end != UINT64_MAX) {
                struct stat metadata;
                off_t start;

                if (append) {
                    if (fstat(file->description->fd, &metadata) != 0) {
                        status = astra_host_path_status(errno, &resolved);
                        break;
                    }
                    start = metadata.st_size;
                } else {
                    start = sequential ? lseek(file->description->fd, 0, SEEK_CUR) :
                                         (off_t)offset;
                }
                if (start < 0) {
                    status = astra_host_path_status(errno, &resolved);
                    break;
                }
                if ((uint64_t)start >= max_end) {
                    status = ASTRA_STATUS_FILE_TOO_LARGE;
                    break;
                }
                if (length > max_end - (uint64_t)start)
                    length = max_end - (uint64_t)start;
            }
            if (append) {
#ifdef CONFIG_LINUX
                struct iovec vector = { .iov_base = data, .iov_len = length };

                saved = lseek(file->description->fd, 0, SEEK_CUR);
                if (saved < 0) {
                    status = astra_host_path_status(errno, &resolved);
                    break;
                }
                moved = pwritev2(file->description->fd, &vector, 1, -1, RWF_APPEND);
#else
                status = ASTRA_STATUS_UNSUPPORTED;
                break;
#endif
            } else {
                moved = sequential ? write(file->description->fd, data, length) :
                        pwrite(file->description->fd, data, length, offset);
            }
            if (moved < 0) {
                status = astra_host_path_status(errno, &resolved);
            } else {
                position = sequential || append ?
                           lseek(file->description->fd, 0, SEEK_CUR) :
                           (off_t)offset + moved;
                if (append && !sequential &&
                    lseek(file->description->fd, saved, SEEK_SET) < 0)
                    position = -1;
                if (position < 0) {
                    status = astra_host_path_status(errno, &resolved);
                } else {
                    stl_be_p(command + HOST_FIELD(result_length), moved);
                    astra_host_put64(command, HOST_FIELD(value_hi), position);
                    status = ASTRA_STATUS_OK;
                }
            }
        }
        break;
    }
    case ASTRA_HOST_FS_SEEK: {
        off_t position;

        if (flags > 2u || path[0] || path2[0] || file == NULL)
            break;
        if (file->description->directory != NULL) {
            int64_t delta = astra_host_get64(command, HOST_FIELD(offset_hi));

            if (delta != 0 || flags == 2u)
                break;
            if (flags == 0u) {
                rewinddir(file->description->directory);
                file->description->directory_cookie = 0;
            }
            position = file->description->directory_cookie;
        } else {
            position = lseek(file->description->fd,
                (int64_t)astra_host_get64(command, HOST_FIELD(offset_hi)),
                flags == 0u ? SEEK_SET : flags == 1u ? SEEK_CUR : SEEK_END);
        }
        if (position < 0)
            status = astra_host_path_status(errno, &resolved);
        else {
            astra_host_put64(command, HOST_FIELD(value_hi), position);
            status = ASTRA_STATUS_OK;
        }
        break;
    }
    case ASTRA_HOST_FS_SHARE: {
        uint64_t token;

        if (flags || path[0] || path2[0] || file == NULL)
            break;
        status = astra_host_share_file(s, file, owner, handle, session, session_id,
                                       bound ? scope.files : 0u, &token);
        if (status == ASTRA_STATUS_OK)
            astra_host_put64(command, HOST_FIELD(value_hi), token);
        break;
    }
    case ASTRA_HOST_FS_ADOPT:
        if (flags & ~ASTRA_HOST_FS_ADOPT_DISCARD || handle || path[0] || path2[0])
            break;
        status = astra_host_adopt_file(s, owner, channel != NULL ? channel->process : 0u, session, session_id,
            astra_host_get64(command, HOST_FIELD(offset_hi)),
            flags & ASTRA_HOST_FS_ADOPT_DISCARD, bound ? scope.files : 0u, &handle);
        if (status != ASTRA_STATUS_OK || flags & ASTRA_HOST_FS_ADOPT_DISCARD)
            break;
        if (astra_host_lost_file(s, owner, handle, false)) {
            stl_be_p(command + HOST_FIELD(handle), handle);
            break;
        }
        file = astra_host_session_file(s, owner, session_id, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
            break;
        }
        if (file->description->directory != NULL) {
            int confined = astra_host_rooted(s, session, root, file->description->fd);

            if (confined < 0) {
                status = astra_host_status_from_errno(-confined);
                astra_host_close_file(s, owner, session_id, handle);
                break;
            }
        }
        if (astra_host_publish_node(command, file->description->fd, "", session->mount) < 0) {
            status = astra_host_path_status(errno, &resolved);
            astra_host_close_file(s, owner, session_id, handle);
            memset(command + HOST_FIELD(node), 0, sizeof(AstraHostNode));
        } else {
            stl_be_p(command + HOST_FIELD(handle), handle);
        }
        break;
    case ASTRA_HOST_FS_FILE_FLAGS: {
        uint32_t wanted = ldl_be_p(command + HOST_FIELD(value_lo));

        if (flags & ~ASTRA_VFS_FILE_STATUS_MASK || wanted & ~ASTRA_VFS_FILE_STATUS_MASK ||
            wanted & ~flags || path[0] || path2[0] || file == NULL)
            break;
        if ((flags & ASTRA_VFS_OPEN_NONBLOCK) != 0u) {
            int native = fcntl(file->description->fd, F_GETFL);

            if (native < 0 || fcntl(file->description->fd, F_SETFL,
                    (native & ~O_NONBLOCK) |
                    (wanted & ASTRA_VFS_OPEN_NONBLOCK ? O_NONBLOCK : 0)) < 0) {
                status = astra_host_status_from_errno(errno);
                break;
            }
        }
        file->description->flags = (file->description->flags & ~flags) | wanted;
        stl_be_p(command + HOST_FIELD(value_hi), 0u);
        stl_be_p(command + HOST_FIELD(value_lo), file->description->flags);
        status = ASTRA_STATUS_OK;
        break;
    }
    case ASTRA_HOST_FS_SYNC:
        file = astra_host_session_file(s, owner, session_id, handle);
        status = file == NULL ? ASTRA_STATUS_BAD_HANDLE :
                 fsync(file->description->fd) == 0 ? ASTRA_STATUS_OK :
                 astra_host_path_status(errno, &resolved);
        break;
    case ASTRA_HOST_FS_TRUNCATE:
        file = astra_host_session_file(s, owner, session_id, handle);
        if (file == NULL) {
            status = ASTRA_STATUS_BAD_HANDLE;
        } else if ((file->description->flags & ASTRA_VFS_OPEN_WRITE) == 0) {
            status = ASTRA_STATUS_ACCESS;
        } else if (file->description->directory != NULL) {
            status = ASTRA_STATUS_IS_DIR;
        } else {
            status = ftruncate(
                file->description->fd, astra_host_get64(command, HOST_FIELD(value_hi))) == 0 ?
                ASTRA_STATUS_OK : astra_host_path_status(errno, &resolved);
        }
        break;
    case ASTRA_HOST_FS_STAT: {
        int fd;
#ifdef CONFIG_LINUX
        const int metadata_flags = O_PATH;
#else
        const int metadata_flags = O_RDONLY;
#endif

        if (flags & ~(ASTRA_HOST_FS_STAT_FOLLOW | ASTRA_HOST_FS_STAT_MISSING |
                      ASTRA_HOST_FS_STAT_BUDGET | ASTRA_HOST_FS_STAT_ENTRY))
            break;
        if ((flags & (ASTRA_HOST_FS_STAT_FOLLOW | ASTRA_HOST_FS_STAT_ENTRY)) ==
            (ASTRA_HOST_FS_STAT_FOLLOW | ASTRA_HOST_FS_STAT_ENTRY))
            break;
        if (base >= 0 && path[0] == '\0') {
            status = astra_host_publish_node(command, base, "", session->mount) == 0 ?
                     ASTRA_STATUS_OK : astra_host_path_status(errno, &resolved);
            break;
        }
        fd = astra_host_open_resolved(s, session, root, base, path, metadata_flags, 0,
            flags & ASTRA_HOST_FS_STAT_FOLLOW ? ASTRA_RESOLVE_FOLLOW_FINAL :
            flags & ASTRA_HOST_FS_STAT_ENTRY ? ASTRA_RESOLVE_ENTRY : 0u,
            flags & ASTRA_HOST_FS_STAT_BUDGET ?
                ldl_be_p(command + HOST_FIELD(offset_lo)) : ASTRA_RESOLVE_LINK_MAX,
            &resolved);
        status = fd < 0 ?
                 errno == ENOENT && resolved.missing &&
                    (flags & ASTRA_HOST_FS_STAT_MISSING) ? ASTRA_STATUS_OK :
                    astra_host_path_status(errno, &resolved) :
                 astra_host_publish_node(command, fd, "", session->mount) == 0 ?
                 ASTRA_STATUS_OK : astra_host_path_status(errno, &resolved);
        stl_be_p(command + HOST_FIELD(value_hi), resolved.links);
        if (fd >= 0)
            close(fd);
        break;
    }
    case ASTRA_HOST_FS_READDIR: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint32_t limit = ldl_be_p(command + HOST_FIELD(value_lo));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);
        uint64_t cookie = astra_host_get64(command, HOST_FIELD(offset_hi));
        uint32_t used = 0, count = 0;
        DIR *directory;
        int directory_fd;
        bool persistent = false;

        if (data == NULL || capacity < ASTRA_VFS_DIRENT_HEADER + 1u ||
            limit == 0 || flags != 0 ||
            (cookie > LONG_MAX && cookie != ASTRA_VFS_CURSOR_CURRENT) ||
            (handle != 0u && path[0] != '\0') ||
            (handle == 0u && cookie == ASTRA_VFS_CURSOR_CURRENT))
            break;
        if (handle != 0u) {
            file = astra_host_session_file(s, owner, session_id, handle);
            if (file == NULL || file->description->directory == NULL) {
                status = ASTRA_STATUS_BAD_HANDLE;
                break;
            }
            int confined = astra_host_rooted(s, session, root, file->description->fd);
            if (confined < 0) {
                status = astra_host_status_from_errno(-confined);
                break;
            }
            directory = file->description->directory;
            persistent = true;
            if (cookie != ASTRA_VFS_CURSOR_CURRENT) {
                seekdir(directory, (long)cookie);
                file->description->directory_cookie = cookie;
            }
        } else {
            directory_fd = astra_host_open_resolved(
                s, session, root, -1, path, O_RDONLY | O_DIRECTORY, 0,
                ASTRA_RESOLVE_FOLLOW_FINAL, ASTRA_RESOLVE_LINK_MAX, &resolved);
            if (directory_fd < 0) {
                status = astra_host_path_status(errno, &resolved);
                break;
            }
            directory = fdopendir(directory_fd);
            if (directory == NULL) {
                status = astra_host_path_status(errno, &resolved);
                close(directory_fd);
                break;
            }
            if (cookie != 0)
                seekdir(directory, (long)cookie);
        }
        status = ASTRA_STATUS_OK;
        while (count < limit) {
            struct dirent *entry;
            long before = telldir(directory);
            long after;
            size_t length;
            uint8_t *record;

            if (before < 0) {
                status = astra_host_path_status(errno, &resolved);
                break;
            }
            errno = 0;
            do {
                entry = readdir(directory);
            } while (entry != NULL &&
                     (strcmp(entry->d_name, ".") == 0 ||
                      strcmp(entry->d_name, "..") == 0));
            if (entry == NULL) {
                status = errno == 0 ? ASTRA_STATUS_NOT_FOUND :
                         astra_host_path_status(errno, &resolved);
                cookie = 0;
                break;
            }
            length = strlen(entry->d_name);
            if (length == 0 || length >= ASTRA_VFS_NAME_MAX ||
                ASTRA_VFS_DIRENT_HEADER + length > capacity - used) {
                seekdir(directory, before);
                status = ASTRA_STATUS_BUFFER_TOO_SMALL;
                break;
            }
            if (!g_utf8_validate(entry->d_name, length, NULL)) {
                seekdir(directory, before);
                status = ASTRA_STATUS_INVALID;
                break;
            }
            AstraResolution child_resolution;
            int child_fd = astra_host_open_resolved(s, session, root, dirfd(directory),
                entry->d_name, O_PATH, 0u, 0u, ASTRA_RESOLVE_LINK_MAX, &child_resolution);
            int metadata = child_fd < 0 ? -1 : astra_host_publish_node(command, child_fd, "", session->mount);
            int metadata_error = errno;
            if (child_fd >= 0)
                close(child_fd);
            if (metadata < 0) {
                errno = metadata_error;
                if (errno == ENOENT)
                    continue;
                status = astra_host_path_status(errno, &resolved);
                seekdir(directory, before);
                break;
            }
            after = telldir(directory);
            if (after < 0) {
                status = astra_host_path_status(errno, &resolved);
                seekdir(directory, before);
                break;
            }
            record = data + used;
            memcpy(record, command + NODE_FIELD(kind), 2);
            memcpy(record + 2, command + NODE_FIELD(mode), 2);
            stw_be_p(record + 4, MIN(ldl_be_p(command + NODE_FIELD(nlink)),
                                    UINT16_MAX));
            record[6] = length;
            record[7] = 0;
            memcpy(record + 8, command + NODE_FIELD(uid), 4);
            memcpy(record + 12, command + NODE_FIELD(gid), 4);
            memcpy(record + 16, command + NODE_FIELD(size_hi), 8);
            memcpy(record + 24, command + NODE_FIELD(mtime), 8);
            memcpy(record + ASTRA_VFS_DIRENT_HEADER, entry->d_name, length);
            used += ASTRA_VFS_DIRENT_HEADER + length;
            cookie = after;
            ++count;
        }
        if (count != 0) {
            status = ASTRA_STATUS_OK;
            stl_be_p(command + HOST_FIELD(result_length), used);
            astra_host_put64(command, HOST_FIELD(value_hi), cookie);
        }
        if (persistent) {
            long position = telldir(directory);
            if (position >= 0)
                file->description->directory_cookie = position;
        } else {
            closedir(directory);
        }
        break;
    }
    case ASTRA_HOST_FS_MKDIR:
    case ASTRA_HOST_FS_UNLINK:
    case ASTRA_HOST_FS_RENAME:
    case ASTRA_HOST_FS_LINK:
    case ASTRA_HOST_FS_CHMOD:
    case ASTRA_HOST_FS_READLINK:
    case ASTRA_HOST_FS_SYMLINK: {
        AstraStoragePathReply reply;
        AstraHostFile *second = NULL;
        uint32_t broker_operation, broker_flags = 0u;
        uint32_t resolve = ASTRA_RESOLVE_ENTRY;
        uint32_t capacity = 0u;
        int second_base = -1, result;
        uint8_t *data = NULL;
        switch (operation) {
        case ASTRA_HOST_FS_MKDIR: broker_operation = ASTRA_STORAGE_PATH_MKDIR; break;
        case ASTRA_HOST_FS_UNLINK:
            if (flags & ~ASTRA_HOST_FS_UNLINK_DIRECTORY)
                goto done;
            broker_operation = ASTRA_STORAGE_PATH_UNLINK;
            broker_flags = flags & ASTRA_HOST_FS_UNLINK_DIRECTORY ? AT_REMOVEDIR : 0u;
            break;
        case ASTRA_HOST_FS_RENAME:
        case ASTRA_HOST_FS_LINK: {
            uint32_t second_handle = ldl_be_p(command + HOST_FIELD(value_lo));
            broker_operation = operation == ASTRA_HOST_FS_RENAME ?
                ASTRA_STORAGE_PATH_RENAME : ASTRA_STORAGE_PATH_LINK;
            if (second_handle == handle)
                second_base = base;
            else if (second_handle != 0u) {
                second = astra_host_fs_base(s, owner, session_id, second_handle);
                if (second == NULL) {
                    status = ASTRA_STATUS_BAD_HANDLE;
                    goto done;
                }
                second_base = second->description->fd;
            }
            break;
        }
        case ASTRA_HOST_FS_CHMOD:
            if (flags & ~ASTRA_VFS_AT_SYMLINK_NOFOLLOW)
                goto done;
            broker_operation = ASTRA_STORAGE_PATH_CHMOD;
            resolve = flags & ASTRA_VFS_AT_SYMLINK_NOFOLLOW ? 0u : ASTRA_RESOLVE_FOLLOW_FINAL;
            break;
        case ASTRA_HOST_FS_READLINK:
            broker_operation = ASTRA_STORAGE_PATH_READLINK;
            resolve = 0u;
            capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
            data = astra_host_command_data(s, physical, bytes, command_bytes, command, capacity);
            if (data == NULL || capacity == 0u)
                goto done;
            capacity = MIN(capacity, ASTRA_STORAGE_PROVIDER_PATH_MAX);
            break;
        default: broker_operation = ASTRA_STORAGE_PATH_SYMLINK; break;
        }
        int error = astra_host_provider_path(s, session, root, base, second_base,
            path, path2, broker_operation, broker_flags, mode, resolve,
            ASTRA_RESOLVE_LINK_MAX, capacity, &reply, &result);
        astra_host_resolution(&reply, &resolved);
        status = error == ERANGE ? ASTRA_STATUS_BUFFER_TOO_SMALL :
            astra_host_path_status(error, &resolved);
        if (operation == ASTRA_HOST_FS_READLINK && error == 0) {
            memcpy(data, reply.data, reply.length);
            stl_be_p(command + HOST_FIELD(result_length), reply.length);
        }
        if (second != NULL)
            astra_host_file_release(s, second);
        break;
    }
    case ASTRA_HOST_FS_FILESYSTEM_INFO: {
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command,
            ASTRA_HOST_FILESYSTEM_INFO_SIZE);
        struct statvfs about;
        int fd = -1;

        if (capacity < ASTRA_HOST_FILESYSTEM_INFO_SIZE || data == NULL)
            break;
        if (base >= 0 && path[0] == '\0') {
            status = fstatvfs(base, &about) == 0 ? ASTRA_STATUS_OK :
                     astra_host_path_status(errno, &resolved);
        } else {
            fd = astra_host_open_resolved(s, session, root, base, path, O_RDONLY, 0,
                                          ASTRA_RESOLVE_FOLLOW_FINAL, ASTRA_RESOLVE_LINK_MAX, &resolved);
            status = fd < 0 ? astra_host_path_status(errno, &resolved) :
                     fstatvfs(fd, &about) == 0 ? ASTRA_STATUS_OK :
                     astra_host_path_status(errno, &resolved);
        }
        if (status == ASTRA_STATUS_OK) {
            astra_host_publish_filesystem_info(data, &about);
            stl_be_p(command + HOST_FIELD(result_length),
                     ASTRA_HOST_FILESYSTEM_INFO_SIZE);
        }
        if (fd >= 0)
            close(fd);
        break;
    }
    default:
        status = ASTRA_STATUS_UNSUPPORTED;
        break;
    }

done:
    astra_host_job_session(s, job, NULL);
    if (borrowed_root) close(root);
    if (descriptor_reserved) {
        qemu_mutex_lock(&s->host.files_lock);
        assert(s->host.fs_descriptors != 0u);
        --s->host.fs_descriptors;
        if (directory_reserved) {
            assert(s->host.fs_directories != 0u);
            --s->host.fs_directories;
        }
        qemu_mutex_unlock(&s->host.files_lock);
    }
    if (status == ASTRA_STATUS_BAD_HANDLE &&
        ((handle != 0u && astra_host_lost_file(s, owner, handle, false)) ||
         ((operation == ASTRA_HOST_FS_RENAME || operation == ASTRA_HOST_FS_LINK) &&
          astra_host_lost_file(s, owner, ldl_be_p(command + HOST_FIELD(value_lo)), false))))
        status = ASTRA_STATUS_LOST;
    if (file != NULL)
        astra_host_file_unlock(s, file);
    if (namespace_locked && astra_host_fs_mutates(operation, flags)) {
        qemu_mutex_lock(&s->host.files_lock);
        astra_host_usage_changed_locked(s);
        qemu_mutex_unlock(&s->host.files_lock);
    }
    if (namespace_locked)
        qemu_mutex_unlock(&s->host.namespace_lock);
    if (session != NULL)
        astra_host_session_release(s, session);
    if (status == ASTRA_STATUS_ESCAPED) {
        uint32_t length = strlen(resolved.continuation);
        uint32_t capacity = ldl_be_p(command + HOST_FIELD(data_capacity));
        uint8_t *data = astra_host_command_data(
            s, physical, bytes, command_bytes, command, capacity);

        stl_be_p(command + HOST_FIELD(result_value), resolved.consumed);
        stl_be_p(command + HOST_FIELD(value_lo), resolved.escape);
        stl_be_p(command + HOST_FIELD(value_hi), resolved.links);
        if (capacity < length)
            status = ASTRA_STATUS_BUFFER_TOO_SMALL;
        else if (data == NULL)
            status = ASTRA_STATUS_INVALID;
        else {
            memcpy(data, resolved.continuation, length);
            stl_be_p(command + HOST_FIELD(result_length), length);
        }
    }
    g_free(path);
    g_free(path2);
    stl_be_p(command + HOST_FIELD(status), status);
    qatomic_add(&s->host.operation_execution_ns[operation_index],
                qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - started_ns);
}

/* QMP runs under BQL: drain workers before cutting their provider state. */
static void astra_host_fs_reset(Astra68State *s)
{
    GPtrArray *files;
    AstraHostStolenSessions sessions;

    for (uint32_t slot = 0u; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot)
        while (s->host.channels[slot].jobs != 0u)
            aio_poll(qemu_get_aio_context(), true);
    qemu_mutex_lock(&s->host.files_lock);
    qatomic_set(&s->host.fs_lost_owner, qatomic_read(&s->host.fs_owner));
    qatomic_set(&s->host.fs_owner, 0u);
    astra_network_next(&s->host.fs_epoch);
    ++s->host.fs_resets;
    files = astra_host_steal_files(s, 0u, true);
    sessions = astra_host_steal_sessions(s, 0u, 0u, 0u);
    qemu_mutex_unlock(&s->host.files_lock);
    astra_host_free_files(s, files);
    astra_host_free_sessions(s, sessions);
    astra_host_release_shares(s, 0u, 0u);
}

static void astra_host_fs_reset_set(Object *object, Visitor *visitor,
                                    const char *name, void *opaque,
                                    Error **errp)
{
    bool reset;

    if (visit_type_bool(visitor, name, &reset, errp) && reset)
        astra_host_fs_reset(opaque);
}

static void astra_host_budget_get(Object *object, Visitor *visitor,
                                  const char *name, void *opaque, Error **errp)
{
    Astra68State *s = opaque;
    uint64_t value = 0u;

    if (strcmp(name, "astra-host-rss-kib") == 0) {
        FILE *file = fopen("/proc/self/statm", "r");
        unsigned long total, resident;
        if (file == NULL || fscanf(file, "%lu %lu", &total, &resident) != 2) {
            if (file != NULL) fclose(file);
            error_setg(errp, "cannot read host RSS");
            return;
        }
        fclose(file);
        value = (uint64_t)resident * sysconf(_SC_PAGESIZE) / 1024u;
    } else if (strcmp(name, "astra-host-fds") == 0) {
        DIR *directory = opendir("/proc/self/fd");
        struct dirent *entry;
        if (directory == NULL) {
            error_setg(errp, "cannot read host descriptor count");
            return;
        }
        while ((entry = readdir(directory)) != NULL)
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
                ++value;
        closedir(directory);
        --value; /* Exclude this enumeration's descriptor. */
    } else {
        qemu_mutex_lock(&s->host.files_lock);
        if (strcmp(name, "astra-host-fs-descriptors") == 0)
            value = s->host.fs_descriptors;
        else if (strcmp(name, "astra-host-fs-directories") == 0)
            value = s->host.fs_directories;
        else
            value = (uint64_t)g_hash_table_size(s->host.files) +
                g_hash_table_size(s->host.lost_handles) +
                g_hash_table_size(s->host.shares) + g_hash_table_size(s->host.sessions);
        qemu_mutex_unlock(&s->host.files_lock);
    }
    visit_type_uint64(visitor, name, &value, errp);
}

static void astra_host_reset(Astra68State *s)
{
    for (uint32_t slot = 0; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot) {
        astra_host_channel_drain(s, &s->host.channels[slot]);
    }
    for (uint32_t slot = 0; slot < ASTRA_AUDIO_MAILBOX_STREAMS; ++slot) {
        astra_audio_stream_stop(s, slot);
    }
    s->audio.completion_pending = false;
    astra_host_refresh_completion(s);
    if (s->host.files != NULL) {
        astra_host_release_files(s, 0);
    }
    /* Under files_lock like every other change of the claim (review N2),
     * though every channel is drained and HOST_EXECUTE runs on this vCPU. */
    qemu_mutex_lock(&s->host.files_lock);
    qatomic_set(&s->host.fs_owner, 0u);
    qatomic_set(&s->host.fs_lost_owner, 0u);
    g_hash_table_remove_all(s->host.lost_handles);
    astra_host_usage_changed_locked(s);
    qemu_mutex_unlock(&s->host.files_lock);
    if (s->host.sessions != NULL)
        astra_host_release_sessions(s, 0u);
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
    if (s->host.provider_control < 0 || s->host.owner == 0 ||
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
    qatomic_inc(&s->host.submissions);
    for (uint32_t index = 0; index < s->host.request_count; ++index) {
        astra_host_execute_command(s, s->host.owner, NULL,
                              base + index * ASTRA_HOST_COMMAND_SIZE,
                              s->host.request_buffer, s->host.request_bytes,
                              command_bytes, s->host.generation, NULL);
        ++s->host.completed;
    }
    qatomic_add(&s->host.commands, s->host.completed);
    qatomic_add(&s->host.execution_ns,
                qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - started);
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

/* The configuration's scope words, valid for `operation`: none for CLOSE,
 * DETACH names only the word, an OPEN is unbound (no words) or bound
 * (BOUND, a live session the claimed owner opened, and that owner as the
 * creator), and ATTACH is bound. The creator check is the provider's half
 * of the creator rule; ASTRA_RIGHT_HOST_SCOPE is the kernel's. */
static bool astra_host_config_scope(Astra68State *s, const uint8_t *config,
                                    uint16_t operation,
                                    AstraHostChannelScope *scope)
{
    uint32_t flags = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope_flags));

    scope->scope = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope));
    scope->owner = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope_owner));
    scope->flags = flags & ASTRA_HOST_SCOPE_READ_ONLY;
    scope->files = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope_files));
    scope->live = true;
    switch (operation) {
    case ASTRA_HOST_CHANNEL_CONFIG_CLOSE:
        return scope->scope == 0 && flags == 0 && scope->owner == 0 && scope->files == 0;
    case ASTRA_HOST_CHANNEL_CONFIG_DETACH:
        return flags == 0 && scope->owner == 0 && scope->files == 0;
    case ASTRA_HOST_CHANNEL_CONFIG_OPEN:
        if (flags == 0 || flags == ASTRA_HOST_CHANNEL_PROVIDER)
            return scope->scope == 0 && scope->owner == 0 && scope->files == 0;
        /* fall through */
    case ASTRA_HOST_CHANNEL_CONFIG_ATTACH:
        return (flags & ASTRA_HOST_SCOPE_BOUND) != 0 &&
               (flags & ~(ASTRA_HOST_SCOPE_BOUND |
                          ASTRA_HOST_SCOPE_READ_ONLY)) == 0 &&
               scope->owner != 0 &&
               scope->owner == qatomic_read(&s->host.fs_owner) &&
               astra_host_session_live(s, scope->owner, scope->scope);
    default:
        return false;
    }
}

/* ATTACH or DETACH one scope on an open bound channel. One scope per word:
 * a second ATTACH of a word already bound is refused. */
static uint32_t astra_host_channel_rebind(Astra68State *s,
                                          AstraHostChannel *channel,
                                          uint16_t operation,
                                          const AstraHostChannelScope *scope)
{
    uint32_t result = ASTRA_SYSCALL_INVALID_ARGUMENT;

    qemu_mutex_lock(&s->host.files_lock);
    for (uint32_t at = 0; at < channel->scope_count; ++at) {
        if (channel->scopes[at].scope != scope->scope)
            continue;
        if (operation == ASTRA_HOST_CHANNEL_CONFIG_DETACH) {
            channel->scopes[at].live = false;
            result = ASTRA_SYSCALL_OK;
        }
        goto out;
    }
    if (operation == ASTRA_HOST_CHANNEL_CONFIG_ATTACH &&
        channel->scope_count < ASTRA_HOST_CHANNEL_SCOPE_MAX) {
        channel->scopes[channel->scope_count++] = *scope;
        result = ASTRA_SYSCALL_OK;
    } else if (operation == ASTRA_HOST_CHANNEL_CONFIG_ATTACH) {
        for (uint32_t at = 0u; at < channel->scope_count; ++at) {
            if (channel->scopes[at].live)
                continue;
            channel->scopes[at] = *scope;
            result = ASTRA_SYSCALL_OK;
            break;
        }
    }
out:
    qemu_mutex_unlock(&s->host.files_lock);
    return result;
}

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
    AstraHostChannelScope scope;

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
    uint32_t process = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(process));
    if (operation == ASTRA_HOST_CHANNEL_CONFIG_OPEN ?
            process == 0u || process > ASTRA_PROCESS_ID_MAX : process != 0u)
        return;
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
    if (!astra_host_config_scope(s, config, operation, &scope)) {
        return;
    }
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
            error_report("Astra68 host channel close rejected: slot=%u "
                         "active=%u owner=%u/%u host=%u/%u channel=%u/%u",
                         slot, channel->active ? 1u : 0u, owner,
                         channel->owner, host_generation,
                         channel->host_generation, channel_generation,
                         channel->channel_generation);
            s->host.channel_result = channel->active ?
                ASTRA_SYSCALL_ACCESS_DENIED : ASTRA_SYSCALL_PEER_DEAD;
            return;
        }
        astra_host_channel_drain(s, channel);
        qemu_mutex_lock(&s->host.files_lock);
        channel->bound = false;
        channel->scope_count = 0;
        qemu_mutex_unlock(&s->host.files_lock);
        astra_host_refresh_completion(s);
        s->host.channel_result = ASTRA_SYSCALL_OK;
        return;
    }
    if (operation == ASTRA_HOST_CHANNEL_CONFIG_ATTACH ||
        operation == ASTRA_HOST_CHANNEL_CONFIG_DETACH) {
        if (physical_buffer != 0 || byte_size != 0 ||
            command_capacity != 0 || !channel->active || !channel->bound ||
            channel->owner != owner ||
            channel->host_generation != host_generation ||
            channel->channel_generation != channel_generation) {
            s->host.channel_result = channel->active ?
                ASTRA_SYSCALL_ACCESS_DENIED : ASTRA_SYSCALL_PEER_DEAD;
            return;
        }
        s->host.channel_result =
            astra_host_channel_rebind(s, channel, operation, &scope);
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
    if (byte_size < data_offset) {
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
    qemu_mutex_lock(&s->host.files_lock);
    channel->owner = owner;
    channel->process = ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(process));
    channel->bound = scope.owner != 0;
    channel->provider = !channel->bound &&
        ldl_be_p(config + HOST_CHANNEL_CONFIG_FIELD(scope_flags)) ==
            ASTRA_HOST_CHANNEL_PROVIDER;
    channel->scope_count = channel->bound ? 1 : 0;
    channel->scopes[0] = scope;
    qemu_mutex_unlock(&s->host.files_lock);
    channel->host_generation = host_generation;
    channel->channel_generation = channel_generation;
    channel->physical_buffer = physical_buffer;
    channel->byte_size = byte_size;
    channel->command_capacity = command_capacity;
    channel->consumer_position = 0;
    channel->submitted_position = 0;
    channel->status = ASTRA_SYSCALL_OK;
    channel->completed = g_new0(uint8_t, command_capacity);
    channel->remote_desktop_fd = -1;
    channel->audio_fd = -1;
    qemu_mutex_lock(&s->host.files_lock);
    channel->active = true;
    qemu_mutex_unlock(&s->host.files_lock);
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
    /* A revoked (closed) channel runs nothing still queued: the close
     * drains jobs, and a job that starts after it began is dropped. */
    if (!qatomic_read(&s->host.channels[job->slot].active) ||
        qatomic_read(&s->host.channels[job->slot].channel_generation) !=
            job->channel_generation) {
        return -ECANCELED;
    }
    astra_host_execute_command(
        s, job->owner, &s->host.channels[job->slot],
        base + ASTRA_HOST_CHANNEL_HEADER_SIZE +
            (job->position & (job->command_capacity - 1u)) *
                ASTRA_HOST_COMMAND_SIZE,
        job->physical_buffer, job->byte_size, command_bytes,
        job->host_generation, job);
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
        qatomic_inc(&s->host.commands);
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
    uint64_t elapsed;

    qemu_mutex_lock(&s->host.files_lock);
    AstraHostJob **held = &s->host.jobs;
    while (*held != NULL && *held != job) held = &(*held)->next;
    assert(*held == job && job->session == NULL);
    *held = job->next;
    qemu_mutex_unlock(&s->host.files_lock);
    assert(channel->jobs != 0);
    --channel->jobs;
    assert(qatomic_read(&s->host.inflight) != 0);
    qatomic_dec(&s->host.inflight);
    if (!channel->active ||
        channel->owner != job->owner ||
        channel->host_generation != job->host_generation ||
        channel->channel_generation != job->channel_generation) {
        g_free(job);
        return;
    }
    base = astra_dma_data(s, channel->physical_buffer, channel->byte_size, 0,
                          channel->byte_size);
    elapsed = qemu_clock_get_ns(QEMU_CLOCK_REALTIME) - job->started_ns;
    qatomic_add(&s->host.execution_ns, elapsed);
    if (elapsed > qatomic_read(&s->host.execution_max_ns))
        qatomic_set(&s->host.execution_max_ns, elapsed);
    if (elapsed > UINT64_C(20000000))
        qatomic_inc(&s->host.slow_commands);
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
    /* A data range and a path-operand range per command at most. */
    ranges = g_new(AstraHostDataRange, 2u * (size_t)outstanding);

    for (uint32_t position = channel->consumer_position;
         position != producer; ++position) {
        const uint8_t *command = base + ASTRA_HOST_CHANNEL_HEADER_SIZE +
            (position & (channel->command_capacity - 1u)) *
                ASTRA_HOST_COMMAND_SIZE;
        uint16_t service = lduw_be_p(command + HOST_FIELD(service));
        uint16_t operation = lduw_be_p(command + HOST_FIELD(operation));
        uint32_t amount;
        uint32_t offset;
        bool writable;

        /* Path operands are read, never written, by the provider. */
        if (service == ASTRA_HOST_SERVICE_FILESYSTEM) {
            uint32_t operands = lduw_be_p(command + HOST_FIELD(path_length)) +
                                lduw_be_p(command + HOST_FIELD(path2_length));
            uint32_t at = ldl_be_p(command + HOST_FIELD(path_offset));

            if (operands != 0u && at >= command_bytes &&
                at <= channel->byte_size &&
                operands <= channel->byte_size - at)
                ranges[range_count++] = (AstraHostDataRange){
                    at, at + operands, false
                };
        }
        if (service == ASTRA_HOST_SERVICE_FILESYSTEM &&
            operation == ASTRA_HOST_FS_WRITE) {
            amount = ldl_be_p(command + HOST_FIELD(data_length));
            writable = false;
        } else if ((service == ASTRA_HOST_SERVICE_FILESYSTEM &&
                    (operation == ASTRA_HOST_FS_READ ||
                   operation == ASTRA_HOST_FS_READDIR ||
                    operation == ASTRA_HOST_FS_READLINK)) ||
                   (service == ASTRA_HOST_SERVICE_METRICS &&
                    operation == ASTRA_HOST_METRICS_SNAPSHOT) ||
                   (service == ASTRA_HOST_SERVICE_ENTROPY &&
                    operation == ASTRA_HOST_ENTROPY_FILL)) {
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
        error_report("Astra68 host channel kick rejected: slot=%u inactive",
                     slot);
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
        error_report("Astra68 host channel kick rejected: slot=%u "
                     "producer=%u submitted=%u consumer=%u",
                     slot, producer, channel->submitted_position,
                     channel->consumer_position);
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
    qemu_mutex_lock(&s->host.files_lock);
    bool exhausted = s->host.next_job > UINT64_MAX - pending;
    qemu_mutex_unlock(&s->host.files_lock);
    if (exhausted) {
        channel->status = ASTRA_SYSCALL_RESOURCE_LIMIT;
        stl_be_p(base + HOST_CHANNEL_HEADER_FIELD(transport_status), channel->status);
        return;
    }
    qatomic_inc(&s->host.submissions);
    channel->submitted_position = producer;
    for (uint32_t position = producer - pending;
         position != producer; ++position) {
        AstraHostJob *job = g_new0(AstraHostJob, 1);

        job->machine = s;
        job->slot = slot;
        job->owner = channel->owner;
        job->process = channel->process;
        job->host_generation = channel->host_generation;
        job->channel_generation = channel->channel_generation;
        job->physical_buffer = channel->physical_buffer;
        job->byte_size = channel->byte_size;
        job->command_capacity = channel->command_capacity;
        job->position = position;
        job->started_ns = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
        qemu_mutex_lock(&s->host.files_lock);
        job->id = ++s->host.next_job;
        job->next = s->host.jobs;
        s->host.jobs = job;
        qemu_mutex_unlock(&s->host.files_lock);
        ++channel->jobs;
        qatomic_inc(&s->host.inflight);
        if (qatomic_read(&s->host.inflight) >
            qatomic_read(&s->host.max_inflight)) {
            qatomic_set(&s->host.max_inflight,
                        qatomic_read(&s->host.inflight));
        }
        thread_pool_submit_aio(astra_host_channel_worker, job,
                               astra_host_channel_complete, job);
    }
}

#undef HOST_CHANNEL_HEADER_FIELD
#undef HOST_CHANNEL_CONFIG_FIELD
#undef HOST_SUBMISSION_FIELD

#undef NODE_FIELD
#undef HOST_FIELD

/*
 * Application audio streams (astra/audio_stream.h, audio_mailbox.h). The
 * kernel opens and closes them through STREAM_CONFIG; STREAM_KICK says the
 * application queued buffers; STREAM_ARM asks for one interrupt once a
 * buffer is free. All of it runs with the BQL held: MMIO from the vCPU, and
 * the daemon's room wakes through the main loop.
 */
#define AUDIO_HEADER_FIELD(field) offsetof(AstraAudioStreamHeader, field)
#define AUDIO_CONFIG_FIELD(field) offsetof(AstraAudioStreamConfig, field)

static void astra_audio_stream_stop(Astra68State *s, uint32_t slot)
{
    AstraAudioStream *stream = &s->audio.streams[slot];

    if (!stream->active) {
        return;
    }
    /* The daemon stops reading before anything else changes. */
    qatomic_store_release(&s->audio.mailbox->streams[slot].generation, 0u);
    memset(stream, 0, sizeof(*stream));
}

static void astra_audio_stream_interrupt(Astra68State *s,
                                         AstraAudioStream *stream)
{
    stream->armed = false;
    s->audio.completion_pending = true;
    s->host.completion_pending = true;
    astra_update_irq(s);
}

/* Copy queued buffers that fit, return buffers the mixer consumed, and raise
   the armed interrupt when that freed one. @p kick: called from the guest's
   STREAM_KICK, so the header's `queued` may be read. */
static void astra_audio_stream_refill(Astra68State *s, uint32_t slot,
                                      bool kick)
{
    AstraAudioStream *stream;
    AstraAudioMailboxStream *ring;
    uint8_t *base;
    uint8_t *data;
    uint32_t outstanding;
    uint32_t used;
    uint32_t written;
    uint32_t copied = 0;
    uint32_t in_mailbox;
    uint32_t completed;

    if (slot >= ASTRA_AUDIO_MAILBOX_STREAMS || !s->audio.streams[slot].active) {
        return;
    }
    stream = &s->audio.streams[slot];
    ring = &s->audio.mailbox->streams[slot];
    data = s->audio.rings + slot * ASTRA_AUDIO_MAILBOX_RING_BYTES;
    base = astra_dma_data(s, stream->physical_buffer, stream->byte_size, 0,
                          stream->byte_size);
    if (base == NULL) {
        return;
    }
    /* The daemon cannot play it: say so in the header, wake the waiter. */
    if (qatomic_load_acquire(&ring->status) != ASTRA_SYSCALL_OK) {
        stl_be_p(base + AUDIO_HEADER_FIELD(status),
                 qatomic_read(&ring->status));
        astra_audio_stream_interrupt(s, stream);
        return;
    }
    if (kick) {
        stream->queued = ldl_be_p(base + AUDIO_HEADER_FIELD(queued));
    }
    outstanding = stream->queued - stream->consumed;
    in_mailbox = stream->copied - stream->consumed;
    if (outstanding > stream->buffer_count || in_mailbox > outstanding) {
        stl_be_p(base + AUDIO_HEADER_FIELD(status),
                 ASTRA_SYSCALL_INVALID_ARGUMENT);
        astra_audio_stream_interrupt(s, stream);
        return;
    }
    written = ring->written;
    used = written - qatomic_load_acquire(&ring->read);
    if (used > in_mailbox * stream->audio_bytes) {
        return; /* a stale read from the previous generation: wait */
    }
    completed = (in_mailbox * stream->audio_bytes - used) /
                stream->audio_bytes;
    stream->consumed += completed;
    outstanding = stream->queued - stream->copied;
    while (outstanding != 0 &&
           used + stream->audio_bytes <=
               ASTRA_AUDIO_MAILBOX_DEPTH * stream->audio_bytes) {
        const uint8_t *source = base + ASTRA_AUDIO_STREAM_HEADER_SIZE +
            (stream->copied % stream->buffer_count) * stream->stride;
        uint32_t at = written % ASTRA_AUDIO_MAILBOX_RING_BYTES;
        uint32_t first = MIN(stream->audio_bytes,
                             ASTRA_AUDIO_MAILBOX_RING_BYTES - at);

        memcpy(data + at, source, first);
        memcpy(data, source + first, stream->audio_bytes - first);
        written += stream->audio_bytes;
        used += stream->audio_bytes;
        ++stream->copied;
        --outstanding;
        ++copied;
    }
    if (copied == 0 && completed == 0) {
        return;
    }
    if (copied != 0) {
        qatomic_store_release(&ring->written, written);
    }
    /* Playback bounds the guest queue; copying ahead does not free a slot. */
    smp_mb();
    stl_be_p(base + AUDIO_HEADER_FIELD(consumed), stream->consumed);
    if (completed != 0 && stream->armed) {
        astra_audio_stream_interrupt(s, stream);
    }
}

static void astra_audio_stream_arm(Astra68State *s, uint32_t slot)
{
    AstraAudioStream *stream;
    uint8_t *base;

    if (slot >= ASTRA_AUDIO_MAILBOX_STREAMS || !s->audio.streams[slot].active) {
        return;
    }
    stream = &s->audio.streams[slot];
    stream->armed = true;
    base = astra_dma_data(s, stream->physical_buffer, stream->byte_size, 0,
                          stream->byte_size);
    /* Already free, or failed: the interrupt is due now. */
    if (base == NULL ||
        ldl_be_p(base + AUDIO_HEADER_FIELD(status)) != ASTRA_SYSCALL_OK ||
        stream->queued - stream->consumed < stream->buffer_count) {
        astra_audio_stream_interrupt(s, stream);
    }
}

static void astra_audio_stream_configure(Astra68State *s, uint32_t physical)
{
    AstraAudioStream *stream;
    AstraAudioMailboxStream *ring;
    uint8_t *config;
    uint8_t *header;
    uint32_t slot, owner, host_generation, generation, physical_buffer;
    uint32_t byte_size, format, period_frames, buffer_count;
    uint32_t frame_bytes, stride;
    uint16_t operation;

    s->audio.result = ASTRA_SYSCALL_INVALID_ARGUMENT;
    if (!s->audio.enabled) {
        s->audio.result = ASTRA_SYSCALL_UNSUPPORTED;
        return;
    }
    if ((physical & 63u) != 0) {
        return;
    }
    config = astra_dma_data(s, physical, ASTRA_AUDIO_STREAM_CONFIG_SIZE, 0,
                            ASTRA_AUDIO_STREAM_CONFIG_SIZE);
    if (config == NULL ||
        ldl_be_p(config + AUDIO_CONFIG_FIELD(size)) !=
            ASTRA_AUDIO_STREAM_CONFIG_SIZE ||
        lduw_be_p(config + AUDIO_CONFIG_FIELD(version)) !=
            ASTRA_AUDIO_STREAM_CONFIG_VERSION) {
        return;
    }
    for (size_t index = 0;
         index < sizeof(((AstraAudioStreamConfig *)0)->reserved); ++index) {
        if (config[AUDIO_CONFIG_FIELD(reserved) + index] != 0) {
            return;
        }
    }
    operation = lduw_be_p(config + AUDIO_CONFIG_FIELD(operation));
    slot = ldl_be_p(config + AUDIO_CONFIG_FIELD(slot));
    owner = ldl_be_p(config + AUDIO_CONFIG_FIELD(owner));
    host_generation = ldl_be_p(config + AUDIO_CONFIG_FIELD(host_generation));
    generation = ldl_be_p(config + AUDIO_CONFIG_FIELD(stream_generation));
    physical_buffer = ldl_be_p(config + AUDIO_CONFIG_FIELD(physical_buffer));
    byte_size = ldl_be_p(config + AUDIO_CONFIG_FIELD(byte_size));
    format = ldl_be_p(config + AUDIO_CONFIG_FIELD(format));
    period_frames = ldl_be_p(config + AUDIO_CONFIG_FIELD(period_frames));
    buffer_count = ldl_be_p(config + AUDIO_CONFIG_FIELD(buffer_count));
    if (slot >= ASTRA_AUDIO_MAILBOX_STREAMS || owner == 0 || generation == 0) {
        return;
    }
    stream = &s->audio.streams[slot];
    if (operation == ASTRA_AUDIO_STREAM_CONFIG_CLOSE) {
        if (!stream->active || stream->owner != owner ||
            stream->generation != generation) {
            s->audio.result = stream->active ? ASTRA_SYSCALL_ACCESS_DENIED :
                                               ASTRA_SYSCALL_PEER_DEAD;
            return;
        }
        astra_audio_stream_stop(s, slot);
        s->audio.result = ASTRA_SYSCALL_OK;
        return;
    }
    frame_bytes = astra_pcm_format_frame_bytes(format);
    stride = astra_audio_stream_buffer_bytes(frame_bytes, period_frames);
    if (operation != ASTRA_AUDIO_STREAM_CONFIG_OPEN || stream->active ||
        host_generation != s->host.generation || stride == 0 ||
        period_frames < ASTRA_AUDIO_STREAM_PERIOD_MIN ||
        period_frames > ASTRA_AUDIO_STREAM_PERIOD_MAX ||
        buffer_count < ASTRA_AUDIO_STREAM_BUFFERS_MIN ||
        buffer_count > ASTRA_AUDIO_STREAM_BUFFERS_MAX ||
        (physical_buffer & (ASTRA_ABI_ALIGNMENT - 1u)) != 0 ||
        byte_size != ASTRA_AUDIO_STREAM_HEADER_SIZE + buffer_count * stride) {
        return;
    }
    header = astra_dma_data(s, physical_buffer, byte_size, 0, byte_size);
    if (header == NULL) {
        return;
    }
    memset(header, 0, ASTRA_AUDIO_STREAM_HEADER_SIZE);
    stl_be_p(header + AUDIO_HEADER_FIELD(magic), ASTRA_AUDIO_STREAM_MAGIC);
    stw_be_p(header + AUDIO_HEADER_FIELD(version), ASTRA_AUDIO_STREAM_VERSION);
    stw_be_p(header + AUDIO_HEADER_FIELD(header_size),
             ASTRA_AUDIO_STREAM_HEADER_SIZE);
    stl_be_p(header + AUDIO_HEADER_FIELD(format), format);
    stl_be_p(header + AUDIO_HEADER_FIELD(period_frames), period_frames);
    stl_be_p(header + AUDIO_HEADER_FIELD(buffer_count), buffer_count);
    stl_be_p(header + AUDIO_HEADER_FIELD(buffer_bytes), stride);
    stl_be_p(header + AUDIO_HEADER_FIELD(buffer_offset),
             ASTRA_AUDIO_STREAM_HEADER_SIZE);
    stl_be_p(header + AUDIO_HEADER_FIELD(total_size), byte_size);
    stl_be_p(header + AUDIO_HEADER_FIELD(stream_generation), generation);
    stl_be_p(header + AUDIO_HEADER_FIELD(status), ASTRA_SYSCALL_OK);
    *stream = (AstraAudioStream){
        .owner = owner, .generation = generation,
        .physical_buffer = physical_buffer, .byte_size = byte_size,
        .buffer_count = buffer_count, .stride = stride,
        .audio_bytes = frame_bytes * period_frames, .active = true,
    };
    /* An empty ring, then the generation that makes it the daemon's. */
    ring = &s->audio.mailbox->streams[slot];
    ring->format = format;
    ring->period_frames = period_frames;
    ring->buffer_bytes = stream->audio_bytes;
    ring->start = qatomic_load_acquire(&ring->read);
    qatomic_set(&ring->written, ring->start);
    qatomic_set(&ring->status, 0u);
    qatomic_store_release(&ring->generation, generation);
    s->audio.result = ASTRA_SYSCALL_OK;
}

#ifdef CONFIG_LINUX
static void *astra_audio_room_thread(void *opaque)
{
    Astra68State *s = opaque;
    uint32_t seen = qatomic_read(&s->audio.mailbox->room_sequence);

    for (;;) {
        uint32_t now = qatomic_read(&s->audio.mailbox->room_sequence);

        if (now == seen) {
            qemu_futex_wait((void *)&s->audio.mailbox->room_sequence, now);
            continue;
        }
        seen = now;
        event_notifier_set(&s->audio.room_notifier);
    }
    return NULL;
}

static void astra_audio_room_ready(EventNotifier *notifier)
{
    Astra68State *s = container_of(notifier, Astra68State,
                                   audio.room_notifier);

    if (!event_notifier_test_and_clear(notifier)) {
        return;
    }
    for (uint32_t slot = 0; slot < ASTRA_AUDIO_MAILBOX_STREAMS; ++slot) {
        astra_audio_stream_refill(s, slot, false);
    }
}

/* ASTRA_AUDIO_MAILBOX_PATH: the file astra-audio-host maps too. Without it
   the host does not offer audio streams. */
static void astra_audio_mailbox_init(Astra68State *s)
{
    const char *path = g_getenv("ASTRA_AUDIO_MAILBOX_PATH");
    AstraAudioMailbox *mailbox;
    void *mapped;
    int fd;

    s->audio.lock_fd = -1;
    if (path == NULL || path[0] == '\0') {
        return;
    }
    fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0660);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0 ||
        ftruncate(fd, ASTRA_AUDIO_MAILBOX_BYTES) != 0) {
        error_report("cannot own audio mailbox '%s': %s", path,
                     strerror(errno));
        exit(EXIT_FAILURE);
    }
    mapped = mmap(NULL, ASTRA_AUDIO_MAILBOX_BYTES, PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        error_report("cannot map audio mailbox '%s': %s", path,
                     strerror(errno));
        exit(EXIT_FAILURE);
    }
    s->audio.lock_fd = fd;
    mailbox = mapped;
    /* Every slot starts closed; the counters continue, so a daemon that
       outlived an earlier QEMU never sees its room word go backwards. */
    for (uint32_t slot = 0; slot < ASTRA_AUDIO_MAILBOX_STREAMS; ++slot) {
        qatomic_store_release(&mailbox->streams[slot].generation, 0u);
    }
    mailbox->stream_count = ASTRA_AUDIO_MAILBOX_STREAMS;
    mailbox->ring_bytes = ASTRA_AUDIO_MAILBOX_RING_BYTES;
    mailbox->ring_offset = ASTRA_AUDIO_MAILBOX_RING_OFFSET;
    mailbox->version = ASTRA_AUDIO_MAILBOX_VERSION;
    qatomic_store_release(&mailbox->magic, ASTRA_AUDIO_MAILBOX_MAGIC);
    s->audio.mailbox = mailbox;
    s->audio.rings = (uint8_t *)mapped + ASTRA_AUDIO_MAILBOX_RING_OFFSET;
    if (event_notifier_init(&s->audio.room_notifier, 0) < 0) {
        error_report("cannot create the audio room notifier");
        exit(EXIT_FAILURE);
    }
    event_notifier_set_handler(&s->audio.room_notifier,
                               astra_audio_room_ready);
    qemu_thread_create(&s->audio.room_thread, "astra-audio-room",
                       astra_audio_room_thread, s, QEMU_THREAD_DETACHED);
    s->audio.enabled = true;
}
#else
static void astra_audio_mailbox_init(Astra68State *s)
{
    s->audio.lock_fd = -1;
    if (g_getenv("ASTRA_AUDIO_MAILBOX_PATH") != NULL) {
        error_report("ASTRA_AUDIO_MAILBOX_PATH requires a Linux host");
        exit(EXIT_FAILURE);
    }
}
#endif

#undef AUDIO_CONFIG_FIELD
#undef AUDIO_HEADER_FIELD

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
    if (astra_astraea_irq_status(s) & s->astraea.irq_enable) {
        pending |= 1u << IRQ_SOURCE_ASTRAEA;
    }
    if (s->ohci.present &&
        (astra_ohci_astra_status(s) &
         (OHCI_ASTRA_IRQ | OHCI_ASTRA_DMA_FAULT)) != 0) {
        pending |= 1u << IRQ_SOURCE_USB;
    }
    if (astra_input_irq_due(&s->input)) {
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

    int64_t late = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) - timer->due_ns;

    if (late > 0 && (uint64_t)late > s->timer_late_max_ns)
        s->timer_late_max_ns = late;
    if (late > 1000000)
        ++s->timer_late_count;
    timer->status |= TIMER_EXPIRED;
    if (s->trace_timers) {
        fprintf(stderr, "ASTRA68 timer expired load=%" PRIu32
                " control=%08" PRIx32 " now=%" PRIu64 "\n",
                timer->load, timer->control,
                qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL));
    }
    if (timer->control & TIMER_PERIODIC) {
        timer->due_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                        astra_timer_period_ns(timer);
        timer_mod_ns(timer->qemu_timer, timer->due_ns);
    } else {
        timer->control &= ~TIMER_ENABLE;
    }
    astra_update_irq(s);
}

static void astra_input_release_motion(void *opaque)
{
    Astra68State *s = opaque;

    if (astra_input_level(&s->input) != 0) {
        s->input.motion_released = true;
        astra_update_irq(s);
    }
}

/*
 * The time to the next vblank and the frame period. With a display helper
 * that has measured the panel (AstraDisplayScanout), that is the panel's
 * own vblank; otherwise a 60 Hz frame of the emulator's clock.
 */
static void astra_next_vblank(Astra68State *s, int64_t *delay_ns,
                              int64_t *period_ns)
{
    const AstraDisplayScanout *scanout;
    uint64_t vblank;
    uint64_t period;
    uint32_t sequence;
    uint64_t now;

    *delay_ns = NANOSECONDS_PER_SECOND / 60;
    *period_ns = *delay_ns;
    if (s->display.mailbox == NULL)
        return;
    scanout = (const AstraDisplayScanout *)
        ((const uint8_t *)s->display.mailbox + ASTRA_DISPLAY_SCANOUT_OFFSET);
    sequence = qatomic_read(&scanout->sequence);
    smp_rmb();
    vblank = qatomic_read(&scanout->vblank_ns);
    period = qatomic_read(&scanout->period_ns);
    smp_rmb();
    if (sequence == 0u || (sequence & 1u) != 0u ||
        sequence != qatomic_read(&scanout->sequence) ||
        period < NANOSECONDS_PER_SECOND / 240 ||
        period > NANOSECONDS_PER_SECOND / 24)
        return;
    /* QEMU_CLOCK_REALTIME is the helper's CLOCK_MONOTONIC. Aim a little
       past the boundary so a late timer never fires a frame twice. */
    now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
    if (vblank <= now)
        vblank += ((now - vblank) / period + 1u) * period;
    if (vblank - now < period / 4u)
        vblank += period;
    *delay_ns = (int64_t)(vblank - now);
    *period_ns = (int64_t)period;
}

static void astra_vblank(void *opaque)
{
    Astra68State *s = opaque;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);
    int64_t delay;
    int64_t period;

    s->vega.frame_counter++;
    s->vega.irq_status |= VEGA_IRQ_VBLANK;
    astra_next_vblank(s, &delay, &period);
    timer_mod_ns(s->input.motion_timer, now + period / 2);
    timer_mod_ns(s->vega.vblank_timer, now + delay);
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
    case 0x01c: return 0x00068040;
    case 0x020: return 0x51454d55; /* QEMU */
    case 0x024: return 0x0000001d;
    case 0x028: return ASTRA_CPU_HZ;
    case 0x02c: return ASTRA_SDRAM_BASE;
    case 0x030: return s->ram_size;
    case 0x034: return ASTRA_ROM_ADDRESS;
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
        s->cycles_latch = astra_now_cycles(s);
        return (uint32_t)s->cycles_latch;
    case 0x0f0:
        return (uint32_t)(s->cycles_latch >> 32);
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
    case 0x1d8: return ASTRA_DISPLAY_HOST_VERSION_1_1;
    case 0x1dc:
        return ASTRA_DISPLAY_HOST_CAP_SOLID_FRAME |
               ASTRA_DISPLAY_HOST_CAP_FENCED_PRESENT |
               ASTRA_DISPLAY_HOST_CAP_RENDER_BATCH |
               ASTRA_DISPLAY_HOST_CAP_HARDWARE_CURSOR |
               ASTRA_DISPLAY_HOST_CAP_READ_SURFACE |
               ASTRA_DISPLAY_HOST_CAP_ATTACHMENT;
    case 0x1e0: return astra_display_queue(&s->display);
    case 0x1e4: return s->display.request_id;
    case 0x1e8: return s->display.request_op;
    case 0x1ec: return s->display.request_source;
    case 0x1f4:
        return astra_display_completion(&s->display) != NULL ?
               astra_display_completion(&s->display)->id : 0u;
    case 0x1f8:
        return astra_display_completion(&s->display) != NULL ?
               astra_display_completion(&s->display)->status : 0u;
    case 0x1fc:
        return astra_display_completion(&s->display) != NULL ?
               astra_display_completion(&s->display)->generation : 0u;
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
    case 0x728: return s->display.request_attachment;
    case 0x72c: return ASTRA_COPY_ENGINE_ID;
    case 0x734: return s->copy_status;
    case 0x738: return s->display.cursor;
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
        return s->host.provider_control >= 0 ? HOST_ACCEL_ID_MAGIC : 0u;
    case 0x884: return ASTRA_HOST_VERSION;
    case 0x888:
        return s->host.provider_control >= 0 ?
            ASTRA_HOST_CAP_FILESYSTEM | ASTRA_HOST_CAP_OWNER_SCOPED |
                ASTRA_HOST_CAP_SUBMISSION_DESCRIPTOR |
                ASTRA_HOST_CAP_CHANNEL |
                ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ |
                ASTRA_HOST_CAP_METRICS |
                ASTRA_HOST_CAP_REMOTE_DESKTOP |
                ASTRA_HOST_CAP_ENTROPY |
                ASTRA_HOST_CAP_AUDIO |
                ASTRA_HOST_CAP_FS_SCOPE |
                ASTRA_HOST_CAP_FS_SESSIONS |
#ifdef CONFIG_LINUX
                ASTRA_HOST_CAP_FS_RESOLVES |
                ASTRA_HOST_CAP_FS_DESCRIPTIONS |
                ASTRA_HOST_CAP_FS_DIRECTORY_BATCH | ASTRA_HOST_CAP_FS_PATHS |
                ASTRA_HOST_CAP_VOLUME_CATALOG |
#endif
                (s->audio.enabled ? ASTRA_HOST_CAP_AUDIO_STREAM : 0u) : 0u;
    case 0x88c:
        return s->host.provider_control >= 0 ? ASTRA_HOST_STATE_READY : 0u;
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
    case 0x8e4: return s->audio.result;
    case 0x8d0: return s->host.completion_pending ? 1u : 0u;
    case 0x8d8: return qatomic_read(&s->host.inflight);
    case 0x8dc: return qatomic_read(&s->host.max_inflight);
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

static void astra_report_status(Astra68State *s, uint32_t value)
{
    const char *result;

    if (value == ASTRA_KERNEL_STATUS_PANIC) {
        result = "PANIC";
        astra_display_panic_text(s);
    } else if (value == ASTRA_KERNEL_STATUS_K1_SOAK) {
        result = "SOAK";
    } else if (value == ASTRA_KERNEL_STATUS_SHUTDOWN) {
        result = "SHUTDOWN";
    } else if (value == ASTRA_KERNEL_STATUS_RESTART) {
        result = "RESTART";
    } else {
        result = "READY";
    }
    fprintf(stderr, "\nASTRA68-QEMU %s cycles=%" PRIu64
            " pc=%08x scratch=%08x\n", result, astra_now_cycles(s),
            s->cpu->env.pc, value);
    if (value == ASTRA_KERNEL_STATUS_SHUTDOWN) {
        qemu_system_shutdown_request_with_code(
            SHUTDOWN_CAUSE_GUEST_SHUTDOWN, ASTRA_GUEST_SHUTDOWN_EXIT_CODE);
    } else if (value == ASTRA_KERNEL_STATUS_RESTART) {
        qemu_system_shutdown_request_with_code(
            SHUTDOWN_CAUSE_GUEST_SHUTDOWN, ASTRA_GUEST_RESTART_EXIT_CODE);
    } else if (value != ASTRA_KERNEL_STATUS_K1_SOAK) {
        qemu_system_shutdown_request(value == ASTRA_KERNEL_STATUS_PANIC ?
                                     SHUTDOWN_CAUSE_GUEST_PANIC :
                                     SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
    }
}

static void astra_vesta_write32(Astra68State *s, hwaddr offset,
                                uint32_t value)
{
    int timer_index;
    AstraTimer *timer;

    switch (offset) {
    case 0x018:
        s->scratch = value;
        if (value == ASTRA_KERNEL_STATUS_K1_READY ||
            value == ASTRA_KERNEL_STATUS_K1_SOAK ||
            value == ASTRA_KERNEL_STATUS_PANIC ||
            value == ASTRA_KERNEL_STATUS_SHUTDOWN ||
            value == ASTRA_KERNEL_STATUS_RESTART) {
            astra_report_status(s, value);
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
            if ((value & ASTRA_DISPLAY_HOST_POP) &&
                s->display.completion_count != 0u) {
                s->display.completion_head =
                    (s->display.completion_head + 1u) %
                    ASTRA_DISPLAY_HOST_QUEUE_DEPTH;
                --s->display.completion_count;
                s->display.collect_cycle = astra_now_cycles(s);
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
            /* Drained: motion queued from now on waits for the next frame. */
            if (astra_input_level(&s->input) == 0)
                s->input.motion_released = false;
        }
        if ((value & ASTRA_INPUT_ACK_OVERFLOW) != 0) {
            s->input.overflow = false;
        }
        astra_update_irq(s);
        break;
    case 0x728: s->display.request_attachment = value; break;
    case 0x730: s->copy_status = astra_copy_run(s, value); break;
    case 0x738: astra_display_post_cursor(s, value); break;
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
    case 0x8e0: astra_audio_stream_configure(s, value); break;
    case 0x8e8: astra_audio_stream_refill(s, value, true); break;
    case 0x8ec: astra_audio_stream_arm(s, value); break;
    case 0x8d4:
        if ((value & 1u) != 0u) {
            for (uint32_t slot = 0; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot) {
                s->host.channels[slot].completion_pending = false;
            }
            s->audio.completion_pending = false;
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
                    timer->due_ns = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                                    astra_timer_period_ns(timer);
                    timer_mod_ns(timer->qemu_timer, timer->due_ns);
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
    case 0x014: return astra_astraea_irq_status(s);
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
    s->input.motion_released = false;
    s->input.dropped = 0;
    ++s->input.host_generation;

    astra_block_reset(s);
    astra_display_reset(s);
    astra_network_reset(s);
    astra_host_reset(s);
    astra_update_irq(s);
}

static uint64_t astra_host_budget(const char *name, uint64_t fallback)
{
    const char *text = g_getenv(name);
    char *end;
    uint64_t value;

    if (text == NULL)
        return fallback;
    if (*text == '\0')
        goto invalid;
    for (const char *at = text; *at != '\0'; ++at)
        if (*at < '0' || *at > '9')
            goto invalid;
    errno = 0;
    value = g_ascii_strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value == 0u || value > UINT32_MAX)
        goto invalid;
    return value;
invalid:
    error_report("%s must be a positive decimal at most %u", name, UINT32_MAX);
    exit(EXIT_FAILURE);
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
    const char *display_payload_path;
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
    s->host.provider_control = -1;
    s->host.provider_path = -1;
    qemu_mutex_init(&s->host.provider_control_lock);
    qemu_mutex_init(&s->host.provider_path_lock);
    qemu_mutex_init(&s->host.files_lock);
    qemu_mutex_init(&s->host.namespace_lock);
    for (uint32_t slot = 0; slot < ASTRA_HOST_CHANNEL_COUNT; ++slot) {
        qemu_mutex_init(&s->host.channel_socket_locks[slot]);
        s->host.channels[slot].remote_desktop_fd = -1;
        s->host.channels[slot].audio_fd = -1;
    }
    s->host.files = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->host.usage_sequence = 1u;
    s->host.lost_handles = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, g_free);
    s->host.shares = g_hash_table_new(g_int64_hash, g_int64_equal);
    s->host.sessions = g_hash_table_new(g_direct_hash, g_direct_equal);
    s->host.fs_epoch = 1u;
    struct rlimit descriptor_limit;
    if (getrlimit(RLIMIT_NOFILE, &descriptor_limit) != 0 || descriptor_limit.rlim_cur <= 256u) {
        error_report("AstraHost requires RLIMIT_NOFILE above its 256-descriptor reserve");
        exit(EXIT_FAILURE);
    }
    uint64_t available_descriptors = MIN((uint64_t)descriptor_limit.rlim_cur - 256u,
                                         UINT32_MAX);
    s->host.fs_max_descriptors = MIN(available_descriptors,
        astra_host_budget("ASTRA_HOSTFS_MAX_DESCRIPTORS", available_descriptors));
    s->host.fs_max_directories = MIN(s->host.fs_max_descriptors,
        astra_host_budget("ASTRA_HOSTFS_MAX_DIRECTORIES", s->host.fs_max_descriptors));
    astra_host_provider_init(s);
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
                                   "astra-host-scoped-commands",
                                   &s->host.scoped_commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add(OBJECT(machine), "astra-host-fs-reset", "bool",
                        NULL, astra_host_fs_reset_set, NULL, s);
    {
        static const char *const gauges[] = {
            "astra-host-fs-descriptors", "astra-host-fs-directories",
            "astra-host-fs-objects", "astra-host-rss-kib", "astra-host-fds"
        };
        for (size_t gauge = 0u; gauge < G_N_ELEMENTS(gauges); ++gauge)
            object_property_add(OBJECT(machine), gauges[gauge], "uint64",
                                astra_host_budget_get, NULL, NULL, s);
    }
    object_property_add_uint64_ptr(OBJECT(machine), "astra-host-fs-resets",
                                   &s->host.fs_resets, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-submissions",
                                   &s->host.submissions,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-commands",
                                   &s->host.commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine), "astra-host-non-fs-commands",
                                   &s->host.non_fs_commands, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine), "astra-host-non-fs-execution-ns",
                                   &s->host.non_fs_execution_ns, OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-execution-ns",
                                   &s->host.execution_ns,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-execution-max-ns",
                                   &s->host.execution_max_ns,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-timer-late-max-ns",
                                   &s->timer_late_max_ns,
                                   OBJ_PROP_FLAG_READWRITE);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-timer-late-count",
                                   &s->timer_late_count,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-host-slow-commands",
                                   &s->host.slow_commands,
                                   OBJ_PROP_FLAG_READ);
    {
        static const struct {
            const char *count;
            const char *execution_ns;
        } properties[] = {
            { "astra-host-fs-invalid",
              "astra-host-fs-invalid-execution-ns" },
            { "astra-host-fs-open",
              "astra-host-fs-open-execution-ns" },
            { "astra-host-fs-close",
              "astra-host-fs-close-execution-ns" },
            { "astra-host-fs-read",
              "astra-host-fs-read-execution-ns" },
            { "astra-host-fs-write",
              "astra-host-fs-write-execution-ns" },
            { "astra-host-fs-sync",
              "astra-host-fs-sync-execution-ns" },
            { "astra-host-fs-truncate",
              "astra-host-fs-truncate-execution-ns" },
            { "astra-host-fs-stat",
              "astra-host-fs-stat-execution-ns" },
            { "astra-host-fs-readdir",
              "astra-host-fs-readdir-execution-ns" },
            { "astra-host-fs-mkdir",
              "astra-host-fs-mkdir-execution-ns" },
            { "astra-host-fs-unlink",
              "astra-host-fs-unlink-execution-ns" },
            { "astra-host-fs-rename",
              "astra-host-fs-rename-execution-ns" },
            { "astra-host-fs-chmod",
              "astra-host-fs-chmod-execution-ns" },
            { "astra-host-fs-readlink",
              "astra-host-fs-readlink-execution-ns" },
            { "astra-host-fs-symlink",
              "astra-host-fs-symlink-execution-ns" },
            { "astra-host-fs-link",
              "astra-host-fs-link-execution-ns" },
            { "astra-host-fs-filesystem-info",
              "astra-host-fs-filesystem-info-execution-ns" },
            { "astra-host-fs-session-open",
              "astra-host-fs-session-open-execution-ns" },
            { "astra-host-fs-session-close",
              "astra-host-fs-session-close-execution-ns" },
            { "astra-host-fs-claim",
              "astra-host-fs-claim-execution-ns" },
            { "astra-host-fs-seek", "astra-host-fs-seek-execution-ns" },
            { "astra-host-fs-share", "astra-host-fs-share-execution-ns" },
            { "astra-host-fs-adopt", "astra-host-fs-adopt-execution-ns" },
            { "astra-host-fs-file-flags", "astra-host-fs-file-flags-execution-ns" },
            { "astra-host-fs-path", "astra-host-fs-path-execution-ns" }
        };

        G_STATIC_ASSERT(G_N_ELEMENTS(properties) ==
                        ASTRA_HOST_FS_MAX + 1u);
        for (size_t index = 0; index < G_N_ELEMENTS(properties); ++index) {
            object_property_add_uint64_ptr(
                OBJECT(machine), properties[index].count,
                &s->host.operation_counts[index], OBJ_PROP_FLAG_READ);
            object_property_add_uint64_ptr(
                OBJECT(machine), properties[index].execution_ns,
                &s->host.operation_execution_ns[index], OBJ_PROP_FLAG_READ);
        }
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
                                   "astra-copy-lists",
                                   &s->copy_lists,
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
                                   "astra-display-render-only-batches",
                                   &s->display.render_only_batches,
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
                                   "astra-display-triangle-commands",
                                   &s->display.triangle_commands,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-surface-reads",
                                   &s->display.surface_reads,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-queued-submissions",
                                   &s->display.queued_submissions,
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
                                   "astra-input-coalesced",
                                   &s->input.coalesced,
                                   OBJ_PROP_FLAG_READ);
    object_property_add_uint64_ptr(OBJECT(machine),
                                   "astra-display-cursor-updates",
                                   &s->display.cursor_updates,
                                   OBJ_PROP_FLAG_READ);

    /* Real constraints only: POST minimum, page alignment, and aperture. */
    if (machine->ram_size < MiB ||
        (machine->ram_size & (4 * KiB - 1)) != 0 ||
        (uint64_t)ASTRA_SDRAM_BASE + machine->ram_size > ASTRA_ROM_ADDRESS) {
        error_report("Astra68 RAM must be at least 1 MiB, a multiple of 4 KiB,"
                     " and must fit below the ROM aperture at 0x%08x",
                     ASTRA_ROM_ADDRESS);
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
    memory_region_add_subregion(sysmem, ASTRA_ROM_ADDRESS, &s->rom);
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
                "astra68.display-mailbox", ASTRA_DISPLAY_MAILBOX_HEADER_BYTES,
                0, RAM_SHARED, display_mailbox_path, 0, &error_fatal)) {
            exit(EXIT_FAILURE);
        }
        s->display.mailbox = memory_region_get_ram_ptr(
            &s->display.mailbox_region);
        display_payload_path = g_getenv("ASTRA_DISPLAY_PAYLOAD_PATH");
        if (display_payload_path == NULL || display_payload_path[0] == '\0') {
            error_report("ASTRA_DISPLAY_MAILBOX_PATH requires "
                         "ASTRA_DISPLAY_PAYLOAD_PATH");
            exit(EXIT_FAILURE);
        }
        {
            int payload_fd = open(display_payload_path, O_RDWR | O_CLOEXEC);
            void *payload = MAP_FAILED;

            if (payload_fd >= 0) {
                payload = mmap(NULL, ASTRA_DISPLAY_MAILBOX_PAYLOAD_BYTES,
                               PROT_READ | PROT_WRITE, MAP_SHARED,
                               payload_fd, 0);
                close(payload_fd);
            }
            if (payload == MAP_FAILED) {
                error_report("cannot map display payload '%s': %s",
                             display_payload_path, strerror(errno));
                exit(EXIT_FAILURE);
            }
            s->display.payload = payload;
        }
        s->display.mailbox_enabled = true;
        /* Continue the file's counters: a helper that outlived an earlier
           QEMU compares against them, and a wake word that went back to a
           value it already holds could be lost. */
        s->display.wake_sequence =
            qatomic_read(&s->display.mailbox->wake_sequence);
        s->display.cursor_sequence =
            qatomic_read(&s->display.mailbox->cursor_sequence);
#ifdef CONFIG_LINUX
        qemu_event_init(&s->display.completion_armed, false);
        if (event_notifier_init(&s->display.completion_notifier, 0) < 0) {
            error_report("cannot create the display completion notifier");
            exit(EXIT_FAILURE);
        }
        event_notifier_set_handler(&s->display.completion_notifier,
                                   astra_display_completion_ready);
        qemu_thread_create(&s->display.completion_thread,
                           "astra-display-completion",
                           astra_display_completion_thread, &s->display,
                           QEMU_THREAD_DETACHED);
#endif
#else
        error_report("ASTRA_DISPLAY_MAILBOX_PATH requires a POSIX host");
        exit(EXIT_FAILURE);
#endif
    }

    astra_panel_host_init(s);
    astra_audio_mailbox_init(s);

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
    s->input.motion_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                         astra_input_release_motion, s);
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
    static const char * const valid_cpu_types[] = {
        M68K_CPU_TYPE_NAME("m68040"),
        NULL,
    };

    mc->desc = "Astra 68 reference machine";
    mc->init = astra68_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68040");
    mc->valid_cpu_types = valid_cpu_types;
    mc->default_ram_size = ASTRA_RAM_SIZE_DE25_GUEST;
    mc->default_ram_id = "astra68.sdram";
    mc->max_cpus = 1;
    mc->no_floppy = 1;
    mc->no_cdrom = 1;
    mc->no_parallel = 1;
}

DEFINE_MACHINE("astra68", astra68_machine_init)
