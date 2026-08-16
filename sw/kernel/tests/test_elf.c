#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf.h"
#include "memory.h"
#include "process.h"
#include "vm.h"

#define PAGE 4096u
#define IMAGE_CAPACITY (16u * PAGE)

#define TEXT_VADDR 0x00100000u
#define DATA_VADDR 0x00102000u

static uint8_t image[IMAGE_CAPACITY];
static uint32_t image_size;

static const KernelElfLimits limits = {
    .minimum_address = 0x00010000u,
    .maximum_address = 0x7fffffffu,
    .maximum_pages = 64u,
    .page_size = PAGE,
};

static const KernelElfLimits library_limits = {
    .minimum_address = 0u,
    .maximum_address = 0x00ffffffu,
    .maximum_pages = 64u,
    .page_size = PAGE,
};

static void put16(uint32_t offset, uint16_t value)
{
    image[offset] = (uint8_t)(value >> 8);
    image[offset + 1u] = (uint8_t)value;
}

static void put32(uint32_t offset, uint32_t value)
{
    image[offset] = (uint8_t)(value >> 24);
    image[offset + 1u] = (uint8_t)(value >> 16);
    image[offset + 2u] = (uint8_t)(value >> 8);
    image[offset + 3u] = (uint8_t)value;
}

/*
 * A two-segment executable in the shape Astra's link script produces: one page
 * of read-execute text at file offset 0, one page of read-write data at file
 * offset 4096 whose memory size exceeds its file size so it carries BSS.
 */
#define PHOFF 52u
#define PHNUM 3u

static void build_valid(void)
{
    memset(image, 0, sizeof(image));
    image_size = 3u * PAGE;

    image[0] = 0x7f;
    image[1] = 'E';
    image[2] = 'L';
    image[3] = 'F';
    image[4] = 1u; /* ELFCLASS32 */
    image[5] = 2u; /* ELFDATA2MSB */
    image[6] = 1u; /* EV_CURRENT */
    image[7] = 0u; /* ELFOSABI_SYSV */
    image[8] = 0u; /* ABI version */

    put16(16u, 2u);  /* ET_EXEC */
    put16(18u, 4u);  /* EM_68K */
    put32(20u, 1u);  /* e_version */
    put32(24u, TEXT_VADDR + 0x10u); /* e_entry */
    put32(28u, PHOFF);
    put32(32u, 0u);  /* e_shoff; sections are never read */
    put32(36u, 0u);  /* e_flags */
    put16(40u, 52u); /* e_ehsize */
    put16(42u, 32u); /* e_phentsize */
    put16(44u, PHNUM);
    put16(46u, 40u); /* e_shentsize */
    put16(48u, 0u);
    put16(50u, 0u);

    /* PT_LOAD text: one page, read + execute. */
    put32(PHOFF + 0u, 1u);
    put32(PHOFF + 4u, 0u);          /* p_offset */
    put32(PHOFF + 8u, TEXT_VADDR);  /* p_vaddr */
    put32(PHOFF + 12u, TEXT_VADDR); /* p_paddr */
    put32(PHOFF + 16u, PAGE);       /* p_filesz */
    put32(PHOFF + 20u, PAGE);       /* p_memsz */
    put32(PHOFF + 24u, 5u);         /* PF_R | PF_X */
    put32(PHOFF + 28u, PAGE);       /* p_align */

    /* PT_LOAD data: one file page, two memory pages, read + write. */
    put32(PHOFF + 32u + 0u, 1u);
    put32(PHOFF + 32u + 4u, PAGE);
    put32(PHOFF + 32u + 8u, DATA_VADDR);
    put32(PHOFF + 32u + 12u, DATA_VADDR);
    put32(PHOFF + 32u + 16u, 0x800u);
    put32(PHOFF + 32u + 20u, PAGE + 0x400u);
    put32(PHOFF + 32u + 24u, 6u); /* PF_R | PF_W */
    put32(PHOFF + 32u + 28u, PAGE);

    /* PT_GNU_STACK, non-executable. */
    put32(PHOFF + 64u + 0u, 0x6474e551u);
    put32(PHOFF + 64u + 24u, 6u); /* PF_R | PF_W */
}

