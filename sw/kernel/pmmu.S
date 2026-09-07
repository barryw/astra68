	.section .text, "ax", @progbits

	.globl kernel_pmmu_load_tc
	.type kernel_pmmu_load_tc, @function
kernel_pmmu_load_tc:
	move.l  4(%sp), %d0
	movec   %d0, %tc
	rts

	.globl kernel_pmmu_load_srp
	.type kernel_pmmu_load_srp, @function
kernel_pmmu_load_srp:
	move.l  4(%sp), %d0
	movec   %d0, %srp
	rts

	.globl kernel_pmmu_load_urp
	.type kernel_pmmu_load_urp, @function
kernel_pmmu_load_urp:
	move.l  4(%sp), %d0
	movec   %d0, %urp
	rts

	.globl kernel_pmmu_read_tc
	.type kernel_pmmu_read_tc, @function
kernel_pmmu_read_tc:
	movec   %tc, %d0
	movea.l 4(%sp), %a0
	move.l  %d0, (%a0)
	rts

	.globl kernel_pmmu_read_srp
	.type kernel_pmmu_read_srp, @function
kernel_pmmu_read_srp:
	movec   %srp, %d0
	movea.l 4(%sp), %a0
	move.l  %d0, (%a0)
	rts

	.globl kernel_pmmu_read_urp
	.type kernel_pmmu_read_urp, @function
kernel_pmmu_read_urp:
	movec   %urp, %d0
	movea.l 4(%sp), %a0
	move.l  %d0, (%a0)
	rts

	.globl kernel_pmmu_flush_all
	.type kernel_pmmu_flush_all, @function
kernel_pmmu_flush_all:
	pflusha
	rts

	.globl kernel_pmmu_flush_non_global
	.type kernel_pmmu_flush_non_global, @function
kernel_pmmu_flush_non_global:
	pflushan
	rts

	.globl kernel_pmmu_flush_page
	.type kernel_pmmu_flush_page, @function
kernel_pmmu_flush_page:
	movea.l 4(%sp), %a0
	pflush  (%a0)
	rts

	.globl kernel_pmmu_disable_transparent_translation
	.type kernel_pmmu_disable_transparent_translation, @function
kernel_pmmu_disable_transparent_translation:
	moveq   #0, %d0
	movec   %d0, %itt0
	movec   %d0, %itt1
	movec   %d0, %dtt0
	movec   %d0, %dtt1
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
	cpusha  %bc
	rts

	.globl kernel_cache_read_control
	.type kernel_cache_read_control, @function
kernel_cache_read_control:
	movec   %cacr, %d0
	rts

	.section .note.GNU-stack, "", @progbits
