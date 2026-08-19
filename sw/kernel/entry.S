| Astra 68 kernel entry. Firmware enters with D0=A68K magic and A0=BootInfo.

	.section .text.entry, "ax", @progbits
	.globl  _kernel_entry
	.type   _kernel_entry, @function
_kernel_entry:
	move.w  #0x2700, %sr
	move.l  %d0, %d4
	move.l  %a0, %a4

	| Clear kernel-owned BSS while the firmware stack is still active.
	lea     _bss_start, %a0
	lea     _bss_end, %a1
1:	cmpa.l  %a1, %a0
	beq     2f
	| The linker aligns both bounds to 16 bytes. Clear one aligned burst per
	| loop so fixed kernel tables do not turn simulation or boot into a byte
	| store/branch benchmark.
	clr.l   (%a0)+
	clr.l   (%a0)+
	clr.l   (%a0)+
	clr.l   (%a0)+
	bra     1b

	| The object tables are NOLOAD in a region of their own, so neither the
	| ROM nor the loop above touches them -- and POST leaves its own patterns
	| in RAM, so what a pool finds there is 0x5AA55A5C rather than zero. Every
	| pool that moved out of BSS was written against BSS's guarantee: an area
	| whose generation survives initialisation as a memory-test pattern fails
	| the pool's first validity check, and the initial image is refused with
	| nothing pointing at the cause. Same 16-byte aligned burst as above; the
	| linker aligns both bounds.
2:	lea     _kernel_tables_start, %a0
	lea     _kernel_tables_end, %a1
4:	cmpa.l  %a1, %a0
	beq     5f
	clr.l   (%a0)+
	clr.l   (%a0)+
	clr.l   (%a0)+
	clr.l   (%a0)+
	bra     4b

5:	lea     _kernel_stack_top, %sp
	lea     _kernel_vectors, %a0
	movec   %a0, %vbr

	| C arguments are pushed right-to-left.
	move.l  %a4, -(%sp)
	move.l  %d4, -(%sp)
	jsr     kernel_main
	addq.l  #8, %sp

3:	stop    #0x2700
	bra     3b

	.globl  kernel_read_vbr
	.type   kernel_read_vbr, @function
kernel_read_vbr:
	movec   %vbr, %d0
	rts

	.section .note.GNU-stack, "", @progbits