static KernelElfStatus accept(void)
{
    KernelElfImage plan;

    return kernel_elf_accept(image, image_size, &limits, &plan);
}

static void expect(KernelElfStatus want, const char *what)
{
    KernelElfStatus got = accept();

    if (got != want) {
        printf("FAIL %s: wanted %s, got %s\n", what,
               kernel_elf_status_text(want), kernel_elf_status_text(got));
        abort();
    }
}

static void test_valid_plan(void)
{
    KernelElfImage plan;

    build_valid();
    assert(kernel_elf_accept(image, image_size, &limits, &plan) ==
           KERNEL_ELF_OK);
    assert(plan.segment_count == 2u);
    assert(plan.entry == TEXT_VADDR + 0x10u);
    assert(plan.total_pages == 3u);

    assert(plan.segment[0].virtual_address == TEXT_VADDR);
    assert(plan.segment[0].file_offset == 0u);
    assert(plan.segment[0].file_size == PAGE);
    assert(plan.segment[0].page_count == 1u);
    assert(plan.segment[0].rights ==
           (KERNEL_ELF_SEGMENT_READ | KERNEL_ELF_SEGMENT_EXEC));

    assert(plan.segment[1].virtual_address == DATA_VADDR);
    assert(plan.segment[1].file_size == 0x800u);
    assert(plan.segment[1].memory_size == PAGE + 0x400u);
    assert(plan.segment[1].page_count == 2u);
    assert(plan.segment[1].rights ==
           (KERNEL_ELF_SEGMENT_READ | KERNEL_ELF_SEGMENT_WRITE));
}

static void test_arguments(void)
{
    KernelElfImage plan;
    KernelElfLimits bad = limits;

    build_valid();
    assert(kernel_elf_accept(NULL, image_size, &limits, &plan) ==
           KERNEL_ELF_INVALID_ARGUMENT);
    assert(kernel_elf_accept(image, image_size, NULL, &plan) ==
           KERNEL_ELF_INVALID_ARGUMENT);
    assert(kernel_elf_accept(image, image_size, &limits, NULL) ==
           KERNEL_ELF_INVALID_ARGUMENT);

    bad.page_size = 3000u;
    assert(kernel_elf_accept(image, image_size, &bad, &plan) ==
           KERNEL_ELF_INVALID_ARGUMENT);
    bad = limits;
    bad.maximum_pages = 0u;
    assert(kernel_elf_accept(image, image_size, &bad, &plan) ==
           KERNEL_ELF_INVALID_ARGUMENT);
    bad = limits;
    bad.minimum_address = bad.maximum_address + 1u;
    assert(kernel_elf_accept(image, image_size, &bad, &plan) ==
           KERNEL_ELF_INVALID_ARGUMENT);
}

static void test_identity_rejections(void)
{
    uint32_t index;

    build_valid();
    image_size = KERNEL_ELF_HEADER_SIZE - 1u;
    expect(KERNEL_ELF_TRUNCATED, "short image");

    build_valid();
    image[1] = 'X';
    expect(KERNEL_ELF_BAD_MAGIC, "magic");

    build_valid();
    image[4] = 2u; /* ELFCLASS64 */
    expect(KERNEL_ELF_BAD_CLASS, "class");

    build_valid();
    image[5] = 1u; /* ELFDATA2LSB */
    expect(KERNEL_ELF_BAD_ENDIAN, "endianness");

    build_valid();
    image[6] = 0u;
    expect(KERNEL_ELF_BAD_VERSION, "identification version");

    build_valid();
    image[7] = 3u; /* ELFOSABI_LINUX */
    expect(KERNEL_ELF_BAD_ABI, "OS ABI");

    build_valid();
    image[8] = 1u;
    expect(KERNEL_ELF_BAD_ABI, "ABI version");

    /* Every reserved identification byte must be zero. */
    for (index = 9u; index < 16u; ++index) {
        build_valid();
        image[index] = 0xffu;
        expect(KERNEL_ELF_BAD_ABI, "reserved identification byte");
    }
}

