| Separately linked kernel payload embedded in the single FAT-visible ROM file.

	.section .rodata.kernel_blob, "a", @progbits
	.balign 16
	.globl  _kernel_blob_start
_kernel_blob_start:
	.incbin "../kernel/astra_kernel.bin"
	.globl  _kernel_blob_end
_kernel_blob_end:

	.section .note.GNU-stack, "", @progbits
