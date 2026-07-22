#include <astra/syscall.h>

	.section .rodata.user_test, "a", @progbits
	.balign 4
	.globl  _k1_survivor_image_start
	.globl  _k1_survivor_image_entry
	.globl  _k1_survivor_image_end
	.globl  _k1_offender_image_start
	.globl  _k1_offender_image_entry
	.globl  _k1_offender_image_end

_k1_survivor_image_start:
_k1_survivor_image_entry:
	movea.l #0x70000000, %a0
	move.l  #0x53555256, (%a0)
	moveq   #0, %d3
1:	cmpi.l  #0x53555256, (%a0)
	bne     2f
	addq.l  #1, %d3
	moveq   #ASTRA_SYSCALL_PROGRESS, %d0
	move.l  %d3, %d1
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     3f
	bra     1b
2:	movea.l #0x60000000, %a1
	move.l  (%a1), %d4
3:	moveq   #ASTRA_SYSCALL_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     3b
_k1_survivor_image_end:

	.balign 4
_k1_offender_image_start:
_k1_offender_image_entry:
	movea.l #0x70000000, %a0
	move.l  #0x4f464644, (%a0)
	moveq   #0, %d3
4:	cmpi.l  #0x4f464644, (%a0)
	bne     5f
	addq.l  #1, %d3
	moveq   #ASTRA_SYSCALL_PROGRESS, %d0
	move.l  %d3, %d1
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     6f
	cmpi.l  #8, %d3
	bne     4b
5:	movea.l #0x60000000, %a1
	move.l  (%a1), %d4
	bra     4b
6:	moveq   #ASTRA_SYSCALL_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     6b
_k1_offender_image_end:

	.section .note.GNU-stack, "", @progbits
