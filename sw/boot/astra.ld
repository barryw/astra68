/* Astra 68 — boot ROM linker script.
 *
 * Boot address windows:
 *   scratch/initial stack below 0x02000000
 *   guest RAM at 0x02000000..0x09FFFFFF (128 MiB)
 *   ROM at 0xFFE00000..0xFFE7FFFF (512 KiB)
 *   MMIO at 0xFFF00000..0xFFFFFFFF
 *
 * At reset the ROM overlay maps this ROM to $0 so the CPU fetches the initial
 * SP ($0) and reset PC ($4) from the vector table below; the reset PC points at
 * the ROM's real address, so execution continues from ROM and the overlay can
 * be disabled.
 */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_start)

MEMORY {
  ROM (rx)  : ORIGIN = 0xFFE00000, LENGTH = 512K
  BOOT (rwx) : ORIGIN = 0x00000000, LENGTH = 0x02000000
}

_stack_top = ORIGIN(BOOT) + LENGTH(BOOT);

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
  } > BOOT
  _data_load = LOADADDR(.data);

  .bss (NOLOAD) : {
    . = ALIGN(4);
    _bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(4);
    _bss_end = .;
  } > BOOT

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
