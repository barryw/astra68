#ifndef ASTRA_HOST_H
#define ASTRA_HOST_H

/*
 * Stable bulk transport between Astra services and an attached host.
 *
 * The kernel validates authority and DMA ownership but never interprets a
 * command.  Host descriptors, pointers and errno values never cross this
 * boundary.  Command classes are append-only; batching is part of version 1
 * so hardware and software implementations use the same data plane.
 */

#include <stddef.h>
#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/syscall.h>

#define ASTRA_DEVICE_CLASS_HOST UINT32_C(0x48414343) /* HACC */
#define ASTRA_DEVICE_ID_HOST0   UINT32_C(0x48410001)
#define ASTRA_CAPABILITY_HOST_DEVICE "HOST_DEVICE"

#define ASTRA_HOST_VERSION_1_1 UINT32_C(0x00010001)
#define ASTRA_HOST_VERSION_1_2 UINT32_C(0x00010002)
#define ASTRA_HOST_VERSION_1_3 UINT32_C(0x00010003)
#define ASTRA_HOST_VERSION_1_4 UINT32_C(0x00010004)
#define ASTRA_HOST_VERSION_1_5 UINT32_C(0x00010005)
#define ASTRA_HOST_VERSION_1_8 UINT32_C(0x00010008)
#define ASTRA_HOST_VERSION     UINT32_C(0x0001000a)
#define ASTRA_HOST_CAP_FILESYSTEM (1u << 0)
#define ASTRA_HOST_CAP_OWNER_SCOPED (1u << 1)
#define ASTRA_HOST_CAP_SUBMISSION_DESCRIPTOR (1u << 2)
#define ASTRA_HOST_CAP_CHANNEL (1u << 3)
#define ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ (1u << 4)
#define ASTRA_HOST_CAP_METRICS (1u << 5)
#define ASTRA_HOST_CAP_REMOTE_DESKTOP (1u << 6)
#define ASTRA_HOST_CAP_ENTROPY (1u << 7)
#define ASTRA_HOST_STATE_READY    (1u << 0)

#define ASTRA_HOST_SERVICE_FILESYSTEM UINT16_C(1)
#define ASTRA_HOST_SERVICE_METRICS UINT16_C(2)
#define ASTRA_HOST_SERVICE_REMOTE_DESKTOP UINT16_C(3)
#define ASTRA_HOST_SERVICE_ENTROPY UINT16_C(4)
#define ASTRA_HOST_FS_PATH_MAX 192u

enum {
    ASTRA_HOST_FS_OPEN = 1u,
    ASTRA_HOST_FS_CLOSE,
    ASTRA_HOST_FS_READ,
    ASTRA_HOST_FS_WRITE,
    ASTRA_HOST_FS_SYNC,
    ASTRA_HOST_FS_TRUNCATE,
    ASTRA_HOST_FS_STAT,
    ASTRA_HOST_FS_READDIR,
    ASTRA_HOST_FS_MKDIR,
    ASTRA_HOST_FS_UNLINK,
    ASTRA_HOST_FS_RENAME,
    ASTRA_HOST_FS_CHMOD,
    ASTRA_HOST_FS_READLINK,
    ASTRA_HOST_FS_SYMLINK,
    ASTRA_HOST_FS_LINK,
    ASTRA_HOST_FS_OPEN_AT,
    ASTRA_HOST_FS_UNLINK_AT,
    ASTRA_HOST_FS_CHMOD_FILE,
    ASTRA_HOST_FS_CHMOD_AT,
    ASTRA_HOST_FS_FILESYSTEM_INFO,
    ASTRA_HOST_FS_STAT_AT,
    ASTRA_HOST_FS_STAT_FILE
};
#define ASTRA_HOST_FS_MAX ASTRA_HOST_FS_STAT_FILE

/* Flags carried in the command header in addition to ASTRA_VFS_OPEN_*. */
#define ASTRA_HOST_FS_WRITE_APPEND (1u << 15)

#define ASTRA_HOST_METRICS_SNAPSHOT UINT16_C(1)
#define ASTRA_HOST_METRICS_VERSION UINT16_C(2)

#define ASTRA_HOST_REMOTE_DESKTOP_ACQUIRE UINT16_C(1)
#define ASTRA_HOST_REMOTE_DESKTOP_STATUS UINT16_C(2)

/** Fill the command data span with host cryptographic entropy. */
#define ASTRA_HOST_ENTROPY_FILL UINT16_C(1)
/** Host entropy requests follow the POSIX getentropy(3) bound. */
#define ASTRA_HOST_ENTROPY_MAX UINT32_C(256)

