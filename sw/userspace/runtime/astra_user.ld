/*
 * Astra user process link contract.
 *
 * Produces exactly the shape kernel_elf_accept() will take: ET_EXEC for
 * MC68030, big-endian, with each loadable segment starting on its own page in
 * both the file and memory, and no segment both writable and executable.
 *
 * The page separation is deliberate and costs a little padding. The looser
 * layout the ELF format permits lets two segments share a page, which would
 * force that page to carry the union of two permission sets; Astra refuses
 * such an image rather than silently widening rights.
 *
 * Link with:
 *   -nostdlib -static -Wl,-z,max-page-size=0x1000 -T astra_user.ld
 */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

ASTRA_PAGE_SIZE = 0x1000;
ASTRA_TEXT_BASE = 0x00100000;
/* astra/event_descriptor.h's ASTRA_EVENT_CATALOG_BASE, which a linker script
 * cannot include. The two must agree; the runtime test checks that they do. */
ASTRA_EVENT_CATALOG_BASE = DEFINED(ASTRA_EVENT_IMAGE_CATALOG_BASE) ?
    ASTRA_EVENT_IMAGE_CATALOG_BASE : 0xE0000000;
/* astra/program.h's ASTRA_PROGRAM_SIZE, for the same reason. A disagreement
 * here refuses every image at link time, which is as loud as a mistake gets. */
ASTRA_PROGRAM_SIZE = 120;

PHDRS
{
    text PT_LOAD FILEHDR PHDRS FLAGS(5); /* ELF headers + R + X text */
    rodata PT_LOAD FLAGS(4);    /* R */
    data PT_LOAD FLAGS(6);      /* R + W */
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = ASTRA_TEXT_BASE + SIZEOF_HEADERS;

    /*
     * The first segment carries the ELF and program headers, so its file
     * offset is zero and its virtual address is the page-aligned base.
     */
    .text : {
        *(.text.startup)
        *(.text .text.*)
    } :text

    . = ALIGN(ASTRA_PAGE_SIZE);

    /*
     * What this image says it is: one ASTRA_PROGRAM record, and the assertion
     * at the end of this file that there is exactly one.
     *
     * Loaded, unlike the event catalog below. Truth about a file is in the file
     * (layout spec 11.4), so the record has to survive into the image that gets
     * installed -- a `version` command reading an unstripped build would be
     * answering about a different file than the one that runs.
     *
     * KEEP, because --gc-sections is on and nothing references this record by
     * name: it is found by walking the section, which a collector cannot see.
     */
    .astra_program : {
        __astra_program_start = .;
        KEEP(*(.astra_program))
        __astra_program_end = .;
    } :rodata

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(ASTRA_PAGE_SIZE);

    /*
     * GNU m68k libgcc addresses its floating-point control word through the
     * GOT. Keep the toolchain's required order: _GLOBAL_OFFSET_TABLE_ names
     * the .got.plt prefix and ordinary GOT entries follow it. Leaving these
     * as separate orphan sections put .got first, so libgcc loaded a zero
     * .got.plt word and faulted at address 6 while formatting a fraction.
     */
    .got : {
        *(.got.plt .igot.plt)
        *(.got .igot)
    } :data

    .data : {
        *(.data .data.*)
        *(.sdata .sdata.*)
    } :data

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    /*
     * The event catalog: every ASTRA_EVENT call site's subsystem, level, file,
     * line, format and argument types, emitted once each.
     *
     * Not loaded, not mapped, and based far from every real address, because a
     * descriptor's address is only ever used as a message id -- so it can
     * never be mistaken for a pointer nor collide with the reserved ids. The
     * ROM image strips the section, the way it already strips DWARF, and a
     * build step reads it into a catalog first.
     *
     * KEEP, because --gc-sections is on and a record referenced only by having
     * its address taken is exactly what a collector gets wrong.
     */
    . = ASTRA_EVENT_CATALOG_BASE;
    .astra_events (INFO) : {
        __astra_events_start = .;
        KEEP(*(.astra_events))
        __astra_events_end = .;
    }

    /*
     * Nothing else may reach the image. A section that is not placed here is a
     * toolchain feature Astra has not qualified, and silently discarding it
     * would hide that.
     */
    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
        *(.gnu.attributes)
    }
}

/*
 * Every Astra program declares its own provenance, and this is what makes that
 * a rule rather than a habit: an image with no ASTRA_PROGRAM record does not
 * link. Two records are caught earlier and more usefully, by the duplicate
 * `astra_program` symbol naming both files.
 *
 * Layout spec 11.2. The supervisor declares one too -- it is the first image on
 * the machine and the rule has no exceptions, which is the only way a rule
 * about every program survives the first program that finds it inconvenient.
 */
ASSERT(__astra_program_end - __astra_program_start == ASTRA_PROGRAM_SIZE,
       "this image declares no ASTRA_PROGRAM record; see astra/program.h")
