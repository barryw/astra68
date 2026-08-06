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
ASTRA_EVENT_CATALOG_BASE = 0xE0000000;

PHDRS
{
    text PT_LOAD FLAGS(5);      /* R + X */
    rodata PT_LOAD FLAGS(4);    /* R */
    data PT_LOAD FLAGS(6);      /* R + W */
    stack PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = ASTRA_TEXT_BASE;

    /*
     * The first segment carries the ELF and program headers, so its file
     * offset is zero and its virtual address is the page-aligned base.
     */
    .text : {
        *(.text.startup)
        *(.text .text.*)
    } :text

    . = ALIGN(ASTRA_PAGE_SIZE);

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(ASTRA_PAGE_SIZE);

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
