#ifndef ASTRA_LIBRARY_H
#define ASTRA_LIBRARY_H

/**
 * @file library.h
 * @brief Identity carried by every Astra shared library: the fixed `.astra_library` record.
 *
 * The filename is chosen by the installer; this record is what the file says
 * it is.  It is fixed-size, loaded, and retained in `.astra_library`, just as
 * AstraProgram is for executables.  The ELF header remains the authority for
 * machine and byte order, while this record owns release and ABI identity.
 *
 * This header is also included by the loader's `start.S` assembly source, so
 * every comment added here stays a C-style block comment (never a `//` line
 * comment), and nothing but comment text is added.
 */

#include <astra/compiler.h>
#include <astra/address_space.h>
#include <astra/syscall.h>

/** @defgroup astra_library Shared-library identity record
 *  @brief The `.astra_library` ABI: one fixed, loaded record per shared library image.
 *  @{
 */

/** Native-big-endian `ALIB` record signature. */
#define ASTRA_LIBRARY_MAGIC 0x414c4942u
/** Current `.astra_library` record layout revision. */
#define ASTRA_LIBRARY_RECORD_VERSION 2u
/** Fixed size of one ::AstraLibrary record, in bytes. */
#define ASTRA_LIBRARY_SIZE 128u
/** Fixed byte offset of the `.astra_library` record within a loaded library image; both the kernel and userspace loaders read the record at this offset directly, without walking ELF section headers. */
#define ASTRA_LIBRARY_FILE_OFFSET 0x00000200u
/** Fixed capacity, in bytes, of ::AstraLibrary's and ::AstraLibraryReference's `name` field, including the NUL terminator for any shorter value. */
#define ASTRA_LIBRARY_NAME_MAX 24u
/** Fixed capacity, in bytes, of ::AstraLibrary's `author` field, including the NUL terminator for any shorter value. */
#define ASTRA_LIBRARY_AUTHOR_MAX 32u
/** Fixed capacity, in bytes, of ::AstraLibrary's `copyright` field, including the NUL terminator for any shorter value. */
#define ASTRA_LIBRARY_COPYRIGHT_MAX 40u
/** Required ::AstraLibrary `target` value identifying an MC68040 image; the big-endian ASCII bytes "M040". */
#define ASTRA_LIBRARY_TARGET_M68040 0x4d303430u

/** Fixed size of one ::AstraLibraryReference, in bytes; a reference's `size` field must match this so the kernel can tell an ABI-incompatible reference struct apart from a stale caller. */
#define ASTRA_LIBRARY_REFERENCE_SIZE 44u

#ifndef __ASSEMBLER__

#include <stdint.h>

/**
 * The fixed, loaded `.astra_library` record: what a shared library says about itself.
 */
typedef struct AstraLibrary {
    /** Must equal ::ASTRA_LIBRARY_MAGIC. */
    uint32_t magic;
    /** Must equal ::ASTRA_LIBRARY_RECORD_VERSION. */
    uint16_t record_version;
    /** Must equal ::ASTRA_LIBRARY_SIZE. */
    uint16_t header_size;
    /** Major release version. */
    uint16_t major;
    /** Minor release version. */
    uint16_t minor;
    /** Patch release version. */
    uint16_t patch;
    /** Major ABI version a caller links against; distinct from the release version. */
    uint16_t abi_major;
    /** Minor ABI version a caller links against; distinct from the release version. */
    uint16_t abi_minor;
    /** Reserved; must be zero. */
    uint16_t reserved_flags;
    /** Must equal ::ASTRA_LIBRARY_TARGET_M68040. */
    uint32_t target;
    /** Which build; zero until a build defines ASTRA_BUILD_ID. */
    uint32_t build_id;
    /** Reserved; must be zero. */
    uint32_t reserved;
    /** Library name, NUL-terminated when shorter than the field. */
    char name[ASTRA_LIBRARY_NAME_MAX];
    /** Author, NUL-terminated when shorter than the field. */
    char author[ASTRA_LIBRARY_AUTHOR_MAX];
    /** Copyright notice, NUL-terminated when shorter than the field. */
    char copyright[ASTRA_LIBRARY_COPYRIGHT_MAX];
} AstraLibrary;