static void test_header_rejections(void)
{
    build_valid();
    put16(16u, 3u); /* ET_DYN: no shared objects, no PIE */
    expect(KERNEL_ELF_BAD_TYPE, "ET_DYN");

    build_valid();
    put16(16u, 1u); /* ET_REL */
    expect(KERNEL_ELF_BAD_TYPE, "ET_REL");

    build_valid();
    put16(18u, 3u); /* EM_386 */
    expect(KERNEL_ELF_BAD_MACHINE, "machine");

    build_valid();
    put32(20u, 2u);
    expect(KERNEL_ELF_BAD_VERSION, "object version");

    build_valid();
    put32(36u, 1u);
    expect(KERNEL_ELF_BAD_FLAGS, "processor flags");

    build_valid();
    put16(40u, 64u);
    expect(KERNEL_ELF_BAD_HEADER_TABLE, "e_ehsize");

    build_valid();
    put16(42u, 56u);
    expect(KERNEL_ELF_BAD_HEADER_TABLE, "e_phentsize");

    build_valid();
    put16(44u, 0u);
    expect(KERNEL_ELF_NO_SEGMENTS, "zero program headers");

    build_valid();
    put32(28u, image_size - 16u);
    expect(KERNEL_ELF_BAD_HEADER_TABLE, "program header table past the image");

    build_valid();
    put32(28u, 0xfffffff0u);
    expect(KERNEL_ELF_BAD_HEADER_TABLE, "program header offset overflow");

    build_valid();
    put16(44u, 0xffffu);
    expect(KERNEL_ELF_BAD_HEADER_TABLE, "program header count overflow");
}

static void test_segment_rejections(void)
{
    build_valid();
    put32(PHOFF + 0u, 2u); /* PT_DYNAMIC */
    expect(KERNEL_ELF_UNSUPPORTED_SEGMENT, "PT_DYNAMIC");

    build_valid();
    put32(PHOFF + 0u, 3u); /* PT_INTERP */
    expect(KERNEL_ELF_UNSUPPORTED_SEGMENT, "PT_INTERP");

    build_valid();
    put32(PHOFF + 0u, 7u); /* PT_TLS */
    expect(KERNEL_ELF_UNSUPPORTED_SEGMENT, "PT_TLS");

    build_valid();
    put32(PHOFF + 0u, 0x70000000u);
    expect(KERNEL_ELF_UNSUPPORTED_SEGMENT, "unknown segment type");

    build_valid();
    put32(PHOFF + 64u + 24u, 7u); /* PF_R | PF_W | PF_X on GNU_STACK */
    expect(KERNEL_ELF_EXECUTABLE_STACK, "executable stack");

    build_valid();
    put32(PHOFF + 24u, 7u); /* writable and executable text */
    expect(KERNEL_ELF_BAD_PERMISSIONS, "write plus execute");

    build_valid();
    put32(PHOFF + 24u, 1u); /* execute only */
    expect(KERNEL_ELF_BAD_PERMISSIONS, "unreadable segment");

    build_valid();
    put32(PHOFF + 24u, 0x10u);
    expect(KERNEL_ELF_BAD_PERMISSIONS, "unknown permission bit");

    /* An empty segment carries nothing and is skipped, not rejected. */
    build_valid();
    put32(PHOFF + 16u, 0u);
    put32(PHOFF + 20u, 0u);
    put32(24u, DATA_VADDR); /* entry must move; the text segment is gone */
    put32(PHOFF + 32u + 24u, 5u); /* make the survivor executable */
    expect(KERNEL_ELF_OK, "empty segment skipped");

    /* An empty segment that still claims file content is malformed. */
    build_valid();
    put32(PHOFF + 20u, 0u);
    expect(KERNEL_ELF_BAD_RANGE, "empty segment with file content");

    build_valid();
    put32(PHOFF + 16u, PAGE * 2u); /* p_filesz beyond p_memsz */
    expect(KERNEL_ELF_BAD_RANGE, "file size beyond memory size");

    build_valid();
    put32(PHOFF + 4u, image_size);
    put32(PHOFF + 16u, PAGE);
    expect(KERNEL_ELF_BAD_RANGE, "segment contents past the image");

    build_valid();
    put32(PHOFF + 4u, 0xfffff000u);
    expect(KERNEL_ELF_BAD_RANGE, "file offset overflow");

    build_valid();
    put32(PHOFF + 8u, TEXT_VADDR + 1u);
    expect(KERNEL_ELF_BAD_ALIGNMENT, "unaligned virtual address");

    build_valid();
    put32(PHOFF + 4u, 0x10u);
    expect(KERNEL_ELF_BAD_ALIGNMENT, "unaligned file offset");

    build_valid();
    put32(PHOFF + 28u, 2048u);
    expect(KERNEL_ELF_BAD_ALIGNMENT, "alignment below the page size");

    build_valid();
    put32(PHOFF + 28u, 3u);
    expect(KERNEL_ELF_BAD_ALIGNMENT, "alignment not a power of two");

    build_valid();
    put32(PHOFF + 8u, 0x1000u); /* below the permitted minimum */
    expect(KERNEL_ELF_BAD_RANGE, "segment below the user range");

    build_valid();
    put32(PHOFF + 8u, 0xfffff000u);
    expect(KERNEL_ELF_BAD_RANGE, "segment above the user range");

    build_valid();
    put32(PHOFF + 20u, 0xfffff001u); /* memory size rounds past the top */
    expect(KERNEL_ELF_BAD_RANGE, "memory size overflow");
}

