	.text
	.balign 2
	.globl __m68k_read_tp
	.type __m68k_read_tp, @function
__m68k_read_tp:
	move.l  %a4, %a0
	rts
	.size __m68k_read_tp, .-__m68k_read_tp

	.section .note.GNU-stack, "", @progbits
