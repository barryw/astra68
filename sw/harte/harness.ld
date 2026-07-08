OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY {
  ROM (rx)  : ORIGIN = 0xFFE00000, LENGTH = 8K
  RAM (rwx) : ORIGIN = 0x01FF8000, LENGTH = 32K
}

_stack_top = 0x01FFC000;      /* supervisor stack top; case area above */
_harness_pc = 0x01FFC000;     /* relocated instruction goes here */

SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > ROM

  .text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > ROM

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
