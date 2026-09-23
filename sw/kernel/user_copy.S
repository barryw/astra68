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
	cmpi.l  #32, %d0
	blo     .Lfrom_long_tail_test
.Lfrom_long_32:
	.globl  _kernel_copy_from_long_start
_kernel_copy_from_long_start:
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	moves.l (%a0)+, %d1
	move.l  %d1, (%a1)+
	subi.l  #32, %d0
	cmpi.l  #32, %d0
	bhs     .Lfrom_long_32
.Lfrom_long_tail_test:
	cmpi.l  #4, %d0
	blo     .Lfrom_byte_test
.Lfrom_long_tail:
	moves.l (%a0)+, %d1
	.globl  _kernel_copy_from_long_end
_kernel_copy_from_long_end:
	move.l  %d1, (%a1)+
	subq.l  #4, %d0
	cmpi.l  #4, %d0
	bhs     .Lfrom_long_tail
.Lfrom_byte_test:
	tst.l   %d0
	beq     .Lfrom_done
.Lfrom_byte:
	.globl  _kernel_copy_from_byte_start
_kernel_copy_from_byte_start:
	moves.b (%a0)+, %d1
	.globl  _kernel_copy_from_byte_end
_kernel_copy_from_byte_end:
	move.b  %d1, (%a1)+
	subq.l  #1, %d0
	bne     .Lfrom_byte
.Lfrom_done:
	moveq   #KERNEL_USER_COPY_OK, %d0
	bra     .Lfrom_restore
	.globl  _kernel_copy_from_fixup
_kernel_copy_from_fixup:
	moveq   #KERNEL_USER_COPY_BAD_ADDRESS, %d0
.Lfrom_restore:
	move.l  (%sp)+, %d1
	movec   %d1, %sfc
	rts
	.size kernel_user_copy_from_asm, .-kernel_user_copy_from_asm

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
	cmpi.l  #32, %d0
	blo     .Lto_long_tail_test
.Lto_long_32:
	move.l  (%a0)+, %d1
	.globl  _kernel_copy_to_long_start
_kernel_copy_to_long_start:
	moves.l %d1, (%a1)+
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	subi.l  #32, %d0
	cmpi.l  #32, %d0
	bhs     .Lto_long_32
.Lto_long_tail_test:
	cmpi.l  #4, %d0
	blo     .Lto_byte_test
.Lto_long_tail:
	move.l  (%a0)+, %d1
	moves.l %d1, (%a1)+
	.globl  _kernel_copy_to_long_end
_kernel_copy_to_long_end:
	subq.l  #4, %d0
	cmpi.l  #4, %d0
	bhs     .Lto_long_tail
.Lto_byte_test:
	tst.l   %d0
	beq     .Lto_done
.Lto_byte:
	move.b  (%a0)+, %d1
	.globl  _kernel_copy_to_byte_start
_kernel_copy_to_byte_start:
	moves.b %d1, (%a1)+
	.globl  _kernel_copy_to_byte_end
_kernel_copy_to_byte_end:
	subq.l  #1, %d0
	bne     .Lto_byte
.Lto_done:
	moveq   #KERNEL_USER_COPY_OK, %d0
	bra     .Lto_restore
	.globl  _kernel_copy_to_fixup
_kernel_copy_to_fixup:
	moveq   #KERNEL_USER_COPY_BAD_ADDRESS, %d0
.Lto_restore:
	move.l  (%sp)+, %d1
	movec   %d1, %dfc
	rts
	.size kernel_user_copy_to_asm, .-kernel_user_copy_to_asm

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
