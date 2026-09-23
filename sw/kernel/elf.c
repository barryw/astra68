#include "elf.h"

#include "bytes.h"

#include <astra/endian.h>
#include <astra/integer.h>
#include <astra/limits.h>

#if defined(__m68k__)
#include "allocation.h"
#include "memory.h"
#else
#include <stdlib.h>
#endif

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

struct KernelElfSegmentBlock {
    struct KernelElfSegmentBlock *next;
    uint32_t physical;
    uint16_t count;
    uint16_t reserved;
    KernelElfSegment segment[
        (ASTRA_MEMORY_PAGE_SIZE - sizeof(void *) - sizeof(uint32_t) -
         (2u * sizeof(uint16_t))) / sizeof(KernelElfSegment)];
};

_Static_assert(sizeof(KernelElfSegmentBlock) <= ASTRA_MEMORY_PAGE_SIZE,
               "ELF segment metadata must fit one page");

#if defined(KERNEL_ELF_HOST_TEST)
static uint32_t segment_allocation_successes_before_failure = UINT32_MAX;

void kernel_elf_test_fail_segment_allocation_after(uint32_t successes)
{
    segment_allocation_successes_before_failure = successes;
}

void kernel_elf_test_clear_segment_allocation_failure(void)
{
    segment_allocation_successes_before_failure = UINT32_MAX;
}
#endif

static KernelElfSegmentBlock *segment_block_allocate(void)
{
#if defined(KERNEL_ELF_HOST_TEST)
    if (segment_allocation_successes_before_failure == 0u)
        return NULL;
    if (segment_allocation_successes_before_failure != UINT32_MAX)
        --segment_allocation_successes_before_failure;
#endif
#if defined(__m68k__)
    uint32_t physical;
    KernelElfSegmentBlock *block;

    if (kernel_memory_alloc_zeroed_tagged(
            KERNEL_ALLOCATION_SITE_ELF_METADATA, 1u, 1u,
            KERNEL_FRAME_KERNEL, KERNEL_OWNER_CORE, &physical) !=
        KERNEL_MEMORY_OK)
        return NULL;
    block = kernel_memory_access(physical, ASTRA_MEMORY_PAGE_SIZE);
    if (block == NULL) {
        (void)kernel_memory_release(physical, 1u, KERNEL_OWNER_CORE);
        return NULL;
    }
    block->physical = physical;
    return block;
#else
    return calloc(1u, sizeof(KernelElfSegmentBlock));
#endif
}

static bool segment_block_release(KernelElfSegmentBlock *block)
{
#if defined(__m68k__)
    return block != NULL && block->physical != 0u &&
           kernel_memory_release(block->physical, 1u, KERNEL_OWNER_CORE) ==
               KERNEL_MEMORY_OK;
#else
    free(block);
    return true;
#endif
}

const KernelElfSegment *kernel_elf_image_segment(
    const KernelElfImage *plan, uint32_t index)
{
    KernelElfSegmentBlock *block;

    if (plan == NULL || index >= plan->segment_count)
        return NULL;
    if (index < KERNEL_ELF_SEGMENT_INLINE)
        return &plan->segment[index];
    index -= KERNEL_ELF_SEGMENT_INLINE;
    for (block = plan->segment_blocks; block != NULL; block = block->next) {
        if (index < block->count)
            return &block->segment[index];
        index -= block->count;
    }
    return NULL;
}

static KernelElfSegment *append_segment(KernelElfImage *plan)
{
    KernelElfSegmentBlock *block;

    if (plan->segment_count < KERNEL_ELF_SEGMENT_INLINE)
        return &plan->segment[plan->segment_count];
    block = plan->segment_blocks_tail;
    if (block == NULL ||
        block->count == sizeof(block->segment) / sizeof(block->segment[0])) {
        KernelElfSegmentBlock *created = segment_block_allocate();

        if (created == NULL)
            return NULL;
        if (block == NULL)
            plan->segment_blocks = created;
        else
            block->next = created;
        plan->segment_blocks_tail = created;
        block = created;
    }
    return &block->segment[block->count++];
}

