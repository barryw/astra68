#include <astra/syscall.h>
#include "qualification.h"

	.section .rodata.user_test, "a", @progbits
	.balign 4
	.globl  _k1_survivor_image_start
	.globl  _k1_survivor_image_entry
	.globl  _k2_sibling_image_entry
	.globl  _k1_survivor_image_end
	.globl  _k1_offender_image_start
	.globl  _k1_offender_image_entry
	.globl  _k1_offender_image_end

_k1_survivor_image_start:
_k1_survivor_image_entry:
	movea.l #KERNEL_QUALIFICATION_SHARED_BASE, %a0
	move.l  #0x53555256, (%a0)

.Lmain_wait_ready:
	cmpi.l  #KERNEL_QUALIFICATION_PHASE_READY, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	beq     .Lmain_signal_event
	moveq   #ASTRA_SYSCALL_YIELD, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	bra     .Lmain_wait_ready

.Lmain_signal_event:
	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), %d1
	moveq   #1, %d2
	moveq   #ASTRA_SYSCALL_SIGNAL, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail

.Lmain_wait_cancel:
	cmpi.l  #KERNEL_QUALIFICATION_PHASE_CANCEL, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	beq     .Lmain_cancel
	moveq   #ASTRA_SYSCALL_YIELD, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	bra     .Lmain_wait_cancel

.Lmain_cancel:
	move.l  KERNEL_QUALIFICATION_THREAD_HANDLE_OFFSET(%a0), %d1
	moveq   #ASTRA_SYSCALL_CANCEL_WAIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail

.Lmain_wait_semaphore:
	cmpi.l  #KERNEL_QUALIFICATION_PHASE_SEM, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	beq     .Lmain_signal_semaphore
	moveq   #ASTRA_SYSCALL_YIELD, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	bra     .Lmain_wait_semaphore

.Lmain_signal_semaphore:
	move.l  KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0), %d1
	moveq   #1, %d2
	moveq   #ASTRA_SYSCALL_SIGNAL, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail

.Lmain_wait_close:
	cmpi.l  #KERNEL_QUALIFICATION_PHASE_CLOSE, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	beq     .Lmain_close
	moveq   #ASTRA_SYSCALL_YIELD, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	bra     .Lmain_wait_close

.Lmain_close:
	move.l  KERNEL_QUALIFICATION_CLOSE_EVENT_HANDLE_OFFSET(%a0), %d1
	moveq   #ASTRA_SYSCALL_CLOSE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail

.Lmain_wait_final:
	cmpi.l  #KERNEL_QUALIFICATION_PHASE_FINAL, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	beq     .Lmain_progress
	moveq   #ASTRA_SYSCALL_YIELD, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	bra     .Lmain_wait_final

