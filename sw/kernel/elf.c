#include "elf.h"

#include "bytes.h"

#include <astra/endian.h>
#include <astra/integer.h>

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
static bool range_within(uint32_t offset, uint32_t length, uint32_t limit)
{
    uint32_t end;

    if (!astra_u32_add_checked(offset, length, &end))
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
    uint32_t file_offset = astra_load_be32(header + 4);
    uint32_t virtual_address = astra_load_be32(header + 8);
    uint32_t file_size = astra_load_be32(header + 16);
    uint32_t memory_size = astra_load_be32(header + 20);
    uint32_t alignment = astra_load_be32(header + 28);
    uint32_t rights;
    uint32_t span;
    uint32_t last;
    KernelElfStatus status;

    status = segment_rights(astra_load_be32(header + 24), &rights);
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
        (!astra_u32_is_power_of_two(alignment) ||
         alignment < limits->page_size))
        return KERNEL_ELF_BAD_ALIGNMENT;

    if (!astra_u32_add_checked(memory_size, page_mask, &span))
        return KERNEL_ELF_BAD_RANGE;
    span &= ~page_mask;
    if (!astra_u32_add_checked(virtual_address, span - 1u, &last))
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

static KernelElfStatus stream_fail(KernelElfStream *stream,
                                   KernelElfStatus status)
{
    kernel_bytes_clear(&stream->plan, sizeof(stream->plan));
    stream->failed = 1u;
    return status;
}

static KernelElfStatus stream_begin(const void *header, uint32_t image_size,
                                    const KernelElfLimits *limits,
                                    uint16_t expected_type, bool library,
                                    KernelElfStream *stream)
{
    const uint8_t *bytes = header;
    uint32_t table_bytes;
    KernelElfStatus status;

    if (header == NULL || limits == NULL || stream == NULL ||
        !astra_u32_is_power_of_two(limits->page_size) ||
        limits->maximum_pages == 0u ||
        limits->minimum_address > limits->maximum_address)
        return KERNEL_ELF_INVALID_ARGUMENT;
    kernel_bytes_clear(stream, sizeof(*stream));
    if (image_size < KERNEL_ELF_HEADER_SIZE)
        return stream_fail(stream, KERNEL_ELF_TRUNCATED);
    status = check_identity(bytes);
    if (status != KERNEL_ELF_OK)
        return stream_fail(stream, status);
    if (astra_load_be16(bytes + 16) != expected_type)
        return stream_fail(stream, KERNEL_ELF_BAD_TYPE);
    if (astra_load_be16(bytes + 18) != ELF_MACHINE_68K)
        return stream_fail(stream, KERNEL_ELF_BAD_MACHINE);
    if (astra_load_be32(bytes + 20) != ELF_VERSION_CURRENT)
        return stream_fail(stream, KERNEL_ELF_BAD_VERSION);
    if (astra_load_be32(bytes + 36) != 0u)
        return stream_fail(stream, KERNEL_ELF_BAD_FLAGS);
    if (astra_load_be16(bytes + 40) != KERNEL_ELF_HEADER_SIZE ||
        astra_load_be16(bytes + 42) != KERNEL_ELF_PHENTSIZE)
        return stream_fail(stream, KERNEL_ELF_BAD_HEADER_TABLE);

    stream->header_count = astra_load_be16(bytes + 44);
    if (stream->header_count == 0u)
        return stream_fail(stream, KERNEL_ELF_NO_SEGMENTS);
    if (stream->header_count > UINT32_MAX / KERNEL_ELF_PHENTSIZE)
        return stream_fail(stream, KERNEL_ELF_BAD_HEADER_TABLE);
    table_bytes = stream->header_count * KERNEL_ELF_PHENTSIZE;
    stream->header_offset = astra_load_be32(bytes + 28);
    if (!range_within(stream->header_offset, table_bytes, image_size))
        return stream_fail(stream, KERNEL_ELF_BAD_HEADER_TABLE);

    kernel_bytes_copy(&stream->limits, limits, sizeof(stream->limits));
    stream->image_size = image_size;
    stream->entry = astra_load_be32(bytes + 24);
    stream->library = library ? 1u : 0u;
    return KERNEL_ELF_OK;
}

KernelElfStatus kernel_elf_stream_begin(const void *header,
                                        uint32_t image_size,
                                        const KernelElfLimits *limits,
                                        KernelElfStream *stream)
{
    return stream_begin(header, image_size, limits, ELF_TYPE_EXEC, false,
                        stream);
}

KernelElfStatus kernel_elf_stream_next_header(const KernelElfStream *stream,
                                              uint32_t *offset,
                                              uint32_t *length)
{
    if (stream == NULL || offset == NULL || length == NULL ||
        stream->failed != 0u)
        return KERNEL_ELF_INVALID_ARGUMENT;
    *offset = 0u;
    *length = 0u;
    if (stream->header_index == stream->header_count)
        return KERNEL_ELF_OK;
    if (stream->header_index > stream->header_count)
        return KERNEL_ELF_INVALID_ARGUMENT;
    *offset = stream->header_offset +
              stream->header_index * KERNEL_ELF_PHENTSIZE;
    *length = KERNEL_ELF_PHENTSIZE;
    return KERNEL_ELF_OK;
}