bool kernel_elf_image_discard(KernelElfImage *plan)
{
    KernelElfSegmentBlock *block;
    bool released = true;

    if (plan == NULL)
        return false;
    block = plan->segment_blocks;
    if (plan->owns_segment_blocks != 0u) {
        while (block != NULL) {
            KernelElfSegmentBlock *next = block->next;

            if (!segment_block_release(block)) {
                released = false;
                break;
            }
            block = next;
        }
    }
    kernel_bytes_clear(plan, sizeof(*plan));
    return released;
}

void kernel_elf_image_move(KernelElfImage *destination,
                           KernelElfImage *source)
{
    if (destination == NULL || source == NULL || destination == source)
        return;
    kernel_bytes_copy(destination, source, sizeof(*destination));
    source->segment_blocks = NULL;
    source->segment_blocks_tail = NULL;
    source->segment_count = 0u;
    source->owns_segment_blocks = 0u;
}

bool kernel_elf_image_equal(const KernelElfImage *left,
                            const KernelElfImage *right)
{
    if (left == NULL || right == NULL ||
        left->segment_count != right->segment_count ||
        left->entry != right->entry ||
        left->total_pages != right->total_pages ||
        left->writable_bytes != right->writable_bytes ||
        left->has_tls != right->has_tls ||
        left->has_dynamic != right->has_dynamic ||
        left->has_interpreter != right->has_interpreter ||
        left->has_relro != right->has_relro ||
        !kernel_bytes_equal(&left->tls, &right->tls, sizeof(left->tls)) ||
        !kernel_bytes_equal(&left->dynamic, &right->dynamic,
                            sizeof(left->dynamic)) ||
        !kernel_bytes_equal(&left->interpreter, &right->interpreter,
                            sizeof(left->interpreter)) ||
        !kernel_bytes_equal(&left->relro, &right->relro,
                            sizeof(left->relro)))
        return false;
    for (uint32_t index = 0u; index < left->segment_count; ++index) {
        const KernelElfSegment *left_segment =
            kernel_elf_image_segment(left, index);
        const KernelElfSegment *right_segment =
            kernel_elf_image_segment(right, index);

        if (left_segment == NULL || right_segment == NULL ||
            !kernel_bytes_equal(left_segment, right_segment,
                                sizeof(*left_segment)))
            return false;
    }
    return true;
}

bool kernel_elf_stream_discard(KernelElfStream *stream)
{
    return stream != NULL && kernel_elf_image_discard(&stream->plan);
}

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
static bool ignorable_segment(uint32_t type)
{
    return type == ELF_PT_NULL || type == ELF_PT_NOTE ||
           type == ELF_PT_PHDR || type == ELF_PT_GNU_EH_FRAME ||
           type == ELF_PT_GNU_PROPERTY;
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

static KernelElfStatus accept_tls_segment(const uint8_t *header,
                                          uint32_t image_size,
                                          KernelElfTls *tls)
{
    uint32_t flags = astra_load_be32(header + 24);
    uint32_t alignment = astra_load_be32(header + 28);

    tls->file_offset = astra_load_be32(header + 4);
    tls->virtual_address = astra_load_be32(header + 8);
    tls->file_size = astra_load_be32(header + 16);
    tls->memory_size = astra_load_be32(header + 20);
    tls->alignment = alignment <= 1u ? 1u : alignment;
    if (flags != ELF_PF_R || tls->file_size > tls->memory_size ||
        !range_within(tls->file_offset, tls->file_size, image_size) ||
        (alignment > 1u && !astra_u32_is_power_of_two(alignment)) ||
        (tls->file_offset & (tls->alignment - 1u)) !=
            (tls->virtual_address & (tls->alignment - 1u)))
        return KERNEL_ELF_BAD_TLS;
    return KERNEL_ELF_OK;
}

static bool tls_covered_by_load(const KernelElfImage *plan)
{
    uint32_t tls_file_end;
    uint32_t tls_memory_end;

    if (!astra_u32_add_checked(plan->tls.virtual_address,
                               plan->tls.memory_size, &tls_memory_end) ||
        tls_memory_end <= plan->tls.virtual_address)
        return false;
    if (plan->tls.file_size == 0u)
        return true;
    if (!astra_u32_add_checked(plan->tls.file_offset, plan->tls.file_size,
                               &tls_file_end))
        return false;
    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment =
            kernel_elf_image_segment(plan, index);
        uint32_t segment_file_end;

        if (segment == NULL ||
            (segment->rights & KERNEL_ELF_SEGMENT_WRITE) != 0u ||
            !astra_u32_add_checked(segment->file_offset, segment->file_size,
                                   &segment_file_end) ||
            plan->tls.file_offset < segment->file_offset ||
            tls_file_end > segment_file_end ||
            plan->tls.virtual_address < segment->virtual_address)
            continue;
        if (plan->tls.file_offset - segment->file_offset ==
            plan->tls.virtual_address - segment->virtual_address)
            return true;
    }
    return false;
}

