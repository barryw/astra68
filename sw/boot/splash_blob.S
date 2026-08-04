| Compressed INDEX8 splash, BGRA palette, and 8x16 MASK1 rescue font.

	.section .rodata.splash_blob, "a", @progbits
	.balign 16
	.globl  _splash_blob_start
_splash_blob_start:
	.incbin "assets/astra_boot_splash.pal8.lz4"
	.globl  _splash_blob_end
_splash_blob_end:

	.section .note.GNU-stack, "", @progbits