KernelElfStatus kernel_elf_stream_add_header(KernelElfStream *stream,
                                             const void *header)
{
    const uint8_t *bytes = header;
    uint32_t type;
    KernelElfSegment segment;
    KernelElfStatus status;

    if (stream == NULL || header == NULL || stream->failed != 0u ||
        stream->complete != 0u ||
        stream->header_index >= stream->header_count)
        return KERNEL_ELF_INVALID_ARGUMENT;
    type = astra_load_be32(bytes);
    if (type == ELF_PT_GNU_STACK) {
        if ((astra_load_be32(bytes + 24) & ELF_PF_X) != 0u)
            return stream_fail(stream, KERNEL_ELF_EXECUTABLE_STACK);
    } else if (type != ELF_PT_LOAD) {
        if (!ignorable_segment(type, stream->library != 0u))
            return stream_fail(stream, KERNEL_ELF_UNSUPPORTED_SEGMENT);
    } else if (astra_load_be32(bytes + 20) == 0u) {
        if (astra_load_be32(bytes + 16) != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_RANGE);
    } else {
        if (stream->plan.segment_count == KERNEL_ELF_SEGMENT_MAX)
            return stream_fail(stream, KERNEL_ELF_TOO_MANY_SEGMENTS);
        status = accept_load_segment(bytes, &stream->limits,
                                     stream->image_size, &segment);
        if (status != KERNEL_ELF_OK)
            return stream_fail(stream, status);
        if (stream->plan.segment_count != 0u) {
            const KernelElfSegment *previous =
                &stream->plan.segment[stream->plan.segment_count - 1u];
            uint32_t previous_end = previous->virtual_address +
                previous->page_count * stream->limits.page_size;

            if (segment.virtual_address < previous->virtual_address)
                return stream_fail(stream, KERNEL_ELF_UNORDERED);
            if (segment.virtual_address < previous_end)
                return stream_fail(stream, KERNEL_ELF_OVERLAP);
        }
        if (!astra_u32_add_checked(stream->total_pages, segment.page_count,
                                   &stream->total_pages) ||
            stream->total_pages > stream->limits.maximum_pages)
            return stream_fail(stream, KERNEL_ELF_TOO_LARGE);
        stream->plan.segment[stream->plan.segment_count] = segment;
        ++stream->plan.segment_count;
    }
    ++stream->header_index;
    return KERNEL_ELF_OK;
}

KernelElfStatus kernel_elf_stream_finish(KernelElfStream *stream,
                                         KernelElfImage *plan)
{
    uint32_t index;

    if (plan != NULL)
        kernel_bytes_clear(plan, sizeof(*plan));
    if (stream == NULL || plan == NULL || stream->failed != 0u ||
        stream->complete != 0u)
        return KERNEL_ELF_INVALID_ARGUMENT;
    if (stream->header_index != stream->header_count)
        return KERNEL_ELF_TRUNCATED;
    if (stream->plan.segment_count == 0u)
        return stream_fail(stream, KERNEL_ELF_NO_SEGMENTS);
    if (stream->library != 0u) {
        if (stream->entry != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_ENTRY);
    } else {
        if ((stream->entry & 1u) != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_ENTRY);
        for (index = 0u; index < stream->plan.segment_count; ++index) {
            const KernelElfSegment *segment = &stream->plan.segment[index];

            if ((segment->rights & KERNEL_ELF_SEGMENT_EXEC) != 0u &&
                stream->entry >= segment->virtual_address &&
                stream->entry < segment->virtual_address +
                                    segment->memory_size)
                break;
        }
        if (index == stream->plan.segment_count)
            return stream_fail(stream, KERNEL_ELF_BAD_ENTRY);
        stream->plan.entry = stream->entry;
    }
    stream->plan.total_pages = stream->total_pages;
    kernel_bytes_copy(plan, &stream->plan, sizeof(*plan));
    stream->complete = 1u;
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
    KernelElfStream stream;
    uint32_t offset;
    uint32_t length;
    KernelElfStatus status;

    if (plan != NULL)
        kernel_bytes_clear(plan, sizeof(*plan));
    if (image == NULL || limits == NULL || plan == NULL)
        return KERNEL_ELF_INVALID_ARGUMENT;
    if (readable < KERNEL_ELF_HEADER_SIZE)
        return KERNEL_ELF_TRUNCATED;
    status = stream_begin(image, image_size, limits, expected_type, library,
                          &stream);
    while (status == KERNEL_ELF_OK) {
        status = kernel_elf_stream_next_header(&stream, &offset, &length);
        if (status != KERNEL_ELF_OK || length == 0u)
            break;
        if (!range_within(offset, length, readable))
            return KERNEL_ELF_BAD_HEADER_TABLE;
        status = kernel_elf_stream_add_header(&stream, bytes + offset);
    }
    return status == KERNEL_ELF_OK ? kernel_elf_stream_finish(&stream, plan) :
                                     status;
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
