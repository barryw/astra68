#ifndef ASTRA_PROGRAM_H
#define ASTRA_PROGRAM_H

/*
 * What a program says it is.
 *
 * Every image on this machine declares its name, its version, who wrote it and
 * who holds the copyright, in one fixed record in `.astra_program`. Not by
 * convention: `astra_user.ld` asserts the section is exactly one record long,
 * so an image with none does not link and an image with two does not either.
 *
 * Mandatory because the alternative is what every other system has -- a fleet
 * of files nobody can attribute, and a version question answered by a changelog
 * if one was kept. A machine that can say *this is `events` 1.0.0, by whom,
 * from which build* about every program it holds answers support questions that
 * otherwise cost an afternoon each, and the cost is 120 bytes and one line at
 * the top of a source file. See the layout spec's 11.2.
 *
 * The record is loaded, unlike the event catalog beside it. Truth about a file
 * is in the file (11.4): a `version` command, or a desktop showing what it is
 * about to run, has to be able to read this off the image as installed, and an
 * image whose provenance only exists in the unstripped build is a file nobody
 * on the machine can attribute.
 *
 * The strings are arrays rather than pointers for the same reason the event
 * descriptor's are: a pointer would put the text somewhere else and leave this
 * record unable to answer on its own, which defeats a record whose entire job
 * is to be readable by a tool that knows nothing but where the section is.
 */

#define ASTRA_PROGRAM_MAGIC 0x41505247u /* "APRG" -- it types itself, per 11.1 */
#define ASTRA_PROGRAM_RECORD_VERSION 1u
#define ASTRA_PROGRAM_SIZE 120u
#define ASTRA_PROGRAM_NAME_MAX 24u
#define ASTRA_PROGRAM_AUTHOR_MAX 32u
#define ASTRA_PROGRAM_COPYRIGHT_MAX 48u

#ifndef __ASSEMBLER__

#include <stdint.h>

/*
 * A version is three numbers rather than a string, because the question asked
 * of a version is almost always a comparison and nobody can compare "1.10" and
 * "1.9" as text without first agreeing on how.
 */
typedef struct AstraProgram {
    uint32_t magic;
    uint16_t record_version;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    /*
     * Which build, so a report about a program can be joined to the events its
     * build emitted. Zero until a build defines ASTRA_BUILD_ID; the events
     * spec's process-start event is what will need it to be real, and filling
     * it before then would be a number that looks like provenance and is not.
     */
    uint32_t build_id;
    char     name[ASTRA_PROGRAM_NAME_MAX];
    char     author[ASTRA_PROGRAM_AUTHOR_MAX];
    char     copyright[ASTRA_PROGRAM_COPYRIGHT_MAX];
} AstraProgram;

_Static_assert(sizeof(AstraProgram) == ASTRA_PROGRAM_SIZE,
               "tools/program_info.py walks this in fixed steps");

/*
 * Mach-O spells a section as segment,section and refuses this one outright, so
 * a host build places the record wherever the compiler likes. Placement is a
 * link-time property and it is the cross-build's linker assertion that checks
 * it, not a host test that cannot see a linker script anyway.
 */
#if defined(__ELF__)
#define ASTRA_PROGRAM_SECTION \
    __attribute__((section(".astra_program"), used, aligned(4)))
#else
#define ASTRA_PROGRAM_SECTION __attribute__((used, aligned(4)))
#endif

#ifndef ASTRA_BUILD_ID
#define ASTRA_BUILD_ID 0u
#endif

/*
 * The declaration, once per image:
 *
 *     ASTRA_PROGRAM("events", 1, 0, 0, "Barry Walker",
 *                   "Copyright 2026 Barry Walker");
 *
 * The symbol is deliberately not static. Two declarations in one image are a
 * duplicate symbol, which names both files; the linker's assertion catches the
 * image that has none. Two mechanisms because they catch different mistakes and
 * the one that names the file is worth having for the one that is more likely.
 *
 * A string too long for its field fails to build rather than being cut. A
 * truncated copyright notice is a legal statement somebody did not make, and a
 * truncated name is a program answering to something nobody installed.
 */
#define ASTRA_PROGRAM(program_name, program_major, program_minor,             \
                      program_patch, program_author, program_copyright)       \
    _Static_assert(sizeof(program_name) <= ASTRA_PROGRAM_NAME_MAX,            \
                   "a program name must fit ASTRA_PROGRAM_NAME_MAX");         \
    _Static_assert(sizeof(program_author) <= ASTRA_PROGRAM_AUTHOR_MAX,        \
                   "an author must fit ASTRA_PROGRAM_AUTHOR_MAX");            \
    _Static_assert(sizeof(program_copyright) <= ASTRA_PROGRAM_COPYRIGHT_MAX,  \
                   "a copyright must fit ASTRA_PROGRAM_COPYRIGHT_MAX");       \
    const AstraProgram astra_program ASTRA_PROGRAM_SECTION = {                \
        ASTRA_PROGRAM_MAGIC, (uint16_t)ASTRA_PROGRAM_RECORD_VERSION,          \
        (uint16_t)(program_major), (uint16_t)(program_minor),                 \
        (uint16_t)(program_patch), (uint32_t)(ASTRA_BUILD_ID),                \
        program_name, program_author, program_copyright                       \
    }

#endif

#endif