.Lmain_progress:
	move.l  #KERNEL_QUALIFICATION_CODE_BASE + \
		(.Lk5_exit_thread - _k1_survivor_image_start), %d1
	move.l  #KERNEL_QUALIFICATION_K5_ARGUMENT, %d2
	moveq   #KERNEL_QUALIFICATION_THREAD_PRIORITY, %d3
	moveq   #KERNEL_QUALIFICATION_THREAD_RIGHTS, %d4
	moveq   #ASTRA_SYSCALL_THREAD_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	move.l  %d1, KERNEL_QUALIFICATION_K5_THREAD_HANDLE_OFFSET(%a0)

	move.l  #0x7fffffff, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	cmpi.l  #KERNEL_QUALIFICATION_K5_EXIT_STATUS, %d1
	bne     .Lmain_fail

	move.l  KERNEL_QUALIFICATION_K5_THREAD_HANDLE_OFFSET(%a0), %d1
	moveq   #0, %d2
	moveq   #0, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	cmpi.l  #KERNEL_QUALIFICATION_K5_EXIT_STATUS, %d1
	bne     .Lmain_fail

	move.l  KERNEL_QUALIFICATION_K5_THREAD_HANDLE_OFFSET(%a0), %d1
	moveq   #ASTRA_SYSCALL_CLOSE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail

	move.l  KERNEL_QUALIFICATION_K5_THREAD_HANDLE_OFFSET(%a0), %d1
	moveq   #0, %d2
	moveq   #0, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	cmpi.l  #ASTRA_SYSCALL_INVALID_HANDLE, %d0
	bne     .Lmain_fail
	move.l  #KERNEL_QUALIFICATION_K5_RESULT, \
		KERNEL_QUALIFICATION_K5_RESULT_OFFSET(%a0)

	move.l  #KERNEL_QUALIFICATION_CODE_BASE + \
		(.Lk6_signal_thread - _k1_survivor_image_start), %d1
	move.l  #KERNEL_QUALIFICATION_K6_ARGUMENT, %d2
	moveq   #KERNEL_QUALIFICATION_K6_THREAD_PRIORITY, %d3
	moveq   #KERNEL_QUALIFICATION_THREAD_RIGHTS, %d4
	moveq   #ASTRA_SYSCALL_THREAD_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	move.l  %d1, KERNEL_QUALIFICATION_K6_THREAD_HANDLE_OFFSET(%a0)

	| Block on event + semaphore; the helper signals member 1 and exits.
	subq.l  #8, %sp
	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), (%sp)
	move.l  KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0), 4(%sp)
	move.l  %sp, %d1
	moveq   #2, %d2
	move.l  #ASTRA_DEADLINE_NONE_HI, %d3
	moveq   #-1, %d4
	moveq   #ASTRA_SYSCALL_WAIT_MULTIPLE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	cmpi.l  #1, %d1
	bne     .Lmain_fail
	tst.l   %d2
	bne     .Lmain_fail

	| Make both members ready. Lowest index wins; the semaphore stays ready.
	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), %d1
	moveq   #1, %d2
	moveq   #ASTRA_SYSCALL_SIGNAL, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	move.l  KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0), %d1
	moveq   #1, %d2
	moveq   #ASTRA_SYSCALL_SIGNAL, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	move.l  %sp, %d1
	moveq   #2, %d2
	move.l  #ASTRA_DEADLINE_NONE_HI, %d3
	moveq   #-1, %d4
	moveq   #ASTRA_SYSCALL_WAIT_MULTIPLE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	tst.l   %d1
	bne     .Lmain_fail
	tst.l   %d2
	bne     .Lmain_fail
	move.l  KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0), %d1
	move.l  #ASTRA_DEADLINE_NONE_HI, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail

	| A dead thread is immediately ready and returns its status as detail.
	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), (%sp)
	move.l  KERNEL_QUALIFICATION_K6_THREAD_HANDLE_OFFSET(%a0), 4(%sp)
	move.l  %sp, %d1
	moveq   #2, %d2
	move.l  #ASTRA_DEADLINE_NONE_HI, %d3
	moveq   #-1, %d4
	moveq   #ASTRA_SYSCALL_WAIT_MULTIPLE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	cmpi.l  #1, %d1
	bne     .Lmain_fail
	cmpi.l  #KERNEL_QUALIFICATION_K6_EXIT_STATUS, %d2
	bne     .Lmain_fail

	| With no runnable peer, the supervisor timer expires the whole set.
	move.l  KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0), 4(%sp)
	moveq   #ASTRA_SYSCALL_CLOCK_MONOTONIC, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	add.l   #KERNEL_QUALIFICATION_K6_TIMEOUT_NS, %d2
	moveq   #0, %d3
	addx.l  %d3, %d1
	move.l  %d2, %d4
	move.l  %d1, %d3
	move.l  %sp, %d1
	moveq   #2, %d2
	moveq   #ASTRA_SYSCALL_WAIT_MULTIPLE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	cmpi.l  #ASTRA_SYSCALL_TIMED_OUT, %d0
	bne     .Lmain_fail
	cmpi.l  #ASTRA_WAIT_INDEX_NONE, %d1
	bne     .Lmain_fail
	tst.l   %d2
	bne     .Lmain_fail

	move.l  KERNEL_QUALIFICATION_K6_THREAD_HANDLE_OFFSET(%a0), %d1
	moveq   #ASTRA_SYSCALL_CLOSE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	addq.l  #8, %sp

	| A first-class timer participates in the same bounded wait set.
	moveq   #KERNEL_QUALIFICATION_THREAD_RIGHTS, %d1
	moveq   #ASTRA_SYSCALL_TIMER_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	move.l  %d1, %d7
	moveq   #ASTRA_SYSCALL_CLOCK_MONOTONIC, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	moveq   #0, %d3
	add.l   #KERNEL_QUALIFICATION_K6_TIMER_NS, %d2
	addx.l  %d3, %d1
	move.l  %d2, %d3
	move.l  %d1, %d2
	move.l  %d7, %d1
	moveq   #ASTRA_SYSCALL_TIMER_SET, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	tst.l   %d1
	bne     .Lmain_fail
	subq.l  #8, %sp
	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), (%sp)
	move.l  %d7, 4(%sp)
	move.l  %sp, %d1
	moveq   #2, %d2
	move.l  #ASTRA_DEADLINE_NONE_HI, %d3
	moveq   #-1, %d4
	moveq   #ASTRA_SYSCALL_WAIT_MULTIPLE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	cmpi.l  #1, %d1
	bne     .Lmain_fail
	tst.l   %d2
	bne     .Lmain_fail
	| Expiration remains ready until a rearm or cancellation.
	move.l  %sp, %d1
	moveq   #2, %d2
	moveq   #0, %d3
	moveq   #0, %d4
	moveq   #ASTRA_SYSCALL_WAIT_MULTIPLE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	cmpi.l  #1, %d1
	bne     .Lmain_fail
	move.l  %d7, %d1
	moveq   #ASTRA_SYSCALL_CLOSE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	addq.l  #8, %sp
	move.l  #KERNEL_QUALIFICATION_K6_TIMER_RESULT, \
		KERNEL_QUALIFICATION_K6_TIMER_RESULT_OFFSET(%a0)

	| Release the sibling to observe and close the offender process handle.
	move.l  KERNEL_QUALIFICATION_FINAL_EVENT_HANDLE_OFFSET(%a0), %d1
	moveq   #1, %d2
	moveq   #ASTRA_SYSCALL_SIGNAL, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	cmpi.l  #KERNEL_QUALIFICATION_K6_PROCESS_RESULT, \
		KERNEL_QUALIFICATION_K6_PROCESS_RESULT_OFFSET(%a0)
	bne     .Lmain_fail
	move.l  #KERNEL_QUALIFICATION_K6_RESULT, \
		KERNEL_QUALIFICATION_K6_RESULT_OFFSET(%a0)

	moveq   #0, %d3
