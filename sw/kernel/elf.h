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
 *   - at most one read-only PT_TLS template, wholly covered by one PT_LOAD
 *   - at most one writable PT_DYNAMIC table, wholly covered by one PT_LOAD
 *   - a dynamic executable carries exactly one read-only PT_INTERP identity,
 *     wholly covered by a read-only PT_LOAD
 *   - an entry point inside an executable segment at an even address
 *
 * Executables and shared libraries have separate entry points below. Shared
 * objects admit ET_DYN plus PT_DYNAMIC and retain the same strict W^X/load
 * rules. Ordinary libraries require a zero ELF entry point and may carry one
 * immutable TLS template. The dynamic interpreter requires an entry point,
 * carries no TLS (it must bootstrap TLS), and never carries another
 * interpreter. Lazy binding is not accepted by any profile.
 */

/*
 * The current toolchain emits three PT_LOAD records. Keep four inline so the
 * measured common path allocates nothing; additional records spill into
 * resource-backed pages and are not an acceptance limit.
 */
#define KERNEL_ELF_SEGMENT_INLINE 4u
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
    KERNEL_ELF_OUT_OF_MEMORY,
    KERNEL_ELF_UNSUPPORTED_SEGMENT,
    KERNEL_ELF_EXECUTABLE_STACK,
    KERNEL_ELF_BAD_PERMISSIONS,
    KERNEL_ELF_BAD_ALIGNMENT,
    KERNEL_ELF_BAD_RANGE,
    KERNEL_ELF_UNORDERED,
    KERNEL_ELF_OVERLAP,
    KERNEL_ELF_TOO_LARGE,
    KERNEL_ELF_BAD_TLS,
    KERNEL_ELF_BAD_DYNAMIC,
    KERNEL_ELF_BAD_INTERPRETER,
    KERNEL_ELF_BAD_ENTRY,
    KERNEL_ELF_BAD_RELRO
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

typedef struct KernelElfTls {
    uint32_t file_offset;
    uint32_t file_size;
    uint32_t virtual_address;
    uint32_t memory_size;
    uint32_t alignment;
} KernelElfTls;

typedef struct KernelElfDynamic {
    uint32_t file_offset;
    uint32_t virtual_address;
    uint32_t size;
} KernelElfDynamic;

typedef struct KernelElfInterpreter {
    uint32_t file_offset;
    uint32_t virtual_address;
    uint32_t size;
} KernelElfInterpreter;

typedef struct KernelElfRelro {
    uint32_t file_offset;
    uint32_t file_size;
    uint32_t virtual_address;
    uint32_t memory_size;
} KernelElfRelro;

typedef struct KernelElfSegmentBlock KernelElfSegmentBlock;

typedef struct KernelElfImage {
    KernelElfSegment segment[KERNEL_ELF_SEGMENT_INLINE];
    KernelElfSegmentBlock *segment_blocks;
    KernelElfSegmentBlock *segment_blocks_tail;
    KernelElfTls tls;
    KernelElfDynamic dynamic;
    KernelElfInterpreter interpreter;
    KernelElfRelro relro;
    uint32_t segment_count;
    uint32_t entry;
    uint32_t total_pages;
    uint32_t writable_bytes;
    uint8_t has_tls;
    uint8_t has_dynamic;
    uint8_t has_interpreter;
    uint8_t has_relro;
    uint8_t owns_segment_blocks;
} KernelElfImage;

typedef enum KernelElfRole {
    KERNEL_ELF_EXECUTABLE = 0u,
    KERNEL_ELF_SHARED_LIBRARY,
    KERNEL_ELF_INTERPRETER
} KernelElfRole;

/*
 * Incremental executable acceptance. The fixed ELF header is accepted once,
 * then each program header is supplied from the exact file offset reported by
 * kernel_elf_stream_next_header(). No byte outside those small records needs
 * to be resident while the placement plan is built.
 */
typedef struct KernelElfStream {
    KernelElfImage plan;
    KernelElfLimits limits;
    uint32_t image_size;
    uint32_t entry;
    uint32_t header_offset;
    uint32_t header_count;
    uint32_t header_index;
    uint32_t total_pages;
    uint8_t role;
    uint8_t failed;
    uint8_t complete;
    uint8_t tls_seen;
    uint8_t dynamic_seen;
    uint8_t interpreter_seen;
    uint8_t relro_seen;
} KernelElfStream;

KernelElfStatus kernel_elf_stream_begin(const void *header,
                                        uint32_t image_size,
                                        const KernelElfLimits *limits,
                                        KernelElfStream *stream);
KernelElfStatus kernel_elf_stream_begin_interpreter(
    const void *header, uint32_t image_size, const KernelElfLimits *limits,
    KernelElfStream *stream);
KernelElfStatus kernel_elf_stream_begin_library(
    const void *header, uint32_t image_size, const KernelElfLimits *limits,
    KernelElfStream *stream);
KernelElfStatus kernel_elf_stream_next_header(const KernelElfStream *stream,
                                              uint32_t *offset,
                                              uint32_t *length);
KernelElfStatus kernel_elf_stream_add_header(KernelElfStream *stream,
                                             const void *header);
KernelElfStatus kernel_elf_stream_finish(KernelElfStream *stream,
                                         KernelElfImage *plan);
bool kernel_elf_stream_discard(KernelElfStream *stream);
bool kernel_elf_image_discard(KernelElfImage *plan);
void kernel_elf_image_move(KernelElfImage *destination,
                           KernelElfImage *source);
const KernelElfSegment *kernel_elf_image_segment(
    const KernelElfImage *plan, uint32_t index);
bool kernel_elf_image_equal(const KernelElfImage *left,
                            const KernelElfImage *right);

#if defined(KERNEL_ELF_HOST_TEST)
void kernel_elf_test_fail_segment_allocation_after(uint32_t successes);
void kernel_elf_test_clear_segment_allocation_failure(void);
#endif

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

KernelElfStatus kernel_elf_accept_interpreter_windowed(
    const void *image, uint32_t image_size, uint32_t readable,
    const KernelElfLimits *limits, KernelElfImage *plan);

KernelElfStatus kernel_elf_accept_interpreter(
    const void *image, uint32_t image_size, const KernelElfLimits *limits,
    KernelElfImage *plan);

const char *kernel_elf_status_text(KernelElfStatus status);

#endif