static void test_ordering_and_limits(void)
{
    KernelElfLimits small = limits;
    KernelElfImage plan;

    /* Data placed before text: ascending order is required, not inferred. */
    build_valid();
    put32(PHOFF + 32u + 8u, TEXT_VADDR - PAGE);
    expect(KERNEL_ELF_UNORDERED, "descending segments");

    /* Data starting inside the text page. */
    build_valid();
    put32(PHOFF + 32u + 8u, TEXT_VADDR);
    expect(KERNEL_ELF_OVERLAP, "identical segment addresses");

    build_valid();
    put32(PHOFF + 16u, PAGE);
    put32(PHOFF + 20u, PAGE * 3u); /* text now runs into data */
    expect(KERNEL_ELF_OVERLAP, "text overrunning data");

    build_valid();
    small.maximum_pages = 2u;
    assert(kernel_elf_accept(image, image_size, &small, &plan) ==
           KERNEL_ELF_TOO_LARGE);
    small.maximum_pages = 3u;
    assert(kernel_elf_accept(image, image_size, &small, &plan) ==
           KERNEL_ELF_OK);
}

static void test_segment_capacity(void)
{
    uint32_t index;

    /* Five loadable segments, one more than the profile accepts. */
    build_valid();
    image_size = 8u * PAGE;
    put16(44u, 5u);
    for (index = 0u; index < 5u; ++index) {
        uint32_t header = PHOFF + (index * 32u);

        put32(header + 0u, 1u);
        put32(header + 4u, index * PAGE);
        put32(header + 8u, TEXT_VADDR + (index * PAGE));
        put32(header + 12u, TEXT_VADDR + (index * PAGE));
        put32(header + 16u, PAGE);
        put32(header + 20u, PAGE);
        put32(header + 24u, index == 0u ? 5u : 6u);
        put32(header + 28u, PAGE);
    }
    expect(KERNEL_ELF_TOO_MANY_SEGMENTS, "five loadable segments");

    /* Only ignorable headers: nothing to load. */
    build_valid();
    put16(44u, 1u);
    put32(PHOFF + 0u, 4u); /* PT_NOTE */
    expect(KERNEL_ELF_NO_SEGMENTS, "no loadable segments");
}

