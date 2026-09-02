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

#include <stdint.h>

#include <astra/message_abi.h>
#include <astra/syscall.h>

#define ASTRA_DEVICE_CLASS_HOST UINT32_C(0x48414343) /* HACC */
#define ASTRA_DEVICE_ID_HOST0   UINT32_C(0x48410001)
#define ASTRA_CAPABILITY_HOST_DEVICE "HOST_DEVICE"

#define ASTRA_HOST_VERSION_1_1 UINT32_C(0x00010001)
#define ASTRA_HOST_VERSION_1_2 UINT32_C(0x00010002)
#define ASTRA_HOST_VERSION_1_3 UINT32_C(0x00010003)
#define ASTRA_HOST_VERSION     UINT32_C(0x00010004)
#define ASTRA_HOST_CAP_FILESYSTEM (1u << 0)
#define ASTRA_HOST_CAP_OWNER_SCOPED (1u << 1)
#define ASTRA_HOST_CAP_SUBMISSION_DESCRIPTOR (1u << 2)
#define ASTRA_HOST_CAP_CHANNEL (1u << 3)
#define ASTRA_HOST_CAP_CHANNEL_ARMED_IRQ (1u << 4)
#define ASTRA_HOST_STATE_READY    (1u << 0)

#define ASTRA_HOST_SERVICE_FILESYSTEM UINT16_C(1)
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
    ASTRA_HOST_FS_SYMLINK
};

/* Flags carried in the command header in addition to ASTRA_VFS_OPEN_*. */
#define ASTRA_HOST_FS_WRITE_APPEND (1u << 15)

#define ASTRA_HOST_COMMAND_VERSION 1u
#define ASTRA_HOST_COMMAND_SIZE 512u

/*
 * One filesystem command.  The paths match the VFS wire limit exactly; data
 * follows the command array inside the same DMA buffer.  Split 64-bit values
 * keep the layout identical on MC68030, Linux and a future RTL engine.
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

#endif
