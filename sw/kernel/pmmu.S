	.section .text, "ax", @progbits

	.globl kernel_pmmu_load_tc
	.type kernel_pmmu_load_tc, @function
kernel_pmmu_load_tc:
	movea.l 4(%sp), %a0
	pmove   (%a0), %tc
	rts

	.globl kernel_pmmu_load_srp
	.type kernel_pmmu_load_srp, @function
kernel_pmmu_load_srp:
	movea.l 4(%sp), %a0
	pmove   (%a0), %srp
	rts

	.globl kernel_pmmu_load_crp
	.type kernel_pmmu_load_crp, @function
kernel_pmmu_load_crp:
	movea.l 4(%sp), %a0
	pmove   (%a0), %crp
	rts

	.globl kernel_pmmu_load_tt0
	.type kernel_pmmu_load_tt0, @function
kernel_pmmu_load_tt0:
	movea.l 4(%sp), %a0
	pmove   (%a0), %tt0
	rts

	.globl kernel_pmmu_load_tt1
	.type kernel_pmmu_load_tt1, @function
kernel_pmmu_load_tt1:
	movea.l 4(%sp), %a0
	pmove   (%a0), %tt1
	rts

	.globl kernel_pmmu_read_tc
	.type kernel_pmmu_read_tc, @function
kernel_pmmu_read_tc:
	movea.l 4(%sp), %a0
	pmove   %tc, (%a0)
	rts

	.globl kernel_pmmu_read_srp
	.type kernel_pmmu_read_srp, @function
kernel_pmmu_read_srp:
	movea.l 4(%sp), %a0
	pmove   %srp, (%a0)
	rts

	.globl kernel_pmmu_read_crp
	.type kernel_pmmu_read_crp, @function
kernel_pmmu_read_crp:
	movea.l 4(%sp), %a0
	pmove   %crp, (%a0)
	rts

	.globl kernel_pmmu_flush_all
	.type kernel_pmmu_flush_all, @function
kernel_pmmu_flush_all:
	pflusha
	rts

	.globl kernel_pmmu_set_user_function_codes
	.type kernel_pmmu_set_user_function_codes, @function
kernel_pmmu_set_user_function_codes:
	moveq   #1, %d0
	movec   %d0, %sfc
	movec   %d0, %dfc
	rts

	.globl kernel_cache_invalidate_all
	.type kernel_cache_invalidate_all, @function
kernel_cache_invalidate_all:
	movec   %cacr, %d0
	ori.l   #0x00000008, %d0
	movec   %d0, %cacr
	andi.l  #0xfffffff7, %d0
	ori.l   #0x00000800, %d0
	movec   %d0, %cacr
	rts

	.globl kernel_cache_read_control
	.type kernel_cache_read_control, @function
kernel_cache_read_control:
	movec   %cacr, %d0
	rts

	.section .note.GNU-stack, "", @progbits
