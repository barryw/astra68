#include "elf.h"

#include "bytes.h"

/* e_ident */
#define ELF_IDENT_SIZE 16u
#define ELF_CLASS_32 1u
#define ELF_DATA_BIG 2u
#define ELF_VERSION_CURRENT 1u
#define ELF_OSABI_SYSV 0u

#define ELF_TYPE_EXEC 2u
#define ELF_TYPE_DYN 3u
#define ELF_MACHINE_68K 4u

#define ELF_PT_NULL 0u
#define ELF_PT_LOAD 1u
#define ELF_PT_DYNAMIC 2u
#define ELF_PT_INTERP 3u
#define ELF_PT_NOTE 4u
#define ELF_PT_SHLIB 5u
#define ELF_PT_PHDR 6u
#define ELF_PT_TLS 7u
#define ELF_PT_GNU_EH_FRAME 0x6474e550u
#define ELF_PT_GNU_STACK 0x6474e551u
#define ELF_PT_GNU_RELRO 0x6474e552u
#define ELF_PT_GNU_PROPERTY 0x6474e553u

#define ELF_PF_X 1u
#define ELF_PF_W 2u
#define ELF_PF_R 4u

/*
 * All header fields are read byte by byte. The image is untrusted and may be
 * unaligned, and the host test build is little-endian, so a struct overlay
 * would be both a fault risk and wrong.
 */