.Lmain_progress_loop:
	cmpi.l  #0x53555256, (%a0)
	bne     .Lmain_fault
	addq.l  #1, %d3
	moveq   #ASTRA_SYSCALL_PROGRESS, %d0
	move.l  %d3, %d1
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lmain_fail
	bra     .Lmain_progress_loop

.Lmain_fault:
	movea.l #0x60000000, %a1
	move.l  (%a1), %d4

.Lmain_fail:
	move.l  #KERNEL_QUALIFICATION_PHASE_ERROR, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	moveq   #ASTRA_SYSCALL_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     .Lmain_fail

.Lk5_exit_thread:
	cmpi.l  #KERNEL_QUALIFICATION_K5_ARGUMENT, %d2
	bne     .Lk5_fail
	move.l  #KERNEL_QUALIFICATION_K5_EXIT_STATUS, %d1
	moveq   #ASTRA_SYSCALL_THREAD_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
.Lk5_fail:
	movea.l #KERNEL_QUALIFICATION_SHARED_BASE, %a0
	move.l  #KERNEL_QUALIFICATION_PHASE_ERROR, \
		KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     .Lk5_fail

.Lk6_signal_thread:
	cmpi.l  #KERNEL_QUALIFICATION_K6_ARGUMENT, %d2
	bne     .Lk6_fail
	movea.l #KERNEL_QUALIFICATION_SHARED_BASE, %a0
	move.l  KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0), %d1
	moveq   #1, %d2
	moveq   #ASTRA_SYSCALL_SIGNAL, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lk6_fail
	move.l  #KERNEL_QUALIFICATION_K6_EXIT_STATUS, %d1
	moveq   #ASTRA_SYSCALL_THREAD_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
.Lk6_fail:
	movea.l #KERNEL_QUALIFICATION_SHARED_BASE, %a0
	move.l  #KERNEL_QUALIFICATION_PHASE_ERROR, \
		KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     .Lk6_fail

