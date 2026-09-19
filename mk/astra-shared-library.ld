/* Canonical Astra ELF shared-library layout.  Every .library image uses this
 * script so the kernel, loader, tooling, and packages see one binary format. */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)

PHDRS
{
    ro      PT_LOAD FILEHDR PHDRS FLAGS(4);
    text    PT_LOAD FLAGS(5);
    data    PT_LOAD FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
    tls     PT_TLS FLAGS(4);
    relro   PT_GNU_RELRO;
    stack   PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    /* Layout constants and sentinels are implementation details, not ABI
     * symbols.  Keep them out of .dynsym even for table-only libraries that
     * intentionally need no per-library export map. */
    HIDDEN(ASTRA_PAGE_SIZE = 0x1000);
    HIDDEN(ASTRA_LIBRARY_METADATA = 0x200);
    HIDDEN(ASTRA_LIBRARY_SIZE = 128);

    . = ASTRA_LIBRARY_METADATA;
    .astra_library : {
        HIDDEN(__astra_library_start = .);
        KEEP(*(.astra_library))
        HIDDEN(__astra_library_end = .);
    } :ro

    .hash : { *(.hash) } :ro
    .gnu.hash : { *(.gnu.hash) } :ro
    .dynsym : { *(.dynsym) } :ro
    .dynstr : { *(.dynstr) } :ro
    .gnu.version : { *(.gnu.version) } :ro
    .gnu.version_d : { *(.gnu.version_d) } :ro
    .gnu.version_r : { *(.gnu.version_r) } :ro
    .rela.dyn : { *(.rela.dyn) } :ro
    .rela.plt : { *(.rela.plt) } :ro
    .rodata : { *(.rodata .rodata.*) } :ro

    /* TLS is an immutable template copied into each thread's writable TLS
     * allocation. It deliberately belongs to the read-only load segment. */
    .tdata : { *(.tdata .tdata.* .gnu.linkonce.td.*) } :ro :tls
    .tbss (NOLOAD) : {
        *(.tbss .tbss.* .gnu.linkonce.tb.*)
        *(.tcommon)
    } :ro :tls

    . = ALIGN(ASTRA_PAGE_SIZE);
    /*
     * Keep the linker-created PLT in its own output section and at the front
     * of the executable segment.  The m68k linker synthesizes PC-relative PLT
     * thunks using that placement.  Folding the input .plt behind an arbitrary
     * amount of .text moved the thunks after their displacements had been
     * chosen; every imported call then jumped past the mapped GOT.
     */
    .plt : { *(.plt .plt.*) } :text
    .text : {
        *(.text.startup)
        *(.text .text.*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } :text

    . = DATA_SEGMENT_ALIGN(ASTRA_PAGE_SIZE, ASTRA_PAGE_SIZE);
    /* Axiom intentionally accepts only whole-page load segments. GNU ld's
     * DATA_SEGMENT_ALIGN may choose a congruent sub-page VMA to save file
     * padding; round once more because congruence is not Astra's contract. */
    . = ALIGN(ASTRA_PAGE_SIZE);
    HIDDEN(__astra_writable_start = .);
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
    .dynamic : {
        HIDDEN(__astra_dynamic_start = .);
        *(.dynamic)
        HIDDEN(__astra_dynamic_end = .);
    } :data :dynamic
    .got : {
        _GLOBAL_OFFSET_TABLE_ = .;
        *(.got.plt .igot.plt)
        *(.got .igot)
    } :data
    . = DATA_SEGMENT_RELRO_END(0, .);
    .data : { *(.data .data.*) *(.sdata .sdata.*) } :data
    .bss (NOLOAD) : {
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
    } :data
    . = DATA_SEGMENT_END(.);
    HIDDEN(__astra_writable_end = .);
    HIDDEN(__astra_image_end = .);

    /DISCARD/ : {
        *(.comment)
        *(.note.GNU-stack)
        *(.gnu.attributes)
        *(.eh_frame .eh_frame_hdr)
    }
}

ASSERT(__astra_library_end - __astra_library_start == ASTRA_LIBRARY_SIZE,
       "this image must declare exactly one ASTRA_DYNAMIC_LIBRARY record")
ASSERT(SIZEOF(.dynamic) != 0,
       "a universal shared library must carry dynamic metadata")
