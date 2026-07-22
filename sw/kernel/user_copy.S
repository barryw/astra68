#include "user_copy.h"

	.section .text, "ax", @progbits

	.globl kernel_user_copy_from_asm
	.type kernel_user_copy_from_asm, @function
kernel_user_copy_from_asm:
	movea.l 4(%sp), %a1
	movea.l 8(%sp), %a0
	move.l  12(%sp), %d0
	movec    %sfc, %d1
	move.l   %d1, -(%sp)
	moveq    #1, %d1
	movec    %d1, %sfc
	bra     .Lfrom_test
.Lfrom_loop:
	cmpi.l  #4, %d0
	blo     .Lfrom_byte
	.globl  _kernel_copy_from_long_start
_kernel_copy_from_long_start:
	moves.l (%a0)+, %d1
	.globl  _kernel_copy_from_long_end
_kernel_copy_from_long_end:
	move.l  %d1, (%a1)+
	subq.l  #4, %d0
	bra     .Lfrom_test
.Lfrom_byte:
	.globl  _kernel_copy_from_byte_start
_kernel_copy_from_byte_start:
	moves.b (%a0)+, %d1
	.globl  _kernel_copy_from_byte_end
_kernel_copy_from_byte_end:
	move.b  %d1, (%a1)+
	subq.l  #1, %d0
.Lfrom_test:
	tst.l   %d0
	bne     .Lfrom_loop
	moveq   #KERNEL_USER_COPY_OK, %d0
	bra     .Lfrom_restore
	.globl  _kernel_copy_from_fixup
_kernel_copy_from_fixup:
	moveq   #KERNEL_USER_COPY_BAD_ADDRESS, %d0
.Lfrom_restore:
	move.l  (%sp)+, %d1
	movec   %d1, %sfc
	rts

	.globl kernel_user_copy_to_asm
	.type kernel_user_copy_to_asm, @function
kernel_user_copy_to_asm:
	movea.l 4(%sp), %a1
	movea.l 8(%sp), %a0
	move.l  12(%sp), %d0
	movec    %dfc, %d1
	move.l   %d1, -(%sp)
	moveq    #1, %d1
	movec    %d1, %dfc
	bra     .Lto_test
.Lto_loop:
	cmpi.l  #4, %d0
	blo     .Lto_byte
	move.l  (%a0)+, %d1
	.globl  _kernel_copy_to_long_start
_kernel_copy_to_long_start:
	moves.l %d1, (%a1)+
	.globl  _kernel_copy_to_long_end
_kernel_copy_to_long_end:
	subq.l  #4, %d0
	bra     .Lto_test
.Lto_byte:
	move.b  (%a0)+, %d1
	.globl  _kernel_copy_to_byte_start
_kernel_copy_to_byte_start:
	moves.b %d1, (%a1)+
	.globl  _kernel_copy_to_byte_end
_kernel_copy_to_byte_end:
	subq.l  #1, %d0
.Lto_test:
	tst.l   %d0
	bne     .Lto_loop
	moveq   #KERNEL_USER_COPY_OK, %d0
	bra     .Lto_restore
	.globl  _kernel_copy_to_fixup
_kernel_copy_to_fixup:
	moveq   #KERNEL_USER_COPY_BAD_ADDRESS, %d0
.Lto_restore:
	move.l  (%sp)+, %d1
	movec   %d1, %dfc
	rts

	.section .rodata.uaccess, "a", @progbits
	.balign 4
	.globl _kernel_user_copy_sites_start
_kernel_user_copy_sites_start:
	.long _kernel_copy_from_long_start, _kernel_copy_from_long_end
	.long _kernel_copy_from_fixup, KERNEL_USER_COPY_FROM_USER
	.long _kernel_copy_from_byte_start, _kernel_copy_from_byte_end
	.long _kernel_copy_from_fixup, KERNEL_USER_COPY_FROM_USER
	.long _kernel_copy_to_long_start, _kernel_copy_to_long_end
	.long _kernel_copy_to_fixup, KERNEL_USER_COPY_TO_USER
	.long _kernel_copy_to_byte_start, _kernel_copy_to_byte_end
	.long _kernel_copy_to_fixup, KERNEL_USER_COPY_TO_USER
	.globl _kernel_user_copy_sites_end
_kernel_user_copy_sites_end:

	.section .note.GNU-stack, "", @progbits
