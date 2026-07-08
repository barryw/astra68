| Astra 68 — DIAGNOSTIC crt0. Same _start as ../boot/crt0.S, but vectors 2/3/4 get distinct
| handlers that emit a marker byte over UART then spin (no RTE, so no fault-retry loop):
|   vec 2 bus error      -> '2'
|   vec 3 address error  -> '3'
|   vec 4 illegal instr  -> '4'
|   any other (5..255)   -> 'E'
| Tells us exactly what executing the relocated instruction from RAM triggers. Throwaway.

	.section .vectors, "a", @progbits
	.balign 4
	.long   _stack_top          | 0: SSP
	.long   _start              | 1: reset PC
	.long   _h_buserr           | 2: bus error
	.long   _h_adrerr           | 3: address error
	.long   _h_illegal          | 4: illegal instruction
	.rept   251
	.long   _default_handler    | 5..255
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

	| ---- diagnostic exception handlers: emit a byte, then spin ----
_h_buserr:
	move.l  #0xFFF00504, %a0
11:	move.l  (%a0), %d0 ; btst #0, %d0 ; beq 11b
	move.l  #0xFFF00500, %a1 ; move.l #0x32, (%a1)     | '2'
12:	bra     12b
_h_adrerr:
	move.l  #0xFFF00504, %a0
13:	move.l  (%a0), %d0 ; btst #0, %d0 ; beq 13b
	move.l  #0xFFF00500, %a1 ; move.l #0x33, (%a1)     | '3'
14:	bra     14b
_h_illegal:
	move.l  #0xFFF00504, %a0
15:	move.l  (%a0), %d0 ; btst #0, %d0 ; beq 15b
	move.l  #0xFFF00500, %a1 ; move.l #0x34, (%a1)     | '4'
16:	bra     16b
	.globl  _default_handler
_default_handler:
	move.l  #0xFFF00504, %a0
17:	move.l  (%a0), %d0 ; btst #0, %d0 ; beq 17b
	move.l  #0xFFF00500, %a1 ; move.l #0x45, (%a1)     | 'E'
18:	bra     18b

	.section .note.GNU-stack, "", @progbits
