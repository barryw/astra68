/* Astra 68 — self-test / bring-up linker script (matches the current SoC block RAM).
 *
 * Physical decode AS IMPLEMENTED (astra_soc.sv / tb_soc.sv):
 *   RAM   0x01FF8000..0x01FFFFFF (32 KB block RAM)  — sel_ram = cpu_adr[31:15] == 0x3FF
 *   ROM   0xFFE00000..0xFFE3FFFF (256 KB)           — boot ROM
 *   MMIO  0xFFF00500..                              — Vesta UART
 * NOTE: RAM base is 0x01FF8000, NOT 0x03FF8000 — the sel_ram field value 0x3FF is
 *   cpu_adr[31:15], so the byte address is 0x3FF<<15 = 0x01FF8000. (SDRAM at 0x0 is
 *   a future SPEC item; until it exists, RAM/stack must live in this 32 KB window.)
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
  RAM (rwx) : ORIGIN = 0x01FF8000, LENGTH = 32K
}

_stack_top = ORIGIN(RAM) + LENGTH(RAM);   /* 0x02000000, one past top of the 32 KB block RAM */

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
