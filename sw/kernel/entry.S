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

2:	lea     _kernel_stack_top, %sp
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