_k2_sibling_image_entry:
	movea.l #KERNEL_QUALIFICATION_SHARED_BASE, %a0
	move.l  %d2, %d6
	move.l  %d5, KERNEL_QUALIFICATION_THREAD_HANDLE_OFFSET(%a0)

	moveq   #0, %d1
	moveq   #KERNEL_QUALIFICATION_SYNC_RIGHTS, %d2
	moveq   #ASTRA_SYSCALL_EVENT_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail
	move.l  %d1, KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0)

	moveq   #0, %d1
	moveq   #1, %d2
	moveq   #KERNEL_QUALIFICATION_SYNC_RIGHTS, %d3
	moveq   #ASTRA_SYSCALL_SEMAPHORE_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail
	move.l  %d1, KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0)

	moveq   #0, %d1
	moveq   #KERNEL_QUALIFICATION_SYNC_RIGHTS, %d2
	moveq   #ASTRA_SYSCALL_EVENT_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail
	move.l  %d1, KERNEL_QUALIFICATION_CLOSE_EVENT_HANDLE_OFFSET(%a0)

	moveq   #0, %d1
	moveq   #KERNEL_QUALIFICATION_SYNC_RIGHTS, %d2
	moveq   #ASTRA_SYSCALL_EVENT_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail
	move.l  %d1, KERNEL_QUALIFICATION_FINAL_EVENT_HANDLE_OFFSET(%a0)
	move.l  #KERNEL_QUALIFICATION_PHASE_READY, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)

	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), %d1
	move.l  #0x7fffffff, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail

	moveq   #ASTRA_SYSCALL_CLOCK_MONOTONIC, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail
	moveq   #0, %d3
	add.l   #KERNEL_QUALIFICATION_TIMEOUT_NS, %d2
	addx.l  %d3, %d1
	move.l  %d2, %d3
	move.l  %d1, %d2
	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), %d1
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	cmpi.l  #ASTRA_SYSCALL_TIMED_OUT, %d0
	bne     .Lsibling_fail

	move.l  #KERNEL_QUALIFICATION_PHASE_CANCEL, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	move.l  KERNEL_QUALIFICATION_EVENT_HANDLE_OFFSET(%a0), %d1
	move.l  #0x7fffffff, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	cmpi.l  #ASTRA_SYSCALL_CANCELLED, %d0
	bne     .Lsibling_fail

	move.l  #KERNEL_QUALIFICATION_PHASE_SEM, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	move.l  KERNEL_QUALIFICATION_SEMAPHORE_HANDLE_OFFSET(%a0), %d1
	move.l  #0x7fffffff, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail

	move.l  #KERNEL_QUALIFICATION_PHASE_CLOSE, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	move.l  KERNEL_QUALIFICATION_CLOSE_EVENT_HANDLE_OFFSET(%a0), %d1
	move.l  #0x7fffffff, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	cmpi.l  #ASTRA_SYSCALL_CLOSED, %d0
	bne     .Lsibling_fail

	move.l  #KERNEL_QUALIFICATION_PHASE_FINAL, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	move.l  KERNEL_QUALIFICATION_FINAL_EVENT_HANDLE_OFFSET(%a0), %d1
	move.l  #0x7fffffff, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail

	| The injected process handle remains valid after fault teardown.
	subq.l  #4, %sp
	move.l  %d6, (%sp)
	move.l  %sp, %d1
	moveq   #1, %d2
	move.l  #ASTRA_DEADLINE_NONE_HI, %d3
	moveq   #-1, %d4
	moveq   #ASTRA_SYSCALL_WAIT_MULTIPLE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	cmpi.l  #ASTRA_SYSCALL_PEER_DEAD, %d0
	bne     .Lsibling_fail
	tst.l   %d1
	bne     .Lsibling_fail
	tst.l   %d2
	bne     .Lsibling_fail
	move.l  %d6, %d1
	moveq   #ASTRA_SYSCALL_CLOSE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     .Lsibling_fail
	addq.l  #4, %sp
	move.l  #KERNEL_QUALIFICATION_K6_PROCESS_RESULT, \
		KERNEL_QUALIFICATION_K6_PROCESS_RESULT_OFFSET(%a0)

	| Preserve the permanent blocked-thread qualification invariant.
	move.l  KERNEL_QUALIFICATION_FINAL_EVENT_HANDLE_OFFSET(%a0), %d1
	move.l  #ASTRA_DEADLINE_NONE_HI, %d2
	moveq   #-1, %d3
	moveq   #ASTRA_SYSCALL_WAIT_ONE, %d0
	trap    #ASTRA_SYSCALL_TRAP

.Lsibling_fail:
	move.l  #KERNEL_QUALIFICATION_PHASE_ERROR, KERNEL_QUALIFICATION_PHASE_OFFSET(%a0)
	moveq   #ASTRA_SYSCALL_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     .Lsibling_fail
_k1_survivor_image_end:

	.balign 4
_k1_offender_image_start:
_k1_offender_image_entry:
	movea.l #KERNEL_QUALIFICATION_SHARED_BASE, %a0
	move.l  #0x4f464644, (%a0)
	move.l  %d2, %d6
	moveq   #0, %d1
	moveq   #KERNEL_QUALIFICATION_SYNC_RIGHTS, %d2
	moveq   #ASTRA_SYSCALL_EVENT_CREATE, %d0
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     6f
	moveq   #0, %d3
	| The internal thread bootstrap argument was saved from D2 in D6.
	tst.l   %d6
	bne     7f
	move.l  #KERNEL_QUALIFICATION_QUANTUM_SPIN_ITERATIONS, %d5
9:	subq.l  #1, %d5
	bne     9b
7:
4:	cmpi.l  #0x4f464644, (%a0)
	bne     5f
	addq.l  #1, %d3
	moveq   #ASTRA_SYSCALL_PROGRESS, %d0
	move.l  %d3, %d1
	trap    #ASTRA_SYSCALL_TRAP
	tst.l   %d0
	bne     6f
	cmpi.l  #8, %d3
	bne     4b
5:	movea.l #0x60000000, %a1
	move.l  (%a1), %d4
	bra     4b
6:	moveq   #ASTRA_SYSCALL_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra     6b
_k1_offender_image_end:

	.section .note.GNU-stack, "", @progbits
