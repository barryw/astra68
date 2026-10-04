#ifndef ASTRA_PROGRAM_H
#define ASTRA_PROGRAM_H

/**
 * @file program.h
 * @brief What a program says about itself: the fixed `.astra_program` record every Astra image carries.
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
 * on the machine can attribute. `tools/program_info.py` reads the record this
 * way, straight off an installed ELF image.
 *
 * The strings are arrays rather than pointers for the same reason the event
 * descriptor's are: a pointer would put the text somewhere else and leave this
 * record unable to answer on its own, which defeats a record whose entire job
 * is to be readable by a tool that knows nothing but where the section is.
 */

#include <astra/compiler.h>

/** @defgroup astra_program Program identity record
 *  @brief The `.astra_program` ABI: one fixed, loaded record per image.
 *  @{
 */

/** Native-big-endian `APRG` record signature (it types itself, per the layout spec's 11.1). */
#define ASTRA_PROGRAM_MAGIC 0x41505247u
/** Current `.astra_program` record layout revision. */
#define ASTRA_PROGRAM_RECORD_VERSION 1u
/** Fixed size of one ::AstraProgram record, in bytes. */
#define ASTRA_PROGRAM_SIZE 120u
/** Fixed capacity, in bytes, of ::AstraProgram's `name` field, including the NUL terminator for any shorter value. */
#define ASTRA_PROGRAM_NAME_MAX 24u
/** Fixed capacity, in bytes, of ::AstraProgram's `author` field, including the NUL terminator for any shorter value. */
#define ASTRA_PROGRAM_AUTHOR_MAX 32u
/** Fixed capacity, in bytes, of ::AstraProgram's `copyright` field, including the NUL terminator for any shorter value. */
#define ASTRA_PROGRAM_COPYRIGHT_MAX 48u

#ifndef __ASSEMBLER__

#include <stdint.h>

/**
 * The fixed, loaded `.astra_program` record: what a program says about itself.
 *
 * A version is three numbers rather than a string, because the question asked
 * of a version is almost always a comparison and nobody can compare "1.10" and
 * "1.9" as text without first agreeing on how.
 */
typedef struct AstraProgram {
    /** Must equal ::ASTRA_PROGRAM_MAGIC. */
    uint32_t magic;
    /** Must equal ::ASTRA_PROGRAM_RECORD_VERSION. */
    uint16_t record_version;
    /** Major release version. */
    uint16_t major;
    /** Minor release version. */
    uint16_t minor;
    /** Patch release version. */
    uint16_t patch;
    /**
     * Which build, so a report about a program can be joined to the events its
     * build emitted. Zero until a build defines ASTRA_BUILD_ID; the events
     * spec's process-start event is what will need it to be real, and filling
     * it before then would be a number that looks like provenance and is not.
     */
    uint32_t build_id;
    /** Program name, NUL-terminated when shorter than the field. */
    char     name[ASTRA_PROGRAM_NAME_MAX];
    /** Author, NUL-terminated when shorter than the field. */
    char     author[ASTRA_PROGRAM_AUTHOR_MAX];
    /** Copyright notice, NUL-terminated when shorter than the field. */
    char     copyright[ASTRA_PROGRAM_COPYRIGHT_MAX];
} AstraProgram;

/** @cond ASTRA_INTERNAL */
_Static_assert(sizeof(AstraProgram) == ASTRA_PROGRAM_SIZE,
               "tools/program_info.py walks this in fixed steps");
/** @endcond */

/*
 * Mach-O spells a section as segment,section and refuses this one outright, so
 * a host build places the record wherever the compiler likes. Placement is a
 * link-time property and it is the cross-build's linker assertion that checks
 * it, not a host test that cannot see a linker script anyway.
 */
#if defined(__ELF__)
/** Section, retention and alignment attributes placing ::AstraProgram in the loaded `.astra_program` section (ELF targets). */
#define ASTRA_PROGRAM_SECTION \
    __attribute__((section(".astra_program"), used, aligned(4)))
#else
/** Retention and alignment attributes for ::AstraProgram on non-ELF (host) builds, which have no `.astra_program` section to place it in. */
#define ASTRA_PROGRAM_SECTION __attribute__((used, aligned(4)))
#endif

#ifndef ASTRA_BUILD_ID
/** Default `AstraProgram::build_id` when the build does not define `ASTRA_BUILD_ID` itself. */
#define ASTRA_BUILD_ID 0u
#endif

/**
 * Define this image's ::AstraProgram record, once per image:
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
 *
 * @param program_name Program name string literal; must fit ::ASTRA_PROGRAM_NAME_MAX including its NUL.
 * @param program_major Major release version.
 * @param program_minor Minor release version.
 * @param program_patch Patch release version.
 * @param program_author Author string literal; must fit ::ASTRA_PROGRAM_AUTHOR_MAX including its NUL.
 * @param program_copyright Copyright notice string literal; must fit ::ASTRA_PROGRAM_COPYRIGHT_MAX including its NUL.
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

/** @} */

#endif
