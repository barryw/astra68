#ifndef ASTRA_LIBRARY_H
#define ASTRA_LIBRARY_H

/*
 * Identity carried by every Astra shared library.
 *
 * The filename is chosen by the installer; this record is what the file says
 * it is.  It is fixed-size, loaded, and retained in `.astra_library`, just as
 * AstraProgram is for executables.  The ELF header remains the authority for
 * machine and byte order, while this record owns release and ABI identity.
 */

#define ASTRA_LIBRARY_MAGIC 0x414c4942u /* "ALIB" */
#define ASTRA_LIBRARY_RECORD_VERSION 1u
#define ASTRA_LIBRARY_SIZE 128u
#define ASTRA_LIBRARY_FILE_OFFSET 0x00000200u
#define ASTRA_LIBRARY_NAME_MAX 24u
#define ASTRA_LIBRARY_AUTHOR_MAX 32u
#define ASTRA_LIBRARY_COPYRIGHT_MAX 40u
#define ASTRA_LIBRARY_TARGET_M68030 0x4d303330u /* "M030" */
#define ASTRA_LIBRARY_EXPORTS_OFFSET 0x00f00000u

/* Runtime mapping window: fifteen independent 16 MiB slots. */
#define ASTRA_LIBRARY_BASE 0x20000000u
#define ASTRA_LIBRARY_SLOT_SIZE 0x01000000u
#define ASTRA_LIBRARY_SLOT_COUNT 15u
#define ASTRA_LIBRARY_IMAGE_MAX 0x00040000u
#define ASTRA_LIBRARY_REFERENCE_SIZE 44u
#define ASTRA_LIBRARY_REFERENCE_EXACT 0u
#define ASTRA_LIBRARY_REFERENCE_LATEST 1u

#ifndef __ASSEMBLER__

#include <stdint.h>

typedef struct AstraLibrary {
    uint32_t magic;
    uint16_t record_version;
    uint16_t header_size;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t flags;
    uint32_t target;
    uint32_t build_id;
    uint32_t exports_offset;
    char name[ASTRA_LIBRARY_NAME_MAX];
    char author[ASTRA_LIBRARY_AUTHOR_MAX];
    char copyright[ASTRA_LIBRARY_COPYRIGHT_MAX];
} AstraLibrary;

/* Exact resolved identity used to attach an already-resident Kit library. */
typedef struct AstraLibraryReference {
    uint32_t size;
    uint32_t build_id;
    char name[ASTRA_LIBRARY_NAME_MAX];
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t flags;
} AstraLibraryReference;

_Static_assert(sizeof(AstraLibrary) == ASTRA_LIBRARY_SIZE,
               "library metadata layout changed");
_Static_assert(sizeof(AstraLibraryReference) == ASTRA_LIBRARY_REFERENCE_SIZE,
               "library reference layout changed");

#if defined(__ELF__)
#define ASTRA_LIBRARY_SECTION \
    __attribute__((section(".astra_library"), used, aligned(4)))
#else
#define ASTRA_LIBRARY_SECTION __attribute__((used, aligned(4)))
#endif

#ifndef ASTRA_BUILD_ID
#define ASTRA_BUILD_ID 0u
#endif

#define ASTRA_LIBRARY(library_name, library_major, library_minor,            \
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
        library_abi_major, library_abi_minor, 0u,                           \
        ASTRA_LIBRARY_TARGET_M68030, ASTRA_BUILD_ID,                       \
        ASTRA_LIBRARY_EXPORTS_OFFSET, library_name,                        \
        library_author, library_copyright                                   \
    }

#define ASTRA_LIBRARY_EXPORTS \
    __attribute__((section(".astra_exports"), used, aligned(4)))

#endif

#endif
