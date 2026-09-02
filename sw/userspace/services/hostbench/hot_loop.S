	.text
	.balign 2
	.globl astra_hostbench_run
	.type astra_hostbench_run, @function

/*
 * Keep one host channel full until iterations commands have completed.
 *
 * uint32_t astra_hostbench_run(header, commands, doorbell, capacity,
 *                              iterations, expected_status);
 *
 * Setup, timing and reporting stay in C.  This is the measured path: publish
 * the producer, ring the userspace doorbell, poll the DMA consumer and verify
 * every completed command before its slot is reused.
 */
astra_hostbench_run:
	movem.l %d2-%d7/%a2-%a3, -(%sp)
	movea.l 36(%sp), %a0		| channel header
	movea.l 40(%sp), %a1		| command array
	movea.l 44(%sp), %a2		| isolated doorbell page
	move.l  48(%sp), %d3		| capacity
	move.l  52(%sp), %d4		| iterations
	move.l  56(%sp), %d7		| expected command status
	move.l  %d3, %d2
	subq.l  #1, %d2		| ring mask
	moveq   #0, %d5		| consumer
	moveq   #0, %d6		| producer

.Lpoll:
	cmp.l   %d4, %d5
	beq.s   .Lsuccess
	move.l  48(%a0), %d1		| observed consumer
	move.l  %d1, %d0
	sub.l   %d5, %d0		| newly completed
	movea.l %d6, %a3
	suba.l  %d5, %a3		| outstanding
	cmpa.l  %d0, %a3
	blo.s   .Lbad_consumer

.Lcollect:
	cmp.l   %d1, %d5
	beq.s   .Lrefill
	move.l  %d5, %d0
	and.l   %d2, %d0
	lsl.l   #8, %d0
	lsl.l   #1, %d0		| slot * 512
	movea.l %a1, %a3
	adda.l  %d0, %a3
	cmp.l   12(%a3), %d7
	bne.s   .Lbad_status
	addq.l  #1, %d5
	bra.s   .Lcollect

.Lrefill:
	cmp.l   %d4, %d6
	beq.s   .Lpoll
	move.l  %d6, %d0
	sub.l   %d5, %d0		| outstanding
	cmp.l   %d3, %d0
	bhi.s   .Lbad_outstanding
	beq.s   .Lpoll
	move.l  %d3, %d1
	sub.l   %d0, %d1		| available
	move.l  %d4, %d0
	sub.l   %d6, %d0		| remaining
	cmp.l   %d0, %d1
	bls.s   .Lpublish
	move.l  %d0, %d1

.Lpublish:
	add.l   %d1, %d6
	move.l  %d6, 36(%a0)		| producer_position
	nop				| MC68030 release fence
	move.l  %d6, 32(%a2)		| ASTRA_HOST_CHANNEL_KICK_OFFSET
	bra.s   .Lpoll

.Lbad_consumer:
	moveq   #1, %d0
	bra.s   .Ldone
.Lbad_status:
	moveq   #2, %d0
	bra.s   .Ldone
.Lbad_outstanding:
	moveq   #3, %d0
	bra.s   .Ldone
.Lsuccess:
	moveq   #0, %d0
.Ldone:
	movem.l (%sp)+, %d2-%d7/%a2-%a3
	rts
	.size astra_hostbench_run, .-astra_hostbench_run

	.section .note.GNU-stack, "", @progbits