static KernelElfStatus accept_dynamic_segment(
    const uint8_t *header, uint32_t image_size, KernelElfDynamic *dynamic)
{
    uint32_t file_size = astra_load_be32(header + 16);
    uint32_t memory_size = astra_load_be32(header + 20);

    dynamic->file_offset = astra_load_be32(header + 4);
    dynamic->virtual_address = astra_load_be32(header + 8);
    dynamic->size = file_size;
    if (astra_load_be32(header + 24) != (ELF_PF_R | ELF_PF_W) ||
        file_size < 8u || file_size != memory_size ||
        (file_size & 7u) != 0u ||
        !range_within(dynamic->file_offset, file_size, image_size))
        return KERNEL_ELF_BAD_DYNAMIC;
    return KERNEL_ELF_OK;
}

static bool dynamic_covered_by_load(const KernelElfImage *plan)
{
    uint32_t file_end;
    uint32_t memory_end;

    if (!astra_u32_add_checked(plan->dynamic.file_offset,
                               plan->dynamic.size, &file_end) ||
        !astra_u32_add_checked(plan->dynamic.virtual_address,
                               plan->dynamic.size, &memory_end))
        return false;
    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment =
            kernel_elf_image_segment(plan, index);
        uint32_t segment_file_end;
        uint32_t segment_memory_end;

        if (segment == NULL ||
            (segment->rights & KERNEL_ELF_SEGMENT_WRITE) == 0u ||
            !astra_u32_add_checked(segment->file_offset, segment->file_size,
                                   &segment_file_end) ||
            !astra_u32_add_checked(segment->virtual_address,
                                   segment->memory_size,
                                   &segment_memory_end))
            continue;
        if (plan->dynamic.file_offset >= segment->file_offset &&
            file_end <= segment_file_end &&
            plan->dynamic.virtual_address >= segment->virtual_address &&
            memory_end <= segment_memory_end &&
            plan->dynamic.file_offset - segment->file_offset ==
                plan->dynamic.virtual_address - segment->virtual_address)
            return true;
    }
    return false;
}

static KernelElfStatus accept_interpreter_segment(
    const uint8_t *header, uint32_t image_size,
    KernelElfInterpreter *interpreter)
{
    uint32_t file_size = astra_load_be32(header + 16);
    uint32_t memory_size = astra_load_be32(header + 20);

    interpreter->file_offset = astra_load_be32(header + 4);
    interpreter->virtual_address = astra_load_be32(header + 8);
    interpreter->size = file_size;
    if (astra_load_be32(header + 24) != ELF_PF_R || file_size < 2u ||
        file_size != memory_size ||
        !range_within(interpreter->file_offset, file_size, image_size))
        return KERNEL_ELF_BAD_INTERPRETER;
    return KERNEL_ELF_OK;
}

