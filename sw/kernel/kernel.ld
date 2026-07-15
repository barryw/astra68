/* Separately linked Astra kernel bootstrap image. */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_kernel_entry)

PHDRS {
  text PT_LOAD FLAGS(5); /* R-X */
  data PT_LOAD FLAGS(6); /* RW- */
}

MEMORY {
  KERNEL (rwx) : ORIGIN = 0x02010000, LENGTH = 512K
}

SECTIONS {
  _kernel_image_start = ORIGIN(KERNEL);

  .text.entry ORIGIN(KERNEL) : {
    KEEP(*(.text.entry))
  } > KERNEL :text

  .vectors : ALIGN(1024) {
    KEEP(*(.vectors))
  } > KERNEL :text

  .text : {
    *(.text .text.*)
    *(.rodata .rodata.*)
    . = ALIGN(4);
  } > KERNEL :text

  .data : {
    *(.data .data.*)
    . = ALIGN(4);
    _kernel_file_end = .;
  } > KERNEL :data

  .bss (NOLOAD) : {
    . = ALIGN(16);
    _bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    . = ALIGN(16);
    _bss_end = .;
  } > KERNEL :data

  .kernel_stack (NOLOAD) : {
    . = ALIGN(4096);
    _kernel_stack_bottom = .;
    . += 8192;
    _kernel_stack_top = .;
  } > KERNEL :data

  . = ALIGN(4096);
  _kernel_memory_end = .;

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(_kernel_entry == ORIGIN(KERNEL), "kernel entry must begin the image")
ASSERT(_kernel_memory_end <= ORIGIN(KERNEL) + LENGTH(KERNEL),
       "kernel exceeds its reserved bootstrap region")
