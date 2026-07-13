| Astra 68 — crt0 startup (GNU as, m68k syntax).
| Vector table (fetched at $0 via the reset ROM overlay) + C runtime bring-up.

	.section .vectors, "a", @progbits
	.balign 4
	.long   _stack_top          | vec 0: initial SP
	.long   _start              | vec 1: reset PC
	.rept   254
	.long   _default_handler    | vec 2..255: catch-all
	.endr

	.section .text
	.globl  _start
	.type   _start, @function
_start:
	move.l  #_stack_top, %sp        | stack stays in BRAM while POST tests SDRAM

#ifdef ASTRA_CPU_TG68K030
#ifndef ASTRA_BOOT_CACR
#define ASTRA_BOOT_CACR 0x1111
#endif
	move.l  #ASTRA_BOOT_CACR, %d0     | CACR.IBE|DBE|IE|DE; override for diagnostics
	movec   %d0, %cacr
#endif

	| copy .data from ROM (LMA) to RAM (VMA)
	lea     _data_load, %a0
	lea     _data_start, %a1
	lea     _data_end, %a2
1:	cmpa.l  %a2, %a1
	beq     2f
	move.b  (%a0)+, (%a1)+
	bra     1b

	| zero .bss
2:	lea     _bss_start, %a0
	lea     _bss_end, %a1
3:	cmpa.l  %a1, %a0
	beq     4f
	clr.b   (%a0)+
	bra     3b

4:	jsr     kmain                   | into C
5:	stop    #0x2700                 | halt (supervisor, IPL7)
	bra     5b

	.globl  _default_handler
	.type   _default_handler, @function
_default_handler:
	rte

	.section .note.GNU-stack, "", @progbits   | mark stack non-executable (silences ld)
