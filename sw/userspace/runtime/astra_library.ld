/* Constrained ELF32/m68k shared-library image accepted by Axiom. */

OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)

ASTRA_PAGE_SIZE = 0x1000;
ASTRA_LIBRARY_METADATA = 0x200;
ASTRA_LIBRARY_SIZE = 128;
ASTRA_LIBRARY_EXPORTS = 0x00f00000;

PHDRS
{
    ro      PT_LOAD FILEHDR PHDRS FLAGS(4); /* R-- */
    text    PT_LOAD FLAGS(5);               /* R-X */
    data    PT_LOAD FLAGS(6);               /* RW- */
    dynamic PT_DYNAMIC FLAGS(6);
    stack   PT_GNU_STACK FLAGS(6);
}

SECTIONS
{
    . = ASTRA_LIBRARY_METADATA;
    .astra_library : {
        __astra_library_start = .;
        KEEP(*(.astra_library))
        __astra_library_end = .;
    } :ro

    .hash : { *(.hash) } :ro
    .dynsym : { *(.dynsym) } :ro
    .dynstr : { *(.dynstr) } :ro
    .rela.dyn : { *(.rela.dyn) } :ro
    .rodata : { *(.rodata .rodata.*) } :ro

    . = ALIGN(ASTRA_PAGE_SIZE);
    .text : { *(.text .text.*) } :text

    . = ASTRA_LIBRARY_EXPORTS;
    .astra_exports : { KEEP(*(.astra_exports)) } :data
    .data.rel.ro : { *(.data.rel.ro .data.rel.ro.*) } :data
    .dynamic : { *(.dynamic) } :data :dynamic
    .got : {
        _GLOBAL_OFFSET_TABLE_ = .;
        *(.got)
    } :data
    .got.plt : { *(.got.plt) } :data
    .data : { *(.data .data.*) *(.sdata .sdata.*) } :data
    .bss (NOLOAD) : {
        *(.sbss .sbss.*) *(.bss .bss.*) *(COMMON)
    } :data

    .plt : { *(.plt .plt.*) } :text
    .rela.plt : { *(.rela.plt) } :ro

    /DISCARD/ : {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame_hdr)
        *(.gnu.attributes)
    }
}

ASSERT(ADDR(.astra_library) == ASTRA_LIBRARY_METADATA,
       "library metadata moved from its ABI offset")
ASSERT(SIZEOF(.astra_library) == ASTRA_LIBRARY_SIZE,
       "library must declare exactly one ASTRA_LIBRARY record")
ASSERT(ADDR(.astra_exports) == ASTRA_LIBRARY_EXPORTS,
       "library export table moved from its ABI offset")
ASSERT(SIZEOF(.astra_exports) != 0,
       "library must declare an export table")
ASSERT(SIZEOF(.plt) == 0, "Astra libraries may not carry a PLT")
ASSERT(SIZEOF(.rela.plt) == 0, "Astra libraries may not lazy-bind")
ASSERT(_GLOBAL_OFFSET_TABLE_ == ADDR(.got),
       "library GOT base moved away from its entries")