static bool interpreter_covered_by_load(const KernelElfImage *plan)
{
    uint32_t file_end;
    uint32_t memory_end;

    if (!astra_u32_add_checked(plan->interpreter.file_offset,
                               plan->interpreter.size, &file_end) ||
        !astra_u32_add_checked(plan->interpreter.virtual_address,
                               plan->interpreter.size, &memory_end))
        return false;
    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment =
            kernel_elf_image_segment(plan, index);
        uint32_t segment_file_end;
        uint32_t segment_memory_end;

        if (segment == NULL ||
            (segment->rights & KERNEL_ELF_SEGMENT_WRITE) != 0u ||
            !astra_u32_add_checked(segment->file_offset, segment->file_size,
                                   &segment_file_end) ||
            !astra_u32_add_checked(segment->virtual_address,
                                   segment->memory_size,
                                   &segment_memory_end))
            continue;
        if (plan->interpreter.file_offset >= segment->file_offset &&
            file_end <= segment_file_end &&
            plan->interpreter.virtual_address >= segment->virtual_address &&
            memory_end <= segment_memory_end &&
            plan->interpreter.file_offset - segment->file_offset ==
                plan->interpreter.virtual_address - segment->virtual_address)
            return true;
    }
    return false;
}

static KernelElfStatus accept_relro_segment(
    const uint8_t *header, uint32_t image_size, KernelElfRelro *relro)
{
    relro->file_offset = astra_load_be32(header + 4);
    relro->virtual_address = astra_load_be32(header + 8);
    relro->file_size = astra_load_be32(header + 16);
    relro->memory_size = astra_load_be32(header + 20);
    if (astra_load_be32(header + 24) != ELF_PF_R ||
        relro->memory_size == 0u ||
        relro->file_size > relro->memory_size ||
        !range_within(relro->file_offset, relro->file_size, image_size))
        return KERNEL_ELF_BAD_RELRO;
    return KERNEL_ELF_OK;
}

static bool relro_covered_by_load(const KernelElfImage *plan,
                                  uint32_t page_size)
{
    uint32_t file_end;
    uint32_t memory_end;

    if (!astra_u32_add_checked(plan->relro.file_offset,
                               plan->relro.file_size, &file_end) ||
        !astra_u32_add_checked(plan->relro.virtual_address,
                               plan->relro.memory_size, &memory_end))
        return false;
    for (uint32_t index = 0u; index < plan->segment_count; ++index) {
        const KernelElfSegment *segment =
            kernel_elf_image_segment(plan, index);
        uint32_t segment_file_end;
        uint32_t segment_mapping_size;
        uint32_t segment_mapping_end;
        uint32_t segment_memory_end;

        if (segment == NULL ||
            (segment->rights & KERNEL_ELF_SEGMENT_WRITE) == 0u ||
            !astra_u32_add_checked(segment->file_offset,
                                   segment->file_size,
                                   &segment_file_end) ||
            segment->page_count > UINT32_MAX / page_size)
            continue;
        segment_mapping_size = segment->page_count * page_size;
        if (!astra_u32_add_checked(segment->virtual_address,
                                   segment_mapping_size,
                                   &segment_mapping_end) ||
            !astra_u32_add_checked(segment->virtual_address,
                                   segment->memory_size,
                                   &segment_memory_end) ||
            plan->relro.virtual_address < segment->virtual_address ||
            segment_memory_end > segment_mapping_end ||
            memory_end > segment_mapping_end)
            continue;
        if (plan->relro.file_size == 0u)
            return true;
        if (plan->relro.file_offset >= segment->file_offset &&
            file_end <= segment_file_end &&
            plan->relro.file_offset - segment->file_offset ==
                plan->relro.virtual_address - segment->virtual_address)
            return true;
    }
    return false;
}

