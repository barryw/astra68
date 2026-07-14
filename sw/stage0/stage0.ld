/* Immutable SD bootstrap: reset vectors and loader in 16 KiB of block RAM. */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY {
  BOOT (rx) : ORIGIN = 0xFFFC0000, LENGTH = 8K
  RAM (rwx) : ORIGIN = 0x01FF8000, LENGTH = 32K
}

_stack_top = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS {
  .vectors : { KEEP(*(.vectors)) } > BOOT

  .text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
    _etext = .;
  } > BOOT

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

ASSERT(_data_load + SIZEOF(.data) <= ORIGIN(BOOT) + LENGTH(BOOT),
       "stage0 exceeds its 8 KiB bootstrap ROM")
ASSERT(_bss_end <= ORIGIN(RAM) + LENGTH(RAM),
       "stage0 exceeds bootstrap scratch RAM")
