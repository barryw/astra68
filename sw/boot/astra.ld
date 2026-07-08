/* Astra 68 — boot ROM linker script.
 *
 * Physical decode:
 *   SDRAM 0x00000000..0x01FFFFFF (32 MB)     — general RAM
 *   ROM   0xFFE00000..0xFFE3FFFF (256 KB)    — boot ROM
 *   MMIO  0xFFF00000..0xFFFFFFFF             — chipset (Vesta/Astraea/Vega/Lyra)
 *
 * At reset the ROM overlay maps this ROM to $0 so the CPU fetches the initial
 * SP ($0) and reset PC ($4) from the vector table below; the reset PC points at
 * the ROM's real address, so execution continues from ROM and the overlay can
 * be disabled. (Overlay hardware lands with the RTL — SPEC open item #1.)
 */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY {
  ROM (rx)  : ORIGIN = 0xFFE00000, LENGTH = 256K
  RAM (rwx) : ORIGIN = 0x00000000, LENGTH = 32M
}

_stack_top = ORIGIN(RAM) + LENGTH(RAM);   /* 0x02000000, top of SDRAM */

SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > ROM

  .text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
    _etext = .;
  } > ROM

  /* .data: resides in ROM (LMA), runs in RAM (VMA); crt0 copies it. */
  .data : AT(_etext) {
    . = ALIGN(4);
    _data_start = .;
    *(.data .data.*)
    . = ALIGN(4);
    _data_end = .;
  } > RAM
  _data_load = LOADADDR(.data);

  .bss (NOLOAD) : {
    . = ALIGN(4);
    _bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _bss_end = .;
  } > RAM

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