static KernelElfStatus stream_fail(KernelElfStream *stream,
                                   KernelElfStatus status)
{
    kernel_elf_image_discard(&stream->plan);
    stream->failed = 1u;
    return status;
}

static KernelElfStatus stream_begin(const void *header, uint32_t image_size,
                                    const KernelElfLimits *limits,
                                    uint16_t expected_type,
                                    KernelElfRole role,
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
    stream->plan.owns_segment_blocks = 1u;
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
    stream->role = (uint8_t)role;
    return KERNEL_ELF_OK;
}

KernelElfStatus kernel_elf_stream_begin(const void *header,
                                        uint32_t image_size,
                                        const KernelElfLimits *limits,
                                        KernelElfStream *stream)
{
    return stream_begin(header, image_size, limits, ELF_TYPE_EXEC,
                        KERNEL_ELF_EXECUTABLE, stream);
}

KernelElfStatus kernel_elf_stream_begin_interpreter(
    const void *header, uint32_t image_size, const KernelElfLimits *limits,
    KernelElfStream *stream)
{
    return stream_begin(header, image_size, limits, ELF_TYPE_DYN,
                        KERNEL_ELF_INTERPRETER, stream);
}

KernelElfStatus kernel_elf_stream_begin_library(
    const void *header, uint32_t image_size, const KernelElfLimits *limits,
    KernelElfStream *stream)
{
    return stream_begin(header, image_size, limits, ELF_TYPE_DYN,
                        KERNEL_ELF_SHARED_LIBRARY, stream);
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
    } else if (type == ELF_PT_TLS) {
        if (stream->tls_seen != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_TLS);
        status = accept_tls_segment(bytes, stream->image_size,
                                    &stream->plan.tls);
        if (status != KERNEL_ELF_OK)
            return stream_fail(stream, status);
        stream->tls_seen = 1u;
        if (stream->plan.tls.memory_size != 0u)
            stream->plan.has_tls = 1u;
    } else if (type == ELF_PT_DYNAMIC) {
        if (stream->dynamic_seen != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_DYNAMIC);
        status = accept_dynamic_segment(bytes, stream->image_size,
                                        &stream->plan.dynamic);
        if (status != KERNEL_ELF_OK)
            return stream_fail(stream, status);
        stream->dynamic_seen = 1u;
        stream->plan.has_dynamic = 1u;
    } else if (type == ELF_PT_INTERP) {
        if (stream->role != KERNEL_ELF_EXECUTABLE ||
            stream->interpreter_seen != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_INTERPRETER);
        status = accept_interpreter_segment(bytes, stream->image_size,
                                            &stream->plan.interpreter);
        if (status != KERNEL_ELF_OK)
            return stream_fail(stream, status);
        stream->interpreter_seen = 1u;
        stream->plan.has_interpreter = 1u;
    } else if (type == ELF_PT_GNU_RELRO) {
        if (stream->relro_seen != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_RELRO);
        status = accept_relro_segment(bytes, stream->image_size,
                                      &stream->plan.relro);
        if (status != KERNEL_ELF_OK)
            return stream_fail(stream, status);
        stream->relro_seen = 1u;
        stream->plan.has_relro = 1u;
    } else if (type != ELF_PT_LOAD) {
        if (!ignorable_segment(type))
            return stream_fail(stream, KERNEL_ELF_UNSUPPORTED_SEGMENT);
    } else if (astra_load_be32(bytes + 20) == 0u) {
        if (astra_load_be32(bytes + 16) != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_RANGE);
    } else {
        status = accept_load_segment(bytes, &stream->limits,
                                     stream->image_size, &segment);
        if (status != KERNEL_ELF_OK)
            return stream_fail(stream, status);
        if (stream->plan.segment_count != 0u) {
            const KernelElfSegment *previous =
                kernel_elf_image_segment(
                    &stream->plan, stream->plan.segment_count - 1u);
            uint32_t previous_end;

            if (previous == NULL)
                return stream_fail(stream, KERNEL_ELF_INVALID_ARGUMENT);
            previous_end = previous->virtual_address +
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
        if ((segment.rights & KERNEL_ELF_SEGMENT_WRITE) != 0u &&
            !astra_u32_add_checked(stream->plan.writable_bytes,
                                   segment.memory_size,
                                   &stream->plan.writable_bytes))
            return stream_fail(stream, KERNEL_ELF_TOO_LARGE);
        KernelElfSegment *published = append_segment(&stream->plan);

        if (published == NULL)
            return stream_fail(stream, KERNEL_ELF_OUT_OF_MEMORY);
        *published = segment;
        ++stream->plan.segment_count;
    }
    ++stream->header_index;
    return KERNEL_ELF_OK;
}

