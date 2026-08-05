#include <astra/syscall.h>

	.text
	.balign 2
	.globl _start
	.type _start, @function
_start:
	move.l  %d2, -(%sp)
	jsr     astra_startup_validate
	addq.l  #4, %sp
	tst.l   %d0
	beq     .Lbad_startup

	move.l  %d2, -(%sp)
	jsr     astra_main
	addq.l  #4, %sp
	move.l  %d0, %d1
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     .Lhalt

.Lbad_startup:
	moveq   #127, %d1
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP

.Lhalt:
	bra     .Lhalt
	.size _start, .-_start