/*
 * A point-in-time, read-only view of the work done below Astra's VFS. Values
 * are append-only and indexed so this wire record can grow without exposing
 * QEMU structures or host pointers. Each 64-bit value is split explicitly;
 * the ABI is identical on the MC68040, Linux, and a future hardware bridge.
 */
enum {
    ASTRA_HOST_METRIC_TIMESTAMP_NS = 0,
    ASTRA_HOST_METRIC_BLOCK_READ_REQUESTS,
    ASTRA_HOST_METRIC_BLOCK_READ_SECTORS,
    ASTRA_HOST_METRIC_BLOCK_WRITE_REQUESTS,
    ASTRA_HOST_METRIC_BLOCK_WRITE_SECTORS,
    ASTRA_HOST_METRIC_BLOCK_FLUSH_REQUESTS,
    ASTRA_HOST_METRIC_BLOCK_DURABILITY_TRANSITIONS,
    ASTRA_HOST_METRIC_HOST_SUBMISSIONS,
    ASTRA_HOST_METRIC_HOST_COMMANDS,
    ASTRA_HOST_METRIC_HOST_EXECUTION_NS,
    ASTRA_HOST_METRIC_HOST_INFLIGHT,
    ASTRA_HOST_METRIC_HOST_MAX_INFLIGHT,
    ASTRA_HOST_METRIC_FS_COUNT_BASE,
    ASTRA_HOST_METRIC_FS_EXECUTION_NS_BASE =
        ASTRA_HOST_METRIC_FS_COUNT_BASE + ASTRA_HOST_FS_MAX + 1u,
    ASTRA_HOST_METRIC_COUNT =
        ASTRA_HOST_METRIC_FS_EXECUTION_NS_BASE +
        ASTRA_HOST_FS_MAX + 1u
};

typedef struct AstraHostMetricValue {
    uint32_t hi;
    uint32_t lo;
} AstraHostMetricValue;

#define ASTRA_HOST_METRICS_SNAPSHOT_SIZE 480u
typedef struct AstraHostMetricsSnapshot {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t version;
    uint16_t count;
    uint32_t reserved[2];
    AstraHostMetricValue values[ASTRA_HOST_METRIC_COUNT];
} AstraHostMetricsSnapshot;

_Static_assert(sizeof(AstraHostMetricsSnapshot) ==
                   ASTRA_HOST_METRICS_SNAPSHOT_SIZE,
               "host metrics snapshot ABI changed");

/* Fixed big-endian payload returned by ASTRA_HOST_FS_FILESYSTEM_INFO. */
#define ASTRA_HOST_FILESYSTEM_INFO_SIZE 64u
typedef struct AstraHostFilesystemInfo {
    uint32_t size;
    uint32_t flags;
    uint32_t block_size;
    uint32_t fragment_size;
    uint32_t blocks_hi;
    uint32_t blocks_lo;
    uint32_t blocks_free_hi;
    uint32_t blocks_free_lo;
    uint32_t blocks_available_hi;
    uint32_t blocks_available_lo;
    uint32_t files_hi;
    uint32_t files_lo;
    uint32_t files_free_hi;
    uint32_t files_free_lo;
    uint32_t name_max;
    uint32_t reserved;
} AstraHostFilesystemInfo;

_Static_assert(sizeof(AstraHostFilesystemInfo) ==
                   ASTRA_HOST_FILESYSTEM_INFO_SIZE,
               "host filesystem-info ABI changed");

static inline uint64_t
astra_host_metric_value(const AstraHostMetricsSnapshot *snapshot,
                        uint32_t metric)
{
    return snapshot != NULL && metric < snapshot->count ?
        ((uint64_t)snapshot->values[metric].hi << 32) |
            snapshot->values[metric].lo : 0u;
}

static inline void
astra_host_metric_set(AstraHostMetricsSnapshot *snapshot, uint32_t metric,
                      uint64_t value)
{
    if (snapshot == NULL || metric >= ASTRA_HOST_METRIC_COUNT)
        return;
    snapshot->values[metric].hi = (uint32_t)(value >> 32);
    snapshot->values[metric].lo = (uint32_t)value;
}

#define ASTRA_HOST_COMMAND_VERSION 1u
#define ASTRA_HOST_COMMAND_SIZE 512u

/*
 * One filesystem command.  The paths match the VFS wire limit exactly; data
 * follows the command array inside the same DMA buffer.  Split 64-bit values
 * keep the layout identical on MC68040, Linux and a future RTL engine.
 */