static bool entry_inside_executable_segment(const KernelElfStream *stream)
{
    if ((stream->entry & 1u) != 0u)
        return false;
    for (uint32_t index = 0u; index < stream->plan.segment_count; ++index) {
        const KernelElfSegment *segment =
            kernel_elf_image_segment(&stream->plan, index);

        if (segment != NULL &&
            (segment->rights & KERNEL_ELF_SEGMENT_EXEC) != 0u &&
            stream->entry >= segment->virtual_address &&
            stream->entry < segment->virtual_address + segment->memory_size)
            return true;
    }
    return false;
}

KernelElfStatus kernel_elf_stream_finish(KernelElfStream *stream,
                                         KernelElfImage *plan)
{

    if (plan != NULL)
        kernel_bytes_clear(plan, sizeof(*plan));
    if (stream == NULL || plan == NULL || stream->failed != 0u ||
        stream->complete != 0u)
        return KERNEL_ELF_INVALID_ARGUMENT;
    if (stream->header_index != stream->header_count)
        return KERNEL_ELF_TRUNCATED;
    if (stream->plan.segment_count == 0u)
        return stream_fail(stream, KERNEL_ELF_NO_SEGMENTS);
    if (stream->plan.has_tls != 0u &&
        !tls_covered_by_load(&stream->plan))
        return stream_fail(stream, KERNEL_ELF_BAD_TLS);
    if (stream->plan.has_dynamic != 0u &&
        !dynamic_covered_by_load(&stream->plan))
        return stream_fail(stream, KERNEL_ELF_BAD_DYNAMIC);
    if (stream->plan.has_interpreter != 0u &&
        !interpreter_covered_by_load(&stream->plan))
        return stream_fail(stream, KERNEL_ELF_BAD_INTERPRETER);
    if (stream->plan.has_relro != 0u &&
        !relro_covered_by_load(&stream->plan, stream->limits.page_size))
        return stream_fail(stream, KERNEL_ELF_BAD_RELRO);
    if (stream->role == KERNEL_ELF_SHARED_LIBRARY) {
        if (stream->plan.has_interpreter != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_INTERPRETER);
        if (stream->plan.has_dynamic == 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_DYNAMIC);
        if (stream->entry != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_ENTRY);
    } else if (stream->role == KERNEL_ELF_INTERPRETER) {
        if (stream->plan.has_interpreter != 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_INTERPRETER);
        if (stream->plan.has_dynamic == 0u)
            return stream_fail(stream, KERNEL_ELF_BAD_DYNAMIC);
        if (stream->entry == 0u ||
            !entry_inside_executable_segment(stream))
            return stream_fail(stream, KERNEL_ELF_BAD_ENTRY);
        stream->plan.entry = stream->entry;
    } else {
        if (stream->plan.has_dynamic != stream->plan.has_interpreter)
            return stream_fail(stream, KERNEL_ELF_BAD_INTERPRETER);
        if (!entry_inside_executable_segment(stream))
            return stream_fail(stream, KERNEL_ELF_BAD_ENTRY);
        stream->plan.entry = stream->entry;
    }
    stream->plan.total_pages = stream->total_pages;
    kernel_bytes_copy(plan, &stream->plan, sizeof(*plan));
    plan->owns_segment_blocks = 0u;
    stream->complete = 1u;
    return KERNEL_ELF_OK;
}