static void test_entry_rejections(void)
{
    build_valid();
    put32(24u, TEXT_VADDR + 0x11u);
    expect(KERNEL_ELF_BAD_ENTRY, "odd entry address");

    build_valid();
    put32(24u, DATA_VADDR + 4u);
    expect(KERNEL_ELF_BAD_ENTRY, "entry inside a data segment");

    build_valid();
    put32(24u, TEXT_VADDR + PAGE);
    expect(KERNEL_ELF_BAD_ENTRY, "entry past the text segment");

    build_valid();
    put32(24u, 0u);
    expect(KERNEL_ELF_BAD_ENTRY, "null entry");
}

/*
 * A rejected image must leave nothing behind. A caller that ignores the status
 * and reads the plan anyway would otherwise map whatever the last accepted
 * segment happened to be.
 */
static void test_plan_cleared_on_failure(void)
{
    KernelElfImage plan;
    const uint8_t *raw = (const uint8_t *)&plan;
    size_t index;

    build_valid();
    put32(24u, DATA_VADDR + 4u);
    memset(&plan, 0xa5, sizeof(plan));
    assert(kernel_elf_accept(image, image_size, &limits, &plan) ==
           KERNEL_ELF_BAD_ENTRY);
    for (index = 0u; index < sizeof(plan); ++index) {
        assert(raw[index] == 0u);
    }
}

static void build_valid_library(void)
{
    build_valid();
    put16(16u, 3u); /* ET_DYN */
    put32(24u, 0u); /* libraries are opened through their export table */
    put32(PHOFF + 8u, 0u);
    put32(PHOFF + 12u, 0u);
    put32(PHOFF + 32u + 8u, 0x2000u);
    put32(PHOFF + 32u + 12u, 0x2000u);
    put32(PHOFF + 64u + 0u, 2u); /* PT_DYNAMIC inside the RW load */
}

static void test_library_profile(void)
{
    KernelElfImage plan;

    build_valid_library();
    assert(kernel_elf_accept_library(image, image_size, &library_limits,
                                     &plan) == KERNEL_ELF_OK);
    assert(plan.segment_count == 2u);
    assert(plan.entry == 0u);
    assert(plan.total_pages == 3u);
    assert(kernel_elf_accept(image, image_size, &library_limits, &plan) ==
           KERNEL_ELF_BAD_TYPE);

    build_valid_library();
    put16(16u, 2u);
    assert(kernel_elf_accept_library(image, image_size, &library_limits,
                                     &plan) == KERNEL_ELF_BAD_TYPE);

    build_valid_library();
    put32(24u, 0x10u);
    assert(kernel_elf_accept_library(image, image_size, &library_limits,
                                     &plan) == KERNEL_ELF_BAD_ENTRY);

    build_valid_library();
    put32(PHOFF + 64u, 3u); /* PT_INTERP remains forbidden */
    assert(kernel_elf_accept_library(image, image_size, &library_limits,
                                     &plan) ==
           KERNEL_ELF_UNSUPPORTED_SEGMENT);
}

/*
 * Truncating an accepted image at every byte boundary must always either
 * reject it or accept it with a plan that stays inside the bytes provided.
 */
static void test_truncation_sweep(void)
{
    uint32_t length;

    build_valid();
    for (length = 0u; length < 4u * KERNEL_ELF_PHENTSIZE + PHOFF; ++length) {
        KernelElfImage plan;
        KernelElfStatus status =
            kernel_elf_accept(image, length, &limits, &plan);

        if (status != KERNEL_ELF_OK) {
            continue;
        }
        for (uint32_t index = 0u; index < plan.segment_count; ++index) {
            assert((uint64_t)plan.segment[index].file_offset +
                       plan.segment[index].file_size <= length);
        }
    }
}

/*
 * Every single-byte corruption of a valid image must either be accepted with a
 * plan that stays inside the image, or rejected. It must never produce a plan
 * that would read or map outside the bounds it was given.
 */
