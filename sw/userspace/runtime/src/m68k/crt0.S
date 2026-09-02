#include <astra/status.h>
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

	jsr     astra_runtime_initialize
	move.l  %d2, -(%sp)
	jsr     astra_main
	addq.l  #4, %sp
	move.l  %d0, %d1
	bpl     .Lexit
	| A negative return carries the verdict bit, which is the system's alone
	| to set: it is what tells a launcher the process never got to answer.
	| A program that returns one is defective rather than dying, and saying
	| so beats truncating it into a number that looks deliberate.
	move.l  #ASTRA_STATUS_BAD_EXIT, %d1
.Lexit:
#if ASTRA_HOSTED_STARTUP
	move.l  %d1, -(%sp)
	jsr     exit
	addq.l  #4, %sp
#else
	move.l  %d1, -(%sp)
	jsr     astra_runtime_finalize
	addq.l  #4, %sp
	move.l  %d0, %d1
#endif
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     .Lhalt

.Lbad_startup:
	| Was 127, which is Unix's "command not found" wearing the wrong meaning
	| and indistinguishable from a program that legitimately returned 127.
	move.l  #ASTRA_STATUS_NO_STARTUP, %d1
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP

.Lhalt:
	bra     .Lhalt
	.size _start, .-_start

	.section .note.GNU-stack, "", @progbits
