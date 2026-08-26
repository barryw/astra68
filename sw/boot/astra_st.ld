/* Astra 68 — boot / self-test linker script (matches the current SoC block RAM).
 *
 * Physical decode AS IMPLEMENTED (astra_soc.sv / tb_soc.sv):
 *   RAM   0x01FF8000..0x01FFFFFF (32 KB block RAM)  — sel_ram = cpu_adr[31:15] == 0x3FF
 *   ROM   0xFFE00000..0xFFE7FFFF (512 KB)           — hosted boot ROM
 *   SDRAM 0x02000000..0x03FFFFFF (32 MB high alias) — destructive POST target
 *   MMIO  0xFFF00000..                              — Vesta identity + UART
 * NOTE: RAM base is 0x01FF8000, NOT 0x03FF8000 — the sel_ram field value 0x3FF is
 *   cpu_adr[31:15], so the byte address is 0x3FF<<15 = 0x01FF8000. Runtime data
 *   and the boot stack stay here so POST can destructively test every SDRAM byte.
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
  ROM (rx)  : ORIGIN = 0xFFE00000, LENGTH = 512K
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

  /* Fixed, independently validated firmware-to-kernel handoff record. */
  .boot_info ORIGIN(RAM) (NOLOAD) : {
    . = ALIGN(16);
    KEEP(*(.boot_info))
  } > RAM

  /* .data: resides in ROM (LMA), runs in RAM (VMA); crt0 copies it. */
  .data ORIGIN(RAM) + 0x400 : AT(_etext) {
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

ASSERT(SIZEOF(.boot_info) <= 0x400, "BootInfo exceeds its 1 KiB reservation")
ASSERT(_bss_end <= ORIGIN(RAM) + LENGTH(RAM),
       "system ROM exceeds bootstrap scratch RAM")
