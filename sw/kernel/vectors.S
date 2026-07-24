| Kernel-owned vector table and fatal exception entry.

#include "user_copy.h"
#include "context.h"
#include "dispatch.h"

	.section .vectors, "a", @progbits
	.balign 1024
	.globl  _kernel_vectors
	.type   _kernel_vectors, @object
_kernel_vectors:
	.rept   2
	.long   _kernel_exception_entry
	.endr
	.long   _kernel_access_fault_entry
	.rept   44
	.long   _kernel_exception_entry
	.endr
	.long   _kernel_syscall_entry
	.rept   32
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
	ori.w   #0x0700, %sr
	movem.l %d0-%d7/%a0-%a6, -(%sp)
	lea     60(%sp), %a1
	move.l  %usp, %a0
	move.l  %a0, -(%sp)
	move.l  %a1, -(%sp)
	lea     8(%sp), %a0
	move.l  %a0, -(%sp)
	jsr     kernel_exception_entry_dispatch
	lea     12(%sp), %sp
	cmpi.l  #KERNEL_DISPATCH_WORKER, %d0
	beq     _kernel_restore_worker_context
	tst.l   %d0
	bne     _kernel_restore_user_context
	bra     .Lkernel_idle

	.globl  _kernel_access_fault_entry
	.type   _kernel_access_fault_entry, @function
_kernel_access_fault_entry:
	ori.w   #0x0700, %sr
	movem.l %d0-%d7/%a0-%a6, -(%sp)
	lea     60(%sp), %a1
	move.l  %usp, %a0
	move.l  %a0, -(%sp)
	move.l  %a1, -(%sp)
	lea     8(%sp), %a0
	move.l  %a0, -(%sp)
	jsr     kernel_access_entry_dispatch
	lea     12(%sp), %sp
	cmpi.l  #KERNEL_DISPATCH_WORKER, %d0
	beq     _kernel_restore_worker_context
	tst.l   %d0
	bne     _kernel_restore_user_context
	movem.l (%sp)+, %d0-%d7/%a0-%a6

	moveq   #0, %d0
	move.w  6(%sp), %d0
	lsr.w   #8, %d0
	lsr.w   #4, %d0
	cmpi.w  #0x0a, %d0
	beq     .Lcollapse_format_a
	cmpi.w  #0x0b, %d0
	beq     .Lcollapse_format_b
	bra     _kernel_exception_entry

.Lcollapse_format_a:
	lea     24(%sp), %a0
	bra     .Lcollapse_access_frame
.Lcollapse_format_b:
	lea     84(%sp), %a0
.Lcollapse_access_frame:
	move.l  (%sp), %d0
	move.l  %d0, (%a0)
	move.l  4(%sp), %d0
	move.l  %d0, 4(%a0)
	clr.w   6(%a0)
	move.l  %a0, %sp
	moveq   #KERNEL_USER_COPY_BAD_ADDRESS, %d0
	rte

	.globl  _kernel_syscall_entry
	.type   _kernel_syscall_entry, @function
_kernel_syscall_entry:
	ori.w   #0x0700, %sr
	movem.l %d0-%d7/%a0-%a6, -(%sp)
	lea     60(%sp), %a1
	move.l  %usp, %a0
	move.l  %a0, -(%sp)
	move.l  %a1, -(%sp)
	lea     8(%sp), %a0
	move.l  %a0, -(%sp)
	jsr     kernel_syscall_entry_dispatch
	lea     12(%sp), %sp
	cmpi.l  #KERNEL_DISPATCH_WORKER, %d0
	beq     _kernel_restore_worker_context
	tst.l   %d0
	bne     _kernel_restore_user_context
	bra     .Lkernel_idle

	.globl  _kernel_interrupt_entry
	.type   _kernel_interrupt_entry, @function
_kernel_interrupt_entry:
	ori.w   #0x0700, %sr
	movem.l %d0-%d7/%a0-%a6, -(%sp)
	lea     60(%sp), %a1
	move.l  %usp, %a0
	move.l  %a0, -(%sp)
	move.l  %a1, -(%sp)
	lea     8(%sp), %a0
	move.l  %a0, -(%sp)
	jsr     kernel_timer_entry_dispatch
	lea     12(%sp), %sp
	cmpi.l  #KERNEL_DISPATCH_WORKER, %d0
	beq     _kernel_restore_worker_context
	tst.l   %d0
	bne     _kernel_restore_user_context
	movem.l (%sp)+, %d0-%d7/%a0-%a6
	rte

	.globl  kernel_enter_user
	.type   kernel_enter_user, @function
kernel_enter_user:
	move.l  4(%sp), %d0

	.globl  _kernel_restore_user_context
	.type   _kernel_restore_user_context, @function
_kernel_restore_user_context:
	move.l  %d0, %a0
	move.l  KERNEL_CONTEXT_USP_OFFSET(%a0), %a1
	move.l  %a1, %usp
	lea     _kernel_stack_top, %sp
	clr.w   -(%sp)
	move.l  KERNEL_CONTEXT_PC_OFFSET(%a0), -(%sp)
	move.w  KERNEL_CONTEXT_SR_OFFSET(%a0), -(%sp)
	movem.l KERNEL_CONTEXT_D0_OFFSET(%a0), %d0-%d7/%a0-%a6
	rte
.Lkernel_idle:
	lea     _kernel_stack_top, %sp
	moveq   #0, %d0
	move.l  %d0, %a0
	move.l  %a0, %usp
	move.w  #0x2000, %sr
1:	stop    #0x2000
	bra     1b

	.globl  kernel_enable_interrupts
	.type   kernel_enable_interrupts, @function
kernel_enable_interrupts:
	andi.w  #0xf8ff, %sr
	rts

	.globl  kernel_disable_interrupts
	.type   kernel_disable_interrupts, @function
kernel_disable_interrupts:
	ori.w   #0x0700, %sr
	rts

	.section .note.GNU-stack, "", @progbits
