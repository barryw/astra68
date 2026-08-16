#ifndef ASTRA_KERNEL_ELF_H
#define ASTRA_KERNEL_ELF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Astra executable acceptance profile.
 *
 * This module decides whether a byte range is an Astra executable and, if so,
 * what must be mapped. It performs no allocation, touches no address space,
 * and knows nothing about page tables: it turns an untrusted image into a
 * bounded, validated placement plan that a loader can execute or discard.
 *
 * The profile is deliberately narrow. Astra accepts exactly one shape of
 * executable and rejects everything else rather than tolerating variations it
 * has not qualified. Every relaxation must be a recorded decision, because a
 * loader is the point where an untrusted file becomes an address space.
 *
 * Accepted:
 *   - ELF32, big-endian, current version, System V ABI, ABI version 0
 *   - ET_EXEC for EM_68K with zero processor flags
 *   - PT_LOAD segments only, each page-aligned in both file and memory,
 *     ascending, non-overlapping, readable, and never both writable and
 *     executable
 *   - an entry point inside an executable segment at an even address
 *
 * Executables and shared libraries have separate entry points below. Shared
 * libraries admit ET_DYN plus PT_DYNAMIC, retain the same strict W^X/load
 * rules, and require a zero ELF entry point. No interpreter or lazy binding is
 * accepted by either profile.
 */

#define KERNEL_ELF_SEGMENT_MAX 4u
#define KERNEL_ELF_HEADER_SIZE 52u
#define KERNEL_ELF_PHENTSIZE 32u

#define KERNEL_ELF_SEGMENT_READ  (1u << 0)
#define KERNEL_ELF_SEGMENT_WRITE (1u << 1)
#define KERNEL_ELF_SEGMENT_EXEC  (1u << 2)

typedef enum KernelElfStatus {
    KERNEL_ELF_OK = 0,
    KERNEL_ELF_INVALID_ARGUMENT,
    KERNEL_ELF_TRUNCATED,
    KERNEL_ELF_BAD_MAGIC,
    KERNEL_ELF_BAD_CLASS,
    KERNEL_ELF_BAD_ENDIAN,
    KERNEL_ELF_BAD_VERSION,
    KERNEL_ELF_BAD_ABI,
    KERNEL_ELF_BAD_TYPE,
    KERNEL_ELF_BAD_MACHINE,
    KERNEL_ELF_BAD_FLAGS,
    KERNEL_ELF_BAD_HEADER_TABLE,
    KERNEL_ELF_NO_SEGMENTS,
    KERNEL_ELF_TOO_MANY_SEGMENTS,
    KERNEL_ELF_UNSUPPORTED_SEGMENT,
    KERNEL_ELF_EXECUTABLE_STACK,
    KERNEL_ELF_BAD_PERMISSIONS,
    KERNEL_ELF_BAD_ALIGNMENT,
    KERNEL_ELF_BAD_RANGE,
    KERNEL_ELF_UNORDERED,
    KERNEL_ELF_OVERLAP,
    KERNEL_ELF_TOO_LARGE,
    KERNEL_ELF_BAD_ENTRY
} KernelElfStatus;

typedef struct KernelElfLimits {
    uint32_t minimum_address;
    uint32_t maximum_address; /* inclusive last byte a segment may occupy */
    uint32_t maximum_pages;   /* across every loadable segment */
    uint32_t page_size;       /* power of two */
} KernelElfLimits;

typedef struct KernelElfSegment {
    uint32_t file_offset;     /* page aligned */
    uint32_t file_size;       /* bytes to copy; the remainder is zero filled */
    uint32_t virtual_address; /* page aligned */
    uint32_t memory_size;
    uint32_t page_count;
    uint32_t rights;          /* KERNEL_ELF_SEGMENT_* */
} KernelElfSegment;

typedef struct KernelElfImage {
    KernelElfSegment segment[KERNEL_ELF_SEGMENT_MAX];
    uint32_t segment_count;
    uint32_t entry;
    uint32_t total_pages;
} KernelElfImage;

/*
 * Validates the whole image before reporting anything. On any failure `plan`
 * is left cleared, so a caller cannot act on a partially accepted image.
 */
/*
 * The same acceptance, with the headers bounded to the first `readable` bytes.
 * A loader copying an image out of another process holds a window rather than
 * the file, and everything this reads has to be inside it.
 */
KernelElfStatus kernel_elf_accept_windowed(const void *image,
                                           uint32_t image_size,
                                           uint32_t readable,
                                           const KernelElfLimits *limits,
                                           KernelElfImage *plan);

KernelElfStatus kernel_elf_accept(const void *image, uint32_t image_size,
                                  const KernelElfLimits *limits,
                                  KernelElfImage *plan);

KernelElfStatus kernel_elf_accept_library_windowed(
    const void *image, uint32_t image_size, uint32_t readable,
    const KernelElfLimits *limits, KernelElfImage *plan);

KernelElfStatus kernel_elf_accept_library(const void *image,
                                          uint32_t image_size,
                                          const KernelElfLimits *limits,
                                          KernelElfImage *plan);

const char *kernel_elf_status_text(KernelElfStatus status);

#endif