static uint16_t read_be16(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static bool is_power_of_two(uint32_t value)
{
    return value != 0u && (value & (value - 1u)) == 0u;
}

/* Sum without wrapping; false means the addition would overflow. */
static bool add_checked(uint32_t left, uint32_t right, uint32_t *result)
{
    if (left > 0xffffffffu - right)
        return false;
    *result = left + right;
    return true;
}

static bool range_within(uint32_t offset, uint32_t length, uint32_t limit)
{
    uint32_t end;

    if (!add_checked(offset, length, &end))
        return false;
    return end <= limit;
}

static KernelElfStatus check_identity(const uint8_t *image)
{
    uint32_t index;

    if (image[0] != 0x7fu || image[1] != 'E' || image[2] != 'L' ||
        image[3] != 'F')
        return KERNEL_ELF_BAD_MAGIC;
    if (image[4] != ELF_CLASS_32)
        return KERNEL_ELF_BAD_CLASS;
    if (image[5] != ELF_DATA_BIG)
        return KERNEL_ELF_BAD_ENDIAN;
    if (image[6] != ELF_VERSION_CURRENT)
        return KERNEL_ELF_BAD_VERSION;
    if (image[7] != ELF_OSABI_SYSV || image[8] != 0u)
        return KERNEL_ELF_BAD_ABI;
    /* Reserved identification bytes must be zero, not merely ignored. */
    for (index = 9u; index < ELF_IDENT_SIZE; ++index) {
        if (image[index] != 0u)
            return KERNEL_ELF_BAD_ABI;
    }
    return KERNEL_ELF_OK;
}

static KernelElfStatus segment_rights(uint32_t flags, uint32_t *rights)
{
    if ((flags & ~(ELF_PF_R | ELF_PF_W | ELF_PF_X)) != 0u)
        return KERNEL_ELF_BAD_PERMISSIONS;
    if ((flags & ELF_PF_R) == 0u)
        return KERNEL_ELF_BAD_PERMISSIONS;
    if ((flags & ELF_PF_W) != 0u && (flags & ELF_PF_X) != 0u)
        return KERNEL_ELF_BAD_PERMISSIONS;

    *rights = KERNEL_ELF_SEGMENT_READ;
    if ((flags & ELF_PF_W) != 0u)
        *rights |= KERNEL_ELF_SEGMENT_WRITE;
    if ((flags & ELF_PF_X) != 0u)
        *rights |= KERNEL_ELF_SEGMENT_EXEC;
    return KERNEL_ELF_OK;
}

/*
 * Program header types Astra neither loads nor objects to. Everything not
 * listed here, and not PT_LOAD, is a rejection: an unknown segment type in an
 * executable means the toolchain produced something this profile has not
 * qualified.
 */
static bool ignorable_segment(uint32_t type, bool library)
{
    return type == ELF_PT_NULL || type == ELF_PT_NOTE ||
           type == ELF_PT_PHDR || type == ELF_PT_GNU_EH_FRAME ||
           type == ELF_PT_GNU_RELRO || type == ELF_PT_GNU_PROPERTY ||
           (library && type == ELF_PT_DYNAMIC);
}

static KernelElfStatus accept_load_segment(const uint8_t *header,
                                           const KernelElfLimits *limits,
                                           uint32_t image_size,
                                           KernelElfSegment *segment)
{
    uint32_t page_mask = limits->page_size - 1u;
    uint32_t file_offset = read_be32(header + 4);
    uint32_t virtual_address = read_be32(header + 8);
    uint32_t file_size = read_be32(header + 16);
    uint32_t memory_size = read_be32(header + 20);
    uint32_t alignment = read_be32(header + 28);
    uint32_t rights;
    uint32_t span;
    uint32_t last;
    KernelElfStatus status;

    status = segment_rights(read_be32(header + 24), &rights);
    if (status != KERNEL_ELF_OK)
        return status;

    if (memory_size == 0u || file_size > memory_size)
        return KERNEL_ELF_BAD_RANGE;
    if (!range_within(file_offset, file_size, image_size))
        return KERNEL_ELF_BAD_RANGE;

    /*
     * Both the file offset and the virtual address are required to be page
     * aligned. The looser congruence rule the ELF specification permits lets
     * two segments share a page, which would force one page to carry the union
     * of two permission sets. Astra refuses the image instead; its own link
     * script places every segment on its own page.
     */
    if ((file_offset & page_mask) != 0u || (virtual_address & page_mask) != 0u)
        return KERNEL_ELF_BAD_ALIGNMENT;
    if (alignment != 0u &&
        (!is_power_of_two(alignment) || alignment < limits->page_size))
        return KERNEL_ELF_BAD_ALIGNMENT;

    if (!add_checked(memory_size, page_mask, &span))
        return KERNEL_ELF_BAD_RANGE;
    span &= ~page_mask;
    if (!add_checked(virtual_address, span - 1u, &last))
        return KERNEL_ELF_BAD_RANGE;
    if (virtual_address < limits->minimum_address ||
        last > limits->maximum_address)
        return KERNEL_ELF_BAD_RANGE;

    segment->file_offset = file_offset;
    segment->file_size = file_size;
    segment->virtual_address = virtual_address;
    segment->memory_size = memory_size;
    segment->page_count = span / limits->page_size;
    segment->rights = rights;
    return KERNEL_ELF_OK;
}

static KernelElfStatus accept_windowed(const void *image,
                                       uint32_t image_size,
                                       uint32_t readable,
                                       const KernelElfLimits *limits,
                                       uint16_t expected_type,
                                       bool library,
                                       KernelElfImage *plan)
{
    const uint8_t *bytes = image;
    uint32_t header_offset;
    uint32_t header_count;
    uint32_t header_size;
    uint32_t table_bytes;
    uint32_t index;
    uint32_t total_pages = 0u;
    uint32_t entry;
    KernelElfStatus status;

    if (image == NULL || limits == NULL || plan == NULL ||
        !is_power_of_two(limits->page_size) || limits->maximum_pages == 0u ||
        limits->minimum_address > limits->maximum_address)
        return KERNEL_ELF_INVALID_ARGUMENT;

    kernel_bytes_clear(plan, sizeof(*plan));

    if (image_size < KERNEL_ELF_HEADER_SIZE ||
        readable < KERNEL_ELF_HEADER_SIZE)
        return KERNEL_ELF_TRUNCATED;

    status = check_identity(bytes);
    if (status != KERNEL_ELF_OK)
        return status;

    if (read_be16(bytes + 16) != expected_type)
        return KERNEL_ELF_BAD_TYPE;
    if (read_be16(bytes + 18) != ELF_MACHINE_68K)
        return KERNEL_ELF_BAD_MACHINE;
    if (read_be32(bytes + 20) != ELF_VERSION_CURRENT)
        return KERNEL_ELF_BAD_VERSION;
    if (read_be32(bytes + 36) != 0u)
        return KERNEL_ELF_BAD_FLAGS;
    if (read_be16(bytes + 40) != KERNEL_ELF_HEADER_SIZE)
        return KERNEL_ELF_BAD_HEADER_TABLE;

    entry = read_be32(bytes + 24);
    header_offset = read_be32(bytes + 28);
    header_size = read_be16(bytes + 42);
    header_count = read_be16(bytes + 44);

    if (header_size != KERNEL_ELF_PHENTSIZE)
        return KERNEL_ELF_BAD_HEADER_TABLE;
    if (header_count == 0u)
        return KERNEL_ELF_NO_SEGMENTS;
    if (header_count > 0xffffffffu / KERNEL_ELF_PHENTSIZE)
        return KERNEL_ELF_BAD_HEADER_TABLE;
    table_bytes = header_count * KERNEL_ELF_PHENTSIZE;
    if (!range_within(header_offset, table_bytes, image_size))
        return KERNEL_ELF_BAD_HEADER_TABLE;
    /*
     * The table has to be inside the bytes the caller actually handed over. A
     * loader reading an image out of another address space holds a window on
     * it, not the whole file, and a program header table that points past that
     * window would be read out of whatever follows the window in the kernel.
     * Refusing it also refuses the file that puts its table at the far end of a
     * large image, which nothing this machine links produces and which exists
     * mainly to make a loader read somewhere it did not mean to.
     */
    if (!range_within(header_offset, table_bytes, readable))
        return KERNEL_ELF_BAD_HEADER_TABLE;

    for (index = 0u; index < header_count; ++index) {
        const uint8_t *header =
            bytes + header_offset + (index * KERNEL_ELF_PHENTSIZE);
        uint32_t type = read_be32(header);
        KernelElfSegment segment;

        if (type == ELF_PT_GNU_STACK) {
            if ((read_be32(header + 24) & ELF_PF_X) != 0u)
                return KERNEL_ELF_EXECUTABLE_STACK;
            continue;
        }
        if (type != ELF_PT_LOAD) {
            if (ignorable_segment(type, library))
                continue;
            return KERNEL_ELF_UNSUPPORTED_SEGMENT;
        }
        /*
         * A linker emits an empty PT_LOAD when an output section it was told
         * to place turns out to have no content. There is nothing to map, so
         * it is skipped rather than rejected; an empty segment that still
         * claims file content is malformed and is not skipped.
         */
        if (read_be32(header + 20) == 0u) {
            if (read_be32(header + 16) != 0u)
                return KERNEL_ELF_BAD_RANGE;
            continue;
        }
        if (plan->segment_count == KERNEL_ELF_SEGMENT_MAX)
            return KERNEL_ELF_TOO_MANY_SEGMENTS;

        status = accept_load_segment(header, limits, image_size, &segment);
        if (status != KERNEL_ELF_OK)
            return status;

        if (plan->segment_count != 0u) {
            const KernelElfSegment *previous =
                &plan->segment[plan->segment_count - 1u];
            uint32_t previous_end = previous->virtual_address +
                                    (previous->page_count *
                                     limits->page_size);

            if (segment.virtual_address < previous->virtual_address)
                return KERNEL_ELF_UNORDERED;
            if (segment.virtual_address < previous_end)
                return KERNEL_ELF_OVERLAP;
        }

        if (!add_checked(total_pages, segment.page_count, &total_pages))
            return KERNEL_ELF_TOO_LARGE;
        if (total_pages > limits->maximum_pages)
            return KERNEL_ELF_TOO_LARGE;

        plan->segment[plan->segment_count] = segment;
        ++plan->segment_count;
    }

    if (plan->segment_count == 0u) {
        kernel_bytes_clear(plan, sizeof(*plan));
        return KERNEL_ELF_NO_SEGMENTS;
    }

    if (library) {
        if (entry != 0u) {
            kernel_bytes_clear(plan, sizeof(*plan));
            return KERNEL_ELF_BAD_ENTRY;
        }
        plan->total_pages = total_pages;
        return KERNEL_ELF_OK;
    }

    /*
     * The entry point must land inside an executable segment. Accepting an
     * entry that merely falls inside the address range would let an image
     * start executing in its own data.
     */
    if ((entry & 1u) != 0u) {
        kernel_bytes_clear(plan, sizeof(*plan));
        return KERNEL_ELF_BAD_ENTRY;
    }
    for (index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment = &plan->segment[index];

        if ((segment->rights & KERNEL_ELF_SEGMENT_EXEC) == 0u)
            continue;
        if (entry >= segment->virtual_address &&
            entry < segment->virtual_address + segment->memory_size) {
            plan->entry = entry;
            plan->total_pages = total_pages;
            return KERNEL_ELF_OK;
        }
    }

    kernel_bytes_clear(plan, sizeof(*plan));
    return KERNEL_ELF_BAD_ENTRY;
}

const char *kernel_elf_status_text(KernelElfStatus status)
{
    switch (status) {
    case KERNEL_ELF_OK: return "ok";
    case KERNEL_ELF_INVALID_ARGUMENT: return "invalid argument";
    case KERNEL_ELF_TRUNCATED: return "truncated";
    case KERNEL_ELF_BAD_MAGIC: return "bad magic";
    case KERNEL_ELF_BAD_CLASS: return "not ELF32";
    case KERNEL_ELF_BAD_ENDIAN: return "not big endian";
    case KERNEL_ELF_BAD_VERSION: return "bad version";
    case KERNEL_ELF_BAD_ABI: return "bad ABI identification";
    case KERNEL_ELF_BAD_TYPE: return "not an executable";
    case KERNEL_ELF_BAD_MACHINE: return "not MC68000 family";
    case KERNEL_ELF_BAD_FLAGS: return "non-zero processor flags";
    case KERNEL_ELF_BAD_HEADER_TABLE: return "bad program header table";
    case KERNEL_ELF_NO_SEGMENTS: return "no loadable segments";
    case KERNEL_ELF_TOO_MANY_SEGMENTS: return "too many loadable segments";
    case KERNEL_ELF_UNSUPPORTED_SEGMENT: return "unsupported segment type";
    case KERNEL_ELF_EXECUTABLE_STACK: return "executable stack";
    case KERNEL_ELF_BAD_PERMISSIONS: return "bad segment permissions";
    case KERNEL_ELF_BAD_ALIGNMENT: return "segment not page aligned";
    case KERNEL_ELF_BAD_RANGE: return "segment outside the permitted range";
    case KERNEL_ELF_UNORDERED: return "segments out of order";
    case KERNEL_ELF_OVERLAP: return "segments overlap";
    case KERNEL_ELF_TOO_LARGE: return "image needs too many pages";
    case KERNEL_ELF_BAD_ENTRY: return "entry point outside executable code";
    }
    return "unknown";
}

KernelElfStatus kernel_elf_accept(const void *image, uint32_t image_size,
                                  const KernelElfLimits *limits,
                                  KernelElfImage *plan)
{
    /* The whole image is readable: the firmware's case, and every test's. */
    return kernel_elf_accept_windowed(image, image_size, image_size, limits,
                                      plan);
}

KernelElfStatus kernel_elf_accept_windowed(const void *image,
                                           uint32_t image_size,
                                           uint32_t readable,
                                           const KernelElfLimits *limits,
                                           KernelElfImage *plan)
{
    return accept_windowed(image, image_size, readable, limits,
                           ELF_TYPE_EXEC, false, plan);
}

KernelElfStatus kernel_elf_accept_library_windowed(
    const void *image, uint32_t image_size, uint32_t readable,
    const KernelElfLimits *limits, KernelElfImage *plan)
{
    return accept_windowed(image, image_size, readable, limits,
                           ELF_TYPE_DYN, true, plan);
}

KernelElfStatus kernel_elf_accept_library(const void *image,
                                          uint32_t image_size,
                                          const KernelElfLimits *limits,
                                          KernelElfImage *plan)
{
    return kernel_elf_accept_library_windowed(image, image_size, image_size,
                                              limits, plan);
}