static void test_single_byte_corruption(void)
{
    uint32_t offset;

    for (offset = 0u; offset < PHOFF + (PHNUM * KERNEL_ELF_PHENTSIZE);
         ++offset) {
        static const uint8_t patterns[] = {0x00u, 0x01u, 0x7fu, 0x80u, 0xffu};
        uint32_t pattern;

        for (pattern = 0u; pattern < sizeof(patterns); ++pattern) {
            KernelElfImage plan;
            uint32_t index;

            build_valid();
            image[offset] ^= patterns[pattern];
            if (kernel_elf_accept(image, image_size, &limits, &plan) !=
                KERNEL_ELF_OK) {
                continue;
            }
            assert(plan.segment_count != 0u &&
                   plan.segment_count <= KERNEL_ELF_SEGMENT_MAX);
            assert(plan.total_pages != 0u &&
                   plan.total_pages <= limits.maximum_pages);
            for (index = 0u; index < plan.segment_count; ++index) {
                const KernelElfSegment *segment = &plan.segment[index];
                uint64_t file_end = (uint64_t)segment->file_offset +
                                    segment->file_size;
                uint64_t memory_end = (uint64_t)segment->virtual_address +
                                      ((uint64_t)segment->page_count * PAGE);

                assert(file_end <= image_size);
                assert(segment->virtual_address >= limits.minimum_address);
                assert(memory_end - 1u <= limits.maximum_address);
                assert(segment->file_size <= segment->memory_size);
                assert((segment->rights & KERNEL_ELF_SEGMENT_READ) != 0u);
                assert((segment->rights &
                        (KERNEL_ELF_SEGMENT_WRITE |
                         KERNEL_ELF_SEGMENT_EXEC)) !=
                       (KERNEL_ELF_SEGMENT_WRITE | KERNEL_ELF_SEGMENT_EXEC));
            }
        }
    }
}

/*
 * Optional: validate a real executable produced by the cross toolchain.
 * ASTRA_ELF_FIXTURE names a file; the profile must accept it unchanged.
 */
static void test_real_image(void)
{
    const char *path = getenv("ASTRA_ELF_FIXTURE");
    /*
     * The limits the kernel itself applies, not the synthetic ones the
     * hand-built images above are measured against. A real service is far
     * larger than those: the supervisor needs 78 pages once lwext4's arena is
     * in its BSS. Measured against the 64-page ceiling used for the images in
     * this file it was refused, and the refusal said nothing about the loader.
     */
    static const KernelElfLimits real_limits = {
        .minimum_address = KERNEL_VM_USER_MIN + KERNEL_PAGE_SIZE,
        .maximum_address = KERNEL_PROCESS_STACK_BASE - 1u,
        .maximum_pages = KERNEL_PROCESS_IMAGE_PAGES_MAX,
        .page_size = KERNEL_PAGE_SIZE,
    };
    KernelElfImage plan;
    KernelElfStatus status;
    static uint8_t fixture[512u * 1024u];
    size_t length;
    FILE *file;

    if (path == NULL) {
        return;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        printf("FAIL fixture %s: cannot open\n", path);
        fflush(stdout);
        abort();
    }
    length = fread(fixture, 1u, sizeof(fixture), file);
    fclose(file);

    status = kernel_elf_accept(fixture, (uint32_t)length, &real_limits, &plan);
    if (status != KERNEL_ELF_OK) {
        printf("FAIL fixture %s: %s\n", path, kernel_elf_status_text(status));
        fflush(stdout);
        abort();
    }
    printf("fixture %s: %u segments, %u pages, entry 0x%08x\n", path,
           (unsigned)plan.segment_count, (unsigned)plan.total_pages,
           (unsigned)plan.entry);
}

int main(void)
{
    test_valid_plan();
    test_arguments();
    test_identity_rejections();
    test_header_rejections();
    test_segment_rejections();
    test_ordering_and_limits();
    test_segment_capacity();
    test_entry_rejections();
    test_plan_cleared_on_failure();
    test_library_profile();
    test_truncation_sweep();
    test_single_byte_corruption();
    test_real_image();
    puts("astra elf acceptance: PASS");
    return 0;
}
