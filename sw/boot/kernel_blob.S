| Separately linked kernel payload embedded in the single FAT-visible ROM file.
| LZ4-legacy compressed by pack_payload.py; firmware decodes and CRC-checks it.

	.section .rodata.kernel_blob, "a", @progbits
	.balign 16
	.globl  _kernel_blob_start
_kernel_blob_start:
	.incbin "build/astra_kernel.lz4"
	.globl  _kernel_blob_end
_kernel_blob_end:

	.section .note.GNU-stack, "", @progbits
