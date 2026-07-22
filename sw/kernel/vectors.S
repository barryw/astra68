| Kernel-owned vector table and fatal exception entry.

	.section .vectors, "a", @progbits
	.balign 1024
	.globl  _kernel_vectors
	.type   _kernel_vectors, @object
_kernel_vectors:
	.rept   80
	.long   _kernel_exception_entry
	.endr
	.long   _kernel_interrupt_entry
	.rept   175
	.long   _kernel_exception_entry
	.endr
	.size   _kernel_vectors, . - _kernel_vectors

	.section .text, "ax", @progbits
	.globl  _kernel_exception_entry
	.type   _kernel_exception_entry, @function
_kernel_exception_entry:
	movem.l %d0-%d7/%a0-%a6, -(%sp)
	lea     60(%sp), %a0
	move.l  %a0, -(%sp)
	jsr     kernel_exception_panic

1:	stop    #0x2700
	bra     1b

	.globl  _kernel_interrupt_entry
	.type   _kernel_interrupt_entry, @function
_kernel_interrupt_entry:
	movem.l %d0-%d7/%a0-%a6, -(%sp)
	jsr     kernel_interrupt_dispatch
	movem.l (%sp)+, %d0-%d7/%a0-%a6
	rte

	.globl  kernel_enable_interrupts
	.type   kernel_enable_interrupts, @function
kernel_enable_interrupts:
	move.w  #0x2000, %sr
	rts

	.section .note.GNU-stack, "", @progbits
