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
        KEEP(*(.init))
        KEEP(*(.fini))
    } :text

    . = ALIGN(ASTRA_PAGE_SIZE);

    __astra_data_start = .;

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
        *(.gcc_except_table .gcc_except_table.*)
    } :rodata

    /* Astra interprets this as a versioned library identity rather than a
     * host path. The supervisor resolves the exact installed loader and the
     * kernel maps its already-validated image before the program runs. */
    .interp : { *(.interp) } :rodata :interp

    /* Eager runtime binding metadata consumed by the versioned interpreter. */
    .hash : { *(.hash) } :rodata
    .gnu.hash : { *(.gnu.hash) } :rodata
    .dynsym : { *(.dynsym) } :rodata
    .dynstr : { *(.dynstr) } :rodata
    .gnu.version : { *(.gnu.version) } :rodata
    .gnu.version_d : { *(.gnu.version_d) } :rodata
    .gnu.version_r : { *(.gnu.version_r) } :rodata
    .rela.dyn : { *(.rela.dyn) } :rodata
    .rela.plt : { *(.rela.plt) } :rodata

    .eh_frame_hdr : {
        *(.eh_frame_hdr)
    } :rodata

    .eh_frame : {
        KEEP(*(.eh_frame))
    } :rodata

    .note.gnu.build-id : {
        KEEP(*(.note.gnu.build-id))
    } :rodata

    /*
     * The mapped bytes are the pristine template. GNU m68k local-exec TLS
     * addresses each variable relative to a per-thread copy; keeping this
     * load read-only ensures a later thread cannot inherit mutations made by
     * an earlier one.
     */
    .tdata : {
        *(.tdata .tdata.* .gnu.linkonce.td.*)
    } :rodata :tls

    .tbss (NOLOAD) : {
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        *(.tcommon)
    } :rodata :tls

    . = DATA_SEGMENT_ALIGN(ASTRA_PAGE_SIZE, ASTRA_PAGE_SIZE);
    . = ALIGN(ASTRA_PAGE_SIZE);

    /* Relocation destinations must be writable while the loader binds the
     * image. The kernel seals this prefix through PT_GNU_RELRO before startup
     * code gets control. */
    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } :data
    .data.rel.ro : {
        *(.data.rel.ro .data.rel.ro.*)
    } :data
    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN(__init_array_end = .);
    } :data
    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } :data

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

    .dynamic : { *(.dynamic) } :data :dynamic

    /* The loader writes eager relocations once, then the kernel seals this
     * prefix before application code runs.  Keep the mutable program data on
     * following pages so even a corrupt process cannot rewrite its GOT or
     * dependency metadata. */
    . = DATA_SEGMENT_RELRO_END(0, .);

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

    __astra_data_end = .;
    . = DATA_SEGMENT_END(.);

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
        *(.note.GNU-stack)
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
ASSERT(ASTRA_DYNAMIC_IMAGE == 1 || SIZEOF(.dynamic) == 0,
       "a static image contains dynamic-link metadata")
ASSERT(__astra_data_end <= ASTRA_EXECUTABLE_ADDRESS_END,
       "program image overlaps the dynamic/shared address arenas")
