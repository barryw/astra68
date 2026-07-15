#include <astra/boot.h>

static int range_end(uint32_t base, uint32_t size, uint32_t *end)
{
    if (size == 0u || base > UINT32_MAX - size) return 0;
    *end = base + size;
    return 1;
}

uint32_t astra_boot_checksum(const AstraBootInfo *info)
{
    const uint32_t *words = (const uint32_t *)info;
    uint32_t sum = 0u;

    for (size_t index = 0; index < sizeof(*info) / sizeof(*words); ++index)
        sum += words[index];
    return sum;
}

void astra_boot_info_finalize(AstraBootInfo *info)
{
    info->checksum = 0u;
    info->checksum = 0u - astra_boot_checksum(info);
}

AstraBootValidation astra_boot_info_validate(const AstraBootInfo *info)
{
    uint32_t ram_end;
    uint32_t kernel_image_end;
    uint32_t kernel_memory_end;
    uint32_t log_end;
    uint32_t previous_end = 0u;

    if (info == NULL) return ASTRA_BOOT_BAD_POINTER;
    if (info->magic != ASTRA_BOOT_INFO_MAGIC) return ASTRA_BOOT_BAD_MAGIC;
    if (info->abi_major != ASTRA_BOOT_ABI_MAJOR ||
        info->abi_minor > ASTRA_BOOT_ABI_MINOR)
        return ASTRA_BOOT_BAD_VERSION;
    if (info->total_size != sizeof(*info)) return ASTRA_BOOT_BAD_SIZE;
    if (astra_boot_checksum(info) != 0u) return ASTRA_BOOT_BAD_CHECKSUM;
    if ((info->flags & ASTRA_BOOT_REQUIRED_FLAGS) != ASTRA_BOOT_REQUIRED_FLAGS)
        return ASTRA_BOOT_BAD_FLAGS;
    if (!range_end(info->ram_base, info->ram_size, &ram_end) ||
        info->cpu_hz == 0u || info->rom_size == 0u)
        return ASTRA_BOOT_BAD_PLATFORM;
    if (!range_end(info->kernel_base, info->kernel_image_size,
                   &kernel_image_end) ||
        !range_end(info->kernel_base, info->kernel_memory_size,
                   &kernel_memory_end) ||
        info->kernel_image_size > info->kernel_memory_size ||
        info->kernel_entry < info->kernel_base ||
        info->kernel_entry >= kernel_image_end ||
        info->kernel_base < info->ram_base || kernel_memory_end > ram_end)
        return ASTRA_BOOT_BAD_KERNEL;
    if (!range_end(info->early_log_base, info->early_log_size, &log_end) ||
        info->early_log_size <= sizeof(AstraEarlyLog) ||
        info->early_log_base < info->ram_base || log_end > ram_end ||
        !(log_end <= info->kernel_base ||
          info->early_log_base >= kernel_memory_end))
        return ASTRA_BOOT_BAD_LOG;
    if (info->memory_range_count == 0u ||
        info->memory_range_count > ASTRA_BOOT_MAX_MEMORY_RANGES ||
        info->memory_range_entry_size != sizeof(AstraBootMemoryRange))
        return ASTRA_BOOT_BAD_MEMORY_MAP;

    for (uint32_t index = 0; index < info->memory_range_count; ++index) {
        const AstraBootMemoryRange *range = &info->memory_ranges[index];
        uint32_t end;

        if (!range_end(range->base, range->size, &end) ||
            (index != 0u && range->base < previous_end) ||
            range->type < ASTRA_MEMORY_RANGE_USABLE ||
            range->type > ASTRA_MEMORY_RANGE_ROM_BACKING)
            return ASTRA_BOOT_BAD_MEMORY_MAP;
        previous_end = end;
    }
    return ASTRA_BOOT_VALID;
}

const char *astra_boot_validation_name(AstraBootValidation validation)
{
    static const char *const names[] = {
        "valid", "pointer", "magic", "version", "size", "checksum",
        "flags", "platform", "kernel", "log", "memory map"
    };

    if ((unsigned)validation >= sizeof(names) / sizeof(names[0]))
        return "unknown";
    return names[validation];
}

void astra_early_log_init(AstraEarlyLog *log, uint32_t total_size)
{
    log->magic = ASTRA_EARLY_LOG_MAGIC;
    log->abi_major = ASTRA_EARLY_LOG_ABI_MAJOR;
    log->abi_minor = ASTRA_EARLY_LOG_ABI_MINOR;
    log->total_size = total_size;
    log->data_offset = sizeof(*log);
    log->write_offset = 0u;
    log->wrap_count = 0u;
    log->flags = 0u;
    log->sequence = 1u;
}

int astra_early_log_validate(const AstraEarlyLog *log, uint32_t mapped_size)
{
    return log != NULL && log->magic == ASTRA_EARLY_LOG_MAGIC &&
           log->abi_major == ASTRA_EARLY_LOG_ABI_MAJOR &&
           log->abi_minor <= ASTRA_EARLY_LOG_ABI_MINOR &&
           log->total_size <= mapped_size &&
           log->data_offset >= sizeof(*log) &&
           log->data_offset < log->total_size &&
           log->write_offset < log->total_size - log->data_offset;
}

void astra_early_log_putc(AstraEarlyLog *log, char value)
{
    uint32_t capacity;
    volatile uint8_t *data;

    if (!astra_early_log_validate(log, log != NULL ? log->total_size : 0u))
        return;
    capacity = log->total_size - log->data_offset;
    data = (volatile uint8_t *)log + log->data_offset;
    data[log->write_offset] = (uint8_t)value;
    ++log->write_offset;
    if (log->write_offset == capacity) {
        log->write_offset = 0u;
        ++log->wrap_count;
    }
}

void astra_early_log_puts(AstraEarlyLog *log, const char *text)
{
    while (*text != '\0') astra_early_log_putc(log, *text++);
}
