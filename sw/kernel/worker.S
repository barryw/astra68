| Stackful supervisor worker coroutine for the MC68030.
|
| The worker runs with S=1/M=1 on MSP. Asynchronous interrupts therefore use
| the processor's documented MSP format-0 plus ISP format-1 frame sequence.
| D2-D7/A2-A6 are the m68k ABI callee-saved set. The saved MSP points at those
| 11 longwords followed by the return PC for kernel_worker_switch_to_user.

#include "context.h"

	.section .text, "ax", @progbits
	.globl  kernel_worker_switch_to_user
	.type   kernel_worker_switch_to_user, @function
kernel_worker_switch_to_user:
	| C ABI: context is 4(%sp) on entry. Preserve the ABI callee-saved set on
	| MSP and suspend this call until _kernel_restore_worker_context. D0/A0/A1,
	| SR, and the active A7 bank are clobbered; interrupts remain masked.
	movem.l %d2-%d7/%a2-%a6, -(%sp)
	move.l  %sp, kernel_worker_saved_msp
	move.l  48(%sp), %d0
	| Switch from MSP to the dedicated ISP before constructing the user RTE
	| frame. The saved MSP remains untouched until the next worker handoff.
	move.w  #0x2700, %sr
	jmp     _kernel_restore_user_context

	.globl  _kernel_restore_worker_context
	.type   _kernel_restore_worker_context, @function
_kernel_restore_worker_context:
	| Entry is always on ISP with interrupts masked. Discard that completed
	| exception path, load the worker's MSP, restore D2-D7/A2-A6, and resume
	| the suspended C call. D0/A0, SR, ISP, and MSP are clobbered by design.
	addq.l  #1, kernel_worker_restore_entries
	lea     _kernel_stack_top, %sp
	move.l  kernel_worker_saved_msp, %a0
	movec   %a0, %msp
	move.w  #0x3700, %sr
	movem.l (%sp)+, %d2-%d7/%a2-%a6
	rts

	.globl  kernel_worker_arch_wait
	.type   kernel_worker_arch_wait, @function
kernel_worker_arch_wait:
	| STOP in master mode at IPL0. The interrupt return restores MSP and
	| execution continues here; mask again before C examines worker state.
	| No registers are changed other than SR; the interrupt may clobber only
	| registers covered by its normal entry/exit contract.
	stop    #0x3000
	ori.w   #0x0700, %sr
	rts

	.section .note.GNU-stack, "", @progbits