typedef struct AstraHostCommand {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t version;
    uint16_t service;
    uint16_t operation;
    uint16_t flags;
    uint32_t status;
    uint32_t handle;
    uint32_t generation;
    uint32_t offset_hi;
    uint32_t offset_lo;
    uint32_t value_hi;
    uint32_t value_lo;
    uint32_t data_offset;
    uint32_t data_length;
    uint32_t data_capacity;
    uint32_t result_length;
    uint32_t result_value;
    uint32_t reserved0;
    uint32_t node_size_hi;
    uint32_t node_size_lo;
    uint32_t mtime_hi;
    uint32_t mtime_lo;
    uint32_t uid;
    uint32_t gid;
    uint16_t kind;
    uint16_t mode;
    uint16_t nlink;
    uint16_t reserved1;
    char path[ASTRA_HOST_FS_PATH_MAX];
    char path2[ASTRA_HOST_FS_PATH_MAX];
    uint32_t reserved[8];
} AstraHostCommand;

_Static_assert(sizeof(AstraHostCommand) == ASTRA_HOST_COMMAND_SIZE,
               "host command ABI changed");

#define ASTRA_HOST_LEASE_INFO_SIZE 32u
typedef struct AstraHostLeaseInfo {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t capabilities;
    uint32_t state_flags;
    uint32_t host_generation;
    uint32_t maximum_transfer;
    uint32_t maximum_commands;
    uint32_t reserved[2];
} AstraHostLeaseInfo;

#define ASTRA_HOST_TRANSPORT_REQUEST_SIZE 24u
typedef struct AstraHostTransportRequest {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t buffer;
    uint32_t buffer_offset;
    uint32_t byte_size;
    uint32_t command_count;
    uint32_t reserved;
} AstraHostTransportRequest;

_Static_assert(sizeof(AstraHostLeaseInfo) == ASTRA_HOST_LEASE_INFO_SIZE,
               "host lease ABI changed");
_Static_assert(sizeof(AstraHostTransportRequest) ==
                   ASTRA_HOST_TRANSPORT_REQUEST_SIZE,
               "host request ABI changed");

/*
 * Kernel-authenticated submission programmed with one MMIO doorbell.  This
 * record lives in kernel memory: user mode supplies the DMA handle, while the
 * kernel supplies the physical range and current process owner.  A software
 * host and a future FPGA engine therefore consume the same bounded batch
 * without trusting an owner or physical pointer written by an application.
 */
#define ASTRA_HOST_SUBMISSION_VERSION 1u
#define ASTRA_HOST_SUBMISSION_SIZE 64u
typedef struct AstraHostSubmission {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t version;
    uint16_t flags;
    uint32_t owner;
    uint32_t host_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_count;
    uint32_t reserved[9];
} AstraHostSubmission;

_Static_assert(sizeof(AstraHostSubmission) == ASTRA_HOST_SUBMISSION_SIZE,
               "host submission ABI changed");

/*
 * One owner-bound host channel.  Axiom authenticates and pins the backing DMA
 * buffer once; user mode then publishes batches and rings only its isolated
 * doorbell page.  Filesystem and hosted-tool messages share this transport.
 */
#define ASTRA_HOST_CHANNEL_MAGIC UINT32_C(0x41484348) /* "AHCH" */
#define ASTRA_HOST_CHANNEL_VERSION 1u
#define ASTRA_HOST_CHANNEL_PHYSICAL_BASE UINT32_C(0xffd00000)
#define ASTRA_HOST_CHANNEL_APERTURE_SIZE UINT32_C(0x00100000)
#define ASTRA_HOST_CHANNEL_PAGE_SIZE UINT32_C(0x00001000)
#define ASTRA_HOST_CHANNEL_COUNT \
    (ASTRA_HOST_CHANNEL_APERTURE_SIZE / ASTRA_HOST_CHANNEL_PAGE_SIZE)
