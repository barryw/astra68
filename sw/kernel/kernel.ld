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
  META   (rw)  : ORIGIN = 0x020d4000, LENGTH = 512K
  /*
   * Kernel object tables. A region of their own, above META rather than inside
   * KERNEL, so that raising a limit costs address space here and moves no
   * address anything else depends on -- the trace ring in particular, which
   * crash tooling reads at a fixed address.
   */
  TABLES (rw)  : ORIGIN = 0x02154000, LENGTH = 2M
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

  /*
   * Taken here, before the thread stacks, because those now live in TABLES:
   * the location counter follows them out of this region, and a bootstrap end
   * measured after them would be an address in a different region entirely.
   */
  . = ALIGN(4096);
  _kernel_bootstrap_end = .;

  /*
   * 32 fixed slots: one unmapped 4 KiB guard and one 8 KiB ISP each, 384 KiB
   * in all. They moved out of the kernel region when the slot count rose --
   * they had been 192 KiB of a 512 KiB reservation, and raising the thread
   * ceiling with them still inside would have meant moving the trace ring.
   */
  .kernel_thread_stacks ORIGIN(TABLES) (NOLOAD) : {
    . = ALIGN(4096);
    _kernel_thread_stacks_start = .;
    . += 393216;
    _kernel_thread_stacks_end = .;
  } > TABLES :data

  .kernel_trace ORIGIN(TRACE) (NOLOAD) : {
    . = ALIGN(16);
    _kernel_trace_start = .;
    KEEP(*(.kernel_trace))
    . = ALIGN(16);
    _kernel_trace_end = .;
  } > TRACE :data

  .noinit ORIGIN(META) (NOLOAD) : {
    . = ALIGN(16);
    *(.noinit .noinit.*)
    . = ALIGN(16);
    _kernel_metadata_end = .;
  } > META :data

  .tables (NOLOAD) : {
    . = ALIGN(16);
    _kernel_tables_start = .;
    *(.tables .tables.*)
    . = ALIGN(16);
    _kernel_tables_end = .;
  } > TABLES :data

  _kernel_memory_end = _kernel_tables_end;

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
ASSERT(_kernel_thread_stacks_end - _kernel_thread_stacks_start == 393216,
       "kernel thread stack arena size changed")
ASSERT(_kernel_bootstrap_end <= ORIGIN(KERNEL) + LENGTH(KERNEL),
       "kernel exceeds its reserved bootstrap region")
ASSERT(ORIGIN(TRACE) == ORIGIN(KERNEL) + LENGTH(KERNEL),
       "trace must directly follow the bootstrap region")
ASSERT(_kernel_trace_end - _kernel_trace_start == LENGTH(TRACE),
       "retained trace must occupy exactly 64 KiB")
ASSERT(ORIGIN(TABLES) == ORIGIN(META) + LENGTH(META),
       "object tables must follow the frame metadata")
ASSERT(_kernel_tables_end <= ORIGIN(TABLES) + LENGTH(TABLES),
       "object tables overflow TABLES: raise it with ASTRA_KERNEL_RESERVED_SIZE")
ASSERT(_kernel_metadata_end <= ORIGIN(META) + LENGTH(META),
       "kernel frame metadata exceeds its reserved region")
