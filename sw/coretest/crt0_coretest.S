| Astra 68 coretest startup.
| Vectors report unexpected exceptions over UART and then stop, so faults do
| not become invisible RTE retry loops.

	.section .vectors, "a", @progbits
	.balign 4
	.long   _stack_top
	.long   _start
	.long   _h_buserr
	.long   _h_adrerr
	.long   _h_illegal
	.rept   27
	.long   _h_default
	.endr
	.long   _h_trap0
	.rept   223
	.long   _h_default
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

4:	jsr     kmain
5:	stop    #0x2700
	bra     5b

	| UART helpers for exception stubs. A0/D0 are scratch.
	.macro putc_imm ch
	move.l  #0xFFF00504, %a0
9:	move.l  (%a0), %d0
	btst    #0, %d0
	beq     9b
	move.l  #0xFFF00500, %a0
	move.l  #\ch, (%a0)
	.endm

_h_buserr:
	putc_imm 0x32      | '2'
	bra     .
_h_adrerr:
	putc_imm 0x33      | '3'
	bra     .
_h_illegal:
	putc_imm 0x34      | '4'
	bra     .
	.globl _h_default
_h_default:
	putc_imm 0x45      | 'E'
	bra     .
_h_trap0:
	move.l  #0x01ff91f0, %a0
	addq.l  #1, (%a0)
	moveq   #0, %d0
	move.w  (%sp), %d0
	move.l  %d0, 4(%a0)
	moveq   #0, %d0
	move.w  2(%sp), %d0
	swap    %d0
	move.w  4(%sp), %d0
	move.l  %d0, 8(%a0)
	moveq   #0, %d0
	move.w  6(%sp), %d0
	move.l  %d0, 12(%a0)
	move.l  %sp, 16(%a0)
	rte

	.section .note.GNU-stack, "", @progbits