/**
 * Exact resolved identity used to attach an already-resident Kit library.
 *
 * Callers and the kernel's resident-library cache exchange this, rather than
 * the file-shaped ::AstraLibrary, when the question is only "is this the same
 * library build the caller already resolved" and no file is being read.
 */
typedef struct AstraLibraryReference {
    /** Must equal ::ASTRA_LIBRARY_REFERENCE_SIZE. */
    uint32_t size;
    /** Must equal the resident library's ::AstraLibrary `build_id`. */
    uint32_t build_id;
    /** Library name, NUL-terminated when shorter than the field. */
    char name[ASTRA_LIBRARY_NAME_MAX];
    /** Major release version. */
    uint16_t major;
    /** Minor release version. */
    uint16_t minor;
    /** Patch release version. */
    uint16_t patch;
    /** Major ABI version; must be nonzero. */
    uint16_t abi_major;
    /** Minor ABI version. */
    uint16_t abi_minor;
    /** Reserved; must be zero. */
    uint16_t reserved;
} AstraLibraryReference;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraLibrary) == ASTRA_LIBRARY_SIZE,
               "library metadata layout changed");
_Static_assert(sizeof(AstraLibraryReference) == ASTRA_LIBRARY_REFERENCE_SIZE,
               "library reference layout changed");
/** @endcond */

#if defined(__ELF__)
/** Section, retention, alignment and visibility attributes placing ::AstraLibrary in the loaded `.astra_library` section (ELF targets). */
#define ASTRA_LIBRARY_SECTION \
    __attribute__((section(".astra_library"), used, aligned(4), \
                   visibility("hidden")))
#else
/** Retention and alignment attributes for ::AstraLibrary on non-ELF (host) builds, which have no `.astra_library` section to place it in. */
#define ASTRA_LIBRARY_SECTION __attribute__((used, aligned(4)))
#endif

/** @cond ASTRA_INTERNAL */
#ifndef ASTRA_BUILD_ID
/** Default `AstraLibrary::build_id` when the build does not define `ASTRA_BUILD_ID` itself. */
#define ASTRA_BUILD_ID 0u
#endif
/** @endcond */

/**
 * Define this image's ::AstraLibrary record, once per shared library image.
 *
 * @param library_name Library name string literal; must fit ::ASTRA_LIBRARY_NAME_MAX including its NUL.
 * @param library_major Major release version.
 * @param library_minor Minor release version.
 * @param library_patch Patch release version.
 * @param library_abi_major Major ABI version callers link against.
 * @param library_abi_minor Minor ABI version callers link against.
 * @param library_author Author string literal; must fit ::ASTRA_LIBRARY_AUTHOR_MAX including its NUL.
 * @param library_copyright Copyright notice string literal; must fit ::ASTRA_LIBRARY_COPYRIGHT_MAX including its NUL.
 */
#define ASTRA_DYNAMIC_LIBRARY(                                                \
                      library_name, library_major, library_minor,            \
                      library_patch, library_abi_major, library_abi_minor,   \
                      library_author, library_copyright)                    \
    _Static_assert(sizeof(library_name) <= ASTRA_LIBRARY_NAME_MAX,           \
                   "library name is too long");                            \
    _Static_assert(sizeof(library_author) <= ASTRA_LIBRARY_AUTHOR_MAX,       \
                   "library author is too long");                          \
    _Static_assert(sizeof(library_copyright) <=                              \
                       ASTRA_LIBRARY_COPYRIGHT_MAX,                          \
                   "library copyright is too long");                       \
    const AstraLibrary astra_library ASTRA_LIBRARY_SECTION = {              \
        ASTRA_LIBRARY_MAGIC, ASTRA_LIBRARY_RECORD_VERSION,                  \
        ASTRA_LIBRARY_SIZE, library_major, library_minor, library_patch,    \
        library_abi_major, library_abi_minor, 0u,                            \
        ASTRA_LIBRARY_TARGET_M68040, ASTRA_BUILD_ID,                       \
        0u, library_name,                                                   \
        library_author, library_copyright                                   \
    }

#endif

/** @} */

#endif