static KernelElfStatus accept_windowed(const void *image,
                                       uint32_t image_size,
                                       uint32_t readable,
                                       const KernelElfLimits *limits,
                                       uint16_t expected_type,
                                       KernelElfRole role,
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
    status = stream_begin(image, image_size, limits, expected_type, role,
                          &stream);
    while (status == KERNEL_ELF_OK) {
        status = kernel_elf_stream_next_header(&stream, &offset, &length);
        if (status != KERNEL_ELF_OK || length == 0u)
            break;
        if (!range_within(offset, length, readable)) {
            kernel_elf_stream_discard(&stream);
            return KERNEL_ELF_BAD_HEADER_TABLE;
        }
        status = kernel_elf_stream_add_header(&stream, bytes + offset);
    }
    if (status == KERNEL_ELF_OK)
        status = kernel_elf_stream_finish(&stream, plan);
    if (status == KERNEL_ELF_OK)
        kernel_elf_image_move(plan, &stream.plan);
    else
        kernel_elf_stream_discard(&stream);
    return status;
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
    case KERNEL_ELF_OUT_OF_MEMORY: return "out of memory";
    case KERNEL_ELF_UNSUPPORTED_SEGMENT: return "unsupported segment type";
    case KERNEL_ELF_EXECUTABLE_STACK: return "executable stack";
    case KERNEL_ELF_BAD_PERMISSIONS: return "bad segment permissions";
    case KERNEL_ELF_BAD_ALIGNMENT: return "segment not page aligned";
    case KERNEL_ELF_BAD_RANGE: return "segment outside the permitted range";
    case KERNEL_ELF_UNORDERED: return "segments out of order";
    case KERNEL_ELF_OVERLAP: return "segments overlap";
    case KERNEL_ELF_TOO_LARGE: return "image needs too many pages";
    case KERNEL_ELF_BAD_TLS: return "invalid TLS template";
    case KERNEL_ELF_BAD_DYNAMIC: return "invalid dynamic table";
    case KERNEL_ELF_BAD_INTERPRETER: return "invalid dynamic loader identity";
    case KERNEL_ELF_BAD_ENTRY: return "entry point outside executable code";
    case KERNEL_ELF_BAD_RELRO: return "invalid GNU RELRO range";
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
                           ELF_TYPE_EXEC, KERNEL_ELF_EXECUTABLE, plan);
}

KernelElfStatus kernel_elf_accept_library_windowed(
    const void *image, uint32_t image_size, uint32_t readable,
    const KernelElfLimits *limits, KernelElfImage *plan)
{
    return accept_windowed(image, image_size, readable, limits,
                           ELF_TYPE_DYN, KERNEL_ELF_SHARED_LIBRARY, plan);
}

KernelElfStatus kernel_elf_accept_library(const void *image,
                                          uint32_t image_size,
                                          const KernelElfLimits *limits,
                                          KernelElfImage *plan)
{
    return kernel_elf_accept_library_windowed(image, image_size, image_size,
                                              limits, plan);
}

KernelElfStatus kernel_elf_accept_interpreter_windowed(
    const void *image, uint32_t image_size, uint32_t readable,
    const KernelElfLimits *limits, KernelElfImage *plan)
{
    return accept_windowed(image, image_size, readable, limits,
                           ELF_TYPE_DYN, KERNEL_ELF_INTERPRETER, plan);
}

KernelElfStatus kernel_elf_accept_interpreter(
    const void *image, uint32_t image_size, const KernelElfLimits *limits,
    KernelElfImage *plan)
{
    return kernel_elf_accept_interpreter_windowed(
        image, image_size, image_size, limits, plan);
}
