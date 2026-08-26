#include <astra/syscall.h>

	.text
	.balign 2
	.globl astra_thread_start_trampoline
	.type astra_thread_start_trampoline, @function
astra_thread_start_trampoline:
	| D2 is the kernel's one thread argument: a persistent AstraThreadStart.
	movea.l %d2, %a0
	move.l  4(%a0), -(%sp)
	movea.l (%a0), %a0
	jsr     (%a0)
	addq.l  #4, %sp
	moveq   #0, %d1
	moveq   #ASTRA_SYSCALL_THREAD_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
.Lhalt:
	bra     .Lhalt
	.size astra_thread_start_trampoline, .-astra_thread_start_trampoline

	.section .note.GNU-stack, "", @progbits