#define ASTRA_HOST_CHANNEL_HEADER_SIZE 64u
#define ASTRA_HOST_CHANNEL_STATE_OFFSET      0x08u
#define ASTRA_HOST_CHANNEL_GENERATION_OFFSET 0x0cu
#define ASTRA_HOST_CHANNEL_CONSUMER_OFFSET   0x10u
#define ASTRA_HOST_CHANNEL_STATUS_OFFSET     0x14u
#define ASTRA_HOST_CHANNEL_KICK_OFFSET       0x20u
#define ASTRA_HOST_CHANNEL_ARM_OFFSET        0x24u
#define ASTRA_HOST_CHANNEL_DISARM_OFFSET     0x28u
typedef struct AstraHostChannelHeader {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t command_size;
    uint32_t command_capacity;
    uint32_t command_offset;
    uint32_t data_offset;
    uint32_t total_size;
    uint32_t channel_generation;
    volatile uint32_t producer_position;
    uint32_t reserved0[2];
    volatile uint32_t consumer_position;
    volatile uint32_t transport_status;
    uint32_t reserved1[2];
} AstraHostChannelHeader;

_Static_assert(sizeof(AstraHostChannelHeader) ==
                   ASTRA_HOST_CHANNEL_HEADER_SIZE,
               "host channel header ABI changed");

#define ASTRA_HOST_CHANNEL_OPEN_SIZE 48u
typedef struct AstraHostChannelOpen {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint32_t flags;
    uint32_t buffer;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t channel_generation;
    uint32_t channel_address;
    uint32_t host_generation;
    uint32_t reserved[4];
} AstraHostChannelOpen;

_Static_assert(sizeof(AstraHostChannelOpen) == ASTRA_HOST_CHANNEL_OPEN_SIZE,
               "host channel open ABI changed");

#define ASTRA_HOST_CHANNEL_CONFIG_VERSION 1u
#define ASTRA_HOST_CHANNEL_CONFIG_SIZE 64u
#define ASTRA_HOST_CHANNEL_CONFIG_OPEN  1u
#define ASTRA_HOST_CHANNEL_CONFIG_CLOSE 2u
typedef struct AstraHostChannelConfig {
    _Alignas(ASTRA_ABI_ALIGNMENT) uint32_t size;
    uint16_t version;
    uint16_t operation;
    uint32_t slot;
    uint32_t owner;
    uint32_t host_generation;
    uint32_t channel_generation;
    uint32_t physical_buffer;
    uint32_t byte_size;
    uint32_t command_capacity;
    uint32_t reserved[7];
} AstraHostChannelConfig;

_Static_assert(sizeof(AstraHostChannelConfig) ==
                   ASTRA_HOST_CHANNEL_CONFIG_SIZE,
               "host channel config ABI changed");

/** Reusable one-command client for low-volume host services.
 *
 * The runtime owns channel setup, DMA lifetime, publication fences, and
 * completion validation. High-throughput providers such as the VFS retain
 * their per-thread, multi-command lanes instead of serializing through this
 * single-command convenience client.
 */
typedef struct AstraHostChannelClient {
    uint32_t device;          /**< Borrowed host-device capability. */
    uint32_t dma;             /**< Runtime-owned DMA area handle. */
    uint32_t channel_address; /**< Kernel-authenticated doorbell page. */
    uint32_t generation;      /**< Host generation captured at open. */
    uint32_t producer;        /**< Monotonic command sequence. */
    uint32_t byte_size;       /**< Mapped DMA span. */
    uint32_t data_capacity;   /**< Bytes following the command slot. */
    volatile AstraHostChannelHeader *header; /**< Shared channel header. */
    volatile AstraHostCommand *command;      /**< Sole command slot. */
    volatile uint8_t *data;                  /**< Command data span. */
} AstraHostChannelClient;

/** Open a reusable one-command host channel.
 * @param device Host-device capability.
 * @param required_capabilities ASTRA_HOST_CAP_* bits the caller requires.
 * @param data_capacity Bytes required after the command slot.
 * @param client Zeroed client receiving the opened channel.
 * @return ASTRA_SYSCALL_* status.
 */
uint32_t astra_host_client_open(uint32_t device,
                                uint32_t required_capabilities,
                                uint32_t data_capacity,
                                AstraHostChannelClient *client);
/** Reset and initialize the client's command slot.
 * @return Writable command slot, or NULL for an invalid client.
 */
AstraHostCommand *astra_host_client_prepare(AstraHostChannelClient *client,
                                            uint16_t service,
                                            uint16_t operation);
/** Publish the prepared command and wait for its completion.
 * @return ASTRA_SYSCALL_* transport status; command status remains in slot.
 */
uint32_t astra_host_client_submit(AstraHostChannelClient *client);
/** Close the channel and its DMA area, leaving the client zeroed.
 * @return First ASTRA_SYSCALL_* close failure, or ASTRA_SYSCALL_OK.
 */
uint32_t astra_host_client_close(AstraHostChannelClient *client);

#endif
