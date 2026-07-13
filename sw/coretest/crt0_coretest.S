| Astra 68 coretest startup.
| Vectors report unexpected exceptions over UART and then stop, so faults do
| not become invisible RTE retry loops.

	.section .vectors, "a", @progbits
	.balign 4
	.long   _stack_top
	.long   _start
	.long   _h_buserr
	.long   _h_recover     | address error
	.long   _h_recover     | illegal instruction
	.long   _h_recover     | zero divide
	.long   _h_recover     | CHK/CHK2
	.long   _h_recover     | TRAPV
	.long   _h_recover     | privilege violation
	.long   _h_recover     | trace
	.long   _h_recover     | line 1010 emulator
	.long   _h_recover     | line 1111 emulator
	.long   _h_default     | reserved
	.long   _h_default     | coprocessor protocol violation
	.long   _h_recover     | format error
	.rept   17
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
	.globl _h_recover
_h_recover:
#ifdef CORETEST_SIM_PROGRESS
	putc_imm 0x68      | 'h': recovery handler entered
#endif
	move.l  #0x01ff9270, %a0
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
	move.l  8(%sp), 36(%a0)
	move.l  12(%sp), 40(%a0)
	move.l  16(%sp), 44(%a0)
	move.l  24(%sp), 52(%a0)    | format A/B data output buffer
	move.l  36(%sp), 60(%a0)    | format B instruction pipe stage-B address
	move.w  6(%sp), %d0
	andi.w  #0x0fff, %d0
	cmp.l   24(%a0), %d0
	bne     1f
	move.l  20(%a0), 2(%sp)
	move.w  #0x2700, (%sp)
	| Some directed tests service a data fault by skipping the failed
	| instruction. Clear DF in the format A/B SSW so RTE does not rerun it.
	tst.l   56(%a0)
	beq     2f
	clr.l   56(%a0)
	andi.w  #0xfeff, 10(%sp)
2:
#ifdef CORETEST_SIM_PROGRESS
	putc_imm 0x72      | 'r': recovery handler is returning
#endif
	rte
1:	putc_imm 0x58      | 'X'
	bra     .
	.globl _h_irq_return
_h_irq_return:
	movem.l %d0/%a0, -(%sp)
	move.l  #0x01ff9270, %a0
	addq.l  #1, (%a0)
	moveq   #0, %d0
	move.w  8(%sp), %d0
	move.l  %d0, 4(%a0)
	moveq   #0, %d0
	move.w  10(%sp), %d0
	swap    %d0
	move.w  12(%sp), %d0
	move.l  %d0, 8(%a0)
	moveq   #0, %d0
	move.w  14(%sp), %d0
	move.l  %d0, 12(%a0)
	move.l  %sp, %d0
	addq.l  #8, %d0
	move.l  %d0, 16(%a0)
	move.l  %d3, 28(%a0)
	moveq   #0, %d0
	move.w  %d2, %d0
	move.l  %d0, 32(%a0)
	move.l  #0xfff00600, %a0
	moveq   #0, %d0
	move.l  %d0, (%a0)
	movem.l (%sp)+, %d0/%a0
	rte
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
