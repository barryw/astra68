| Astra 68 immutable stage-0 bootstrap.

	.section .vectors, "a", @progbits
	.balign 4
	.long   _stack_top
	.long   _start
	.rept   254
	.long   _default_handler
	.endr

	.section .text
	.globl  _start
	.type   _start, @function
_start:
	move.l  #_stack_top, %sp

	lea     _data_load, %a0
	lea     _data_start, %a1
	lea     _data_end, %a2
1:	cmpa.l  %a2, %a1
	beq     2f
	move.b  (%a0)+, (%a1)+
	bra     1b

2:	lea     _bss_start, %a0
	lea     _bss_end, %a1
3:	cmpa.l  %a1, %a0
	beq     4f
	clr.b   (%a0)+
	bra     3b

4:	jsr     stage0_main
5:	stop    #0x2700
	bra     5b

	.globl  stage0_handoff
	.type   stage0_handoff, @function
stage0_handoff:
	move.l  8(%sp), %a0
	move.l  4(%sp), %sp
	jmp     (%a0)

	.globl  _default_handler
	.type   _default_handler, @function
_default_handler:
	stop    #0x2700
	bra     _default_handler

	.section .note.GNU-stack, "", @progbits
