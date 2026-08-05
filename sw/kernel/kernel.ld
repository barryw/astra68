/* Separately linked Astra kernel bootstrap image. */
OUTPUT_FORMAT("elf32-m68k")
OUTPUT_ARCH(m68k)
ENTRY(_kernel_entry)

PHDRS {
  text PT_LOAD FLAGS(5); /* R-X */
  data PT_LOAD FLAGS(6); /* RW- */
}

MEMORY {
  KERNEL (rwx) : ORIGIN = 0x02044000, LENGTH = 512K
  TRACE  (rw)  : ORIGIN = 0x020c4000, LENGTH = 64K
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

  .noinit (NOLOAD) : {
    . = ALIGN(16);
    *(.noinit .noinit.*)
    . = ALIGN(16);
  } > KERNEL :data

  .kernel_stack (NOLOAD) : {
    . = ALIGN(4096);
    _kernel_stack_guard = .;
    . += 4096;
    _kernel_stack_bottom = .;
    . += 8192;
    _kernel_stack_top = .;
  } > KERNEL :data

  .kernel_worker_stack (NOLOAD) : {
    . = ALIGN(4096);
    _kernel_worker_stack_guard = .;
    . += 4096;
    _kernel_worker_stack_bottom = .;
    . += 8192;
    _kernel_worker_stack_top = .;
  } > KERNEL :data

  /* 16 fixed slots: one unmapped 4 KiB guard and one 8 KiB ISP each. */
  .kernel_thread_stacks (NOLOAD) : {
    . = ALIGN(4096);
    _kernel_thread_stacks_start = .;
    . += 196608;
    _kernel_thread_stacks_end = .;
  } > KERNEL :data

  . = ALIGN(4096);
  _kernel_bootstrap_end = .;

  .kernel_trace ORIGIN(TRACE) (NOLOAD) : {
    . = ALIGN(16);
    _kernel_trace_start = .;
    KEEP(*(.kernel_trace))
    . = ALIGN(16);
    _kernel_trace_end = .;
  } > TRACE :data

  _kernel_memory_end = _kernel_trace_end;

  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}

ASSERT(_kernel_entry == ORIGIN(KERNEL), "kernel entry must begin the image")
ASSERT((_bss_start & 15) == 0 && (_bss_end & 15) == 0,
       "kernel BSS bounds must remain 16-byte aligned")
ASSERT((_kernel_stack_guard & 4095) == 0,
       "kernel stack guard must remain page aligned")
ASSERT((_kernel_worker_stack_guard & 4095) == 0,
       "kernel worker stack guard must remain page aligned")
ASSERT(_kernel_worker_stack_top - _kernel_worker_stack_bottom == 8192,
       "kernel worker stack size changed")
ASSERT((_kernel_thread_stacks_start & 4095) == 0,
       "kernel thread stack arena must remain page aligned")
ASSERT(_kernel_thread_stacks_end - _kernel_thread_stacks_start == 196608,
       "kernel thread stack arena size changed")
ASSERT(_kernel_bootstrap_end <= ORIGIN(KERNEL) + LENGTH(KERNEL),
       "kernel exceeds its reserved bootstrap region")
ASSERT(ORIGIN(TRACE) == ORIGIN(KERNEL) + LENGTH(KERNEL),
       "trace must directly follow the bootstrap region")
ASSERT(_kernel_trace_end - _kernel_trace_start == LENGTH(TRACE),
       "retained trace must occupy exactly 64 KiB")
