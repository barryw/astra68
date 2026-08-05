#ifndef ASTRA_BOOT_H
#define ASTRA_BOOT_H

#include <stddef.h>
#include <stdint.h>

#define ASTRA_BOOT_HANDOFF_MAGIC 0x4136384bu /* "A68K" */
#define ASTRA_BOOT_INFO_MAGIC    0x41363842u /* "A68B" */
#define ASTRA_BOOT_ABI_MAJOR     0u
#define ASTRA_BOOT_ABI_MINOR     3u

#define ASTRA_BOOT_INFO_ADDRESS       0x01ff8000u
#define ASTRA_BOOT_SCRATCH_ADDRESS    0x01ff8000u
#define ASTRA_BOOT_SCRATCH_SIZE       0x00008000u
#define ASTRA_EARLY_LOG_ADDRESS       0x02000000u
#define ASTRA_EARLY_LOG_SIZE          0x00004000u
/*
 * The one firmware-supplied user image lands between the early log and the
 * kernel. Firmware reserves only the pages the image occupies and returns the
 * rest of the hole to the physical allocator.
 */
#define ASTRA_USER_IMAGE_ADDRESS      0x02004000u
#define ASTRA_USER_IMAGE_MAX_SIZE     0x0000c000u
#define ASTRA_USER_IMAGE_ALIGNMENT    0x00001000u
#define ASTRA_KERNEL_LOAD_ADDRESS     0x02010000u
#define ASTRA_KERNEL_TRACE_ADDRESS    0x02090000u
#define ASTRA_KERNEL_TRACE_SIZE       0x00010000u
#define ASTRA_KERNEL_RESERVED_SIZE    0x00090000u
#define ASTRA_KERNEL_USABLE_ADDRESS   0x020a0000u
#define ASTRA_KERNEL_USABLE_SIZE      0x01d60000u
#define ASTRA_ROM_BACKING_ADDRESS     0x03e00000u
#define ASTRA_ROM_BACKING_SIZE        0x00040000u

#define ASTRA_BOOT_FLAG_INTERRUPTS_MASKED (1u << 0)
#define ASTRA_BOOT_FLAG_PMMU_DISABLED     (1u << 1)
#define ASTRA_BOOT_FLAG_OVERLAY_DISABLED  (1u << 2)
#define ASTRA_BOOT_FLAG_DMA_IDLE          (1u << 3)
#define ASTRA_BOOT_FLAG_ICACHE_ENABLED    (1u << 4)
#define ASTRA_BOOT_FLAG_DCACHE_ENABLED    (1u << 5)
#define ASTRA_BOOT_REQUIRED_FLAGS         \
    (ASTRA_BOOT_FLAG_INTERRUPTS_MASKED | ASTRA_BOOT_FLAG_PMMU_DISABLED | \
     ASTRA_BOOT_FLAG_OVERLAY_DISABLED | ASTRA_BOOT_FLAG_DMA_IDLE)

#define ASTRA_MEMORY_READ        (1u << 0)
#define ASTRA_MEMORY_WRITE       (1u << 1)
#define ASTRA_MEMORY_EXECUTE     (1u << 2)
#define ASTRA_MEMORY_CACHEABLE   (1u << 3)
#define ASTRA_MEMORY_RECLAIMABLE (1u << 4)

#define ASTRA_EARLY_LOG_MAGIC       0x41364c47u /* "A6LG" */
#define ASTRA_EARLY_LOG_ABI_MAJOR   0u
#define ASTRA_EARLY_LOG_ABI_MINOR   1u
#define ASTRA_EARLY_LOG_FLAG_PANIC  (1u << 0)

#define ASTRA_KERNEL_STATUS_BOOTING 0x4b424f54u /* "KBOT" */
#define ASTRA_KERNEL_STATUS_READY   0x4b304f4bu /* "K0OK" */
#define ASTRA_KERNEL_STATUS_K1_READY 0x4b314f4bu /* "K1OK" */
#define ASTRA_KERNEL_STATUS_K1_SOAK 0x4b31534bu /* "K1SK" */
#define ASTRA_KERNEL_STATUS_PANIC   0x4b50414eu /* "KPAN" */

