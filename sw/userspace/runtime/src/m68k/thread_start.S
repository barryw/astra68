#include <astra/syscall.h>

	.text
	.balign 2
	.globl astra_thread_start_trampoline
	.type astra_thread_start_trampoline, @function
astra_thread_start_trampoline:
	| D2 points to the runtime-owned copy, released before user entry.
	move.l  %d2, -(%sp)
	bsr.l   astra_thread_start_invoke@PLTPC
	addq.l  #4, %sp
	moveq   #0, %d1
	moveq   #ASTRA_SYSCALL_THREAD_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
.Lhalt:
	bra     .Lhalt
	.size astra_thread_start_trampoline, .-astra_thread_start_trampoline

	.section .note.GNU-stack, "", @progbits
