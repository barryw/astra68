#include <astra/syscall.h>

	.text
	.balign 2
	.globl astra_syscall5
	.type astra_syscall5, @function
astra_syscall5:
	movem.l %d2-%d5, -(%sp)
	move.l  20(%sp), %d0
	move.l  24(%sp), %d1
	move.l  28(%sp), %d2
	move.l  32(%sp), %d3
	move.l  36(%sp), %d4
	move.l  40(%sp), %d5
	trap    #ASTRA_SYSCALL_TRAP
	movea.l 44(%sp), %a0
	move.l  %d0, (%a0)
	move.l  %d1, 4(%a0)
	move.l  %d2, 8(%a0)
	move.l  %d3, 12(%a0)
	movem.l (%sp)+, %d2-%d5
	rts
	.size astra_syscall5, .-astra_syscall5

	.section .note.GNU-stack, "", @progbits