#define ASTRA_BOOT_MAX_MEMORY_RANGES 10u

typedef enum {
    ASTRA_MEMORY_RANGE_USABLE = 1,
    ASTRA_MEMORY_RANGE_FIRMWARE = 2,
    ASTRA_MEMORY_RANGE_EARLY_LOG = 3,
    ASTRA_MEMORY_RANGE_KERNEL = 4,
    ASTRA_MEMORY_RANGE_ROM_BACKING = 5,
    ASTRA_MEMORY_RANGE_DEVICE = 6
} AstraBootMemoryType;

typedef struct {
    uint32_t base;
    uint32_t size;
    uint32_t type;
    uint32_t flags;
} AstraBootMemoryRange;

typedef struct {
    uint32_t magic;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t total_size;
    uint32_t checksum;
    uint32_t flags;
    uint32_t machine_id;
    uint32_t hardware_build_id;
    uint32_t firmware_image_id;
    uint32_t cpu_model;
    uint32_t cpu_implementation;
    uint32_t cpu_features;
    uint32_t cpu_hz;
    uint32_t ram_base;
    uint32_t ram_size;
    uint32_t rom_base;
    uint32_t rom_size;
    uint32_t kernel_base;
    uint32_t kernel_image_size;
    uint32_t kernel_memory_size;
    uint32_t kernel_entry;
    uint32_t early_log_base;
    uint32_t early_log_size;
    /*
     * The single initial user image, or zero/zero when firmware supplies
     * none. This is a load description, not a filesystem: the kernel reads
     * these bytes in place and performs no lookup.
     */
    uint32_t user_image_base;
    uint32_t user_image_size;
    uint32_t memory_range_count;
    uint32_t memory_range_entry_size;
    AstraBootMemoryRange memory_ranges[ASTRA_BOOT_MAX_MEMORY_RANGES];
} AstraBootInfo;

typedef struct {
    uint32_t magic;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t total_size;
    uint32_t data_offset;
    uint32_t write_offset;
    uint32_t wrap_count;
    uint32_t flags;
    uint32_t sequence;
} AstraEarlyLog;

typedef enum {
    ASTRA_BOOT_VALID = 0,
    ASTRA_BOOT_BAD_POINTER,
    ASTRA_BOOT_BAD_MAGIC,
    ASTRA_BOOT_BAD_VERSION,
    ASTRA_BOOT_BAD_SIZE,
    ASTRA_BOOT_BAD_CHECKSUM,
    ASTRA_BOOT_BAD_FLAGS,
    ASTRA_BOOT_BAD_PLATFORM,
    ASTRA_BOOT_BAD_KERNEL,
    ASTRA_BOOT_BAD_LOG,
    ASTRA_BOOT_BAD_MEMORY_MAP,
    ASTRA_BOOT_BAD_USER_IMAGE
} AstraBootValidation;

uint32_t astra_boot_checksum(const AstraBootInfo *info);
void astra_boot_info_finalize(AstraBootInfo *info);
AstraBootValidation astra_boot_info_validate(const AstraBootInfo *info);
const char *astra_boot_validation_name(AstraBootValidation validation);

void astra_early_log_init(AstraEarlyLog *log, uint32_t total_size);
int astra_early_log_validate(const AstraEarlyLog *log, uint32_t mapped_size);
void astra_early_log_putc(AstraEarlyLog *log, char value);
void astra_early_log_puts(AstraEarlyLog *log, const char *text);

_Static_assert(sizeof(AstraBootMemoryRange) == 16u,
               "AstraBootMemoryRange ABI size");
_Static_assert(sizeof(AstraBootInfo) == 264u, "AstraBootInfo ABI size");
_Static_assert(sizeof(AstraEarlyLog) == 32u, "AstraEarlyLog ABI size");

#endif
