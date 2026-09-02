	/* Shared MC68030 block primitives.  Host builds keep their C sources as
	 * executable reference contracts; Astra target builds select these
	 * post-increment and pre-decrement loops. */

	.section .text.memcpy, "ax", @progbits
	.balign 2
	.globl memcpy
	.type memcpy, @function
memcpy:
	move.l  %d2, -(%sp)
	move.l  8(%sp), %d0
	movea.l %d0, %a1
	movea.l 12(%sp), %a0
	move.l  16(%sp), %d1
	beq     .Lmemcpy_done
	cmpi.l  #4, %d1
	bcs     .Lmemcpy_bytes
	move.l  %a0, %d2
	eor.l   %d0, %d2
	andi.l  #3, %d2
	bne     .Lmemcpy_bytes
.Lmemcpy_align:
	move.l  %a1, %d2
	andi.l  #3, %d2
	beq     .Lmemcpy_32_test
	move.b  (%a0)+, (%a1)+
	subq.l  #1, %d1
	bne     .Lmemcpy_align
	bra     .Lmemcpy_done
.Lmemcpy_32_test:
	cmpi.l  #32, %d1
	bcs     .Lmemcpy_4_test
.Lmemcpy_32:
	move.l  (%a0)+, (%a1)+
	move.l  (%a0)+, (%a1)+
	move.l  (%a0)+, (%a1)+
	move.l  (%a0)+, (%a1)+
	move.l  (%a0)+, (%a1)+
	move.l  (%a0)+, (%a1)+
	move.l  (%a0)+, (%a1)+
	move.l  (%a0)+, (%a1)+
	subi.l  #32, %d1
	cmpi.l  #32, %d1
	bcc     .Lmemcpy_32
.Lmemcpy_4_test:
	cmpi.l  #4, %d1
	bcs     .Lmemcpy_bytes
.Lmemcpy_4:
	move.l  (%a0)+, (%a1)+
	subq.l  #4, %d1
	cmpi.l  #4, %d1
	bcc     .Lmemcpy_4
.Lmemcpy_bytes:
	tst.l   %d1
	beq     .Lmemcpy_done
.Lmemcpy_byte:
	move.b  (%a0)+, (%a1)+
	subq.l  #1, %d1
	bne     .Lmemcpy_byte
.Lmemcpy_done:
	move.l  8(%sp), %d0
	move.l  (%sp)+, %d2
	rts
	.size memcpy, .-memcpy
	.globl kernel_bytes_copy
	.type kernel_bytes_copy, @function
	.set kernel_bytes_copy, memcpy

	.section .text.memset, "ax", @progbits
	.balign 2
	.globl memset
	.type memset, @function
memset:
	move.l  %d2, -(%sp)
	move.l  8(%sp), %d0
	movea.l %d0, %a0
	move.l  12(%sp), %d1
	move.l  16(%sp), %d2
	andi.l  #0xff, %d1
	move.l  %d1, %d0
	lsl.l   #8, %d0
	or.l    %d0, %d1
	move.l  %d1, %d0
	swap    %d0
	or.l    %d0, %d1
	cmpi.l  #4, %d2
	bcs     .Lmemset_bytes
.Lmemset_align:
	move.l  %a0, %d0
	andi.l  #3, %d0
	beq     .Lmemset_32_test
	move.b  %d1, (%a0)+
	subq.l  #1, %d2
	bne     .Lmemset_align
	bra     .Lmemset_done
.Lmemset_32_test:
	cmpi.l  #32, %d2
	bcs     .Lmemset_4_test
.Lmemset_32:
	move.l  %d1, (%a0)+
	move.l  %d1, (%a0)+
	move.l  %d1, (%a0)+
	move.l  %d1, (%a0)+
	move.l  %d1, (%a0)+
	move.l  %d1, (%a0)+
	move.l  %d1, (%a0)+
	move.l  %d1, (%a0)+
	subi.l  #32, %d2
	cmpi.l  #32, %d2
	bcc     .Lmemset_32
.Lmemset_4_test:
	cmpi.l  #4, %d2
	bcs     .Lmemset_bytes
.Lmemset_4:
	move.l  %d1, (%a0)+
	subq.l  #4, %d2
	cmpi.l  #4, %d2
	bcc     .Lmemset_4
.Lmemset_bytes:
	tst.l   %d2
	beq     .Lmemset_done
.Lmemset_byte:
	move.b  %d1, (%a0)+
	subq.l  #1, %d2
	bne     .Lmemset_byte
.Lmemset_done:
	move.l  8(%sp), %d0
	move.l  (%sp)+, %d2
	rts
	.size memset, .-memset

	.section .text.kernel_bytes_clear, "ax", @progbits
	.balign 2
	.globl kernel_bytes_clear
	.type kernel_bytes_clear, @function
kernel_bytes_clear:
	move.l  %d2, -(%sp)
	movea.l 8(%sp), %a0
	moveq   #0, %d1
	move.l  12(%sp), %d2
	cmpi.l  #4, %d2
	bcs     .Lmemset_bytes
	bra     .Lmemset_align
	.size kernel_bytes_clear, .-kernel_bytes_clear

	.section .text.kernel_words_fill, "ax", @progbits
	.balign 2
	.globl kernel_words_fill
	.type kernel_words_fill, @function
kernel_words_fill:
	move.l  %d2, -(%sp)
	movea.l 8(%sp), %a0
	move.l  12(%sp), %d2
	move.l  16(%sp), %d1
	lsl.l   #2, %d2
	bra     .Lmemset_32_test
	.size kernel_words_fill, .-kernel_words_fill

	.section .text.memcmp, "ax", @progbits
	.balign 2
	.globl memcmp
	.type memcmp, @function
memcmp:
	movea.l 4(%sp), %a0
	movea.l 8(%sp), %a1
	move.l  12(%sp), %d0
	moveq   #0, %d1
.Lmemcmp_compare:
	cmpi.l  #32, %d0
	bcs     .Lmemcmp_4_test
.Lmemcmp_32:
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	subi.l  #32, %d0
	cmpi.l  #32, %d0
	bcc     .Lmemcmp_32
.Lmemcmp_4_test:
	cmpi.l  #4, %d0
	bcs     .Lmemcmp_bytes
.Lmemcmp_4:
	cmpm.l  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	subq.l  #4, %d0
	cmpi.l  #4, %d0
	bcc     .Lmemcmp_4
.Lmemcmp_bytes:
	tst.l   %d0
	beq     .Lmemcmp_equal
.Lmemcmp_byte:
	cmpm.b  (%a1)+, (%a0)+
	bne     .Lmemcmp_difference
	subq.l  #1, %d0
	bne     .Lmemcmp_byte
.Lmemcmp_equal:
	tst.b   %d1
	bne     .Lmemcmp_equal_bool
	moveq   #0, %d0
	bra     .Lmemcmp_done
.Lmemcmp_difference:
	bcs     .Lmemcmp_less
	tst.b   %d1
	bne     .Lmemcmp_not_equal
	moveq   #1, %d0
	bra     .Lmemcmp_done
.Lmemcmp_less:
	tst.b   %d1
	bne     .Lmemcmp_not_equal
	moveq   #-1, %d0
	bra     .Lmemcmp_done
.Lmemcmp_equal_bool:
	moveq   #1, %d0
	bra     .Lmemcmp_done
.Lmemcmp_not_equal:
	moveq   #0, %d0
.Lmemcmp_done:
	rts
	.size memcmp, .-memcmp

	.section .text.kernel_bytes_equal, "ax", @progbits
	.balign 2
	.globl kernel_bytes_equal
	.type kernel_bytes_equal, @function
kernel_bytes_equal:
	movea.l 4(%sp), %a0
	movea.l 8(%sp), %a1
	move.l  12(%sp), %d0
	moveq   #1, %d1
	bra     .Lmemcmp_compare
	.size kernel_bytes_equal, .-kernel_bytes_equal

	.section .text.memmove, "ax", @progbits
	.balign 2
	.globl memmove
	.type memmove, @function
memmove:
	move.l  %d2, -(%sp)
	move.l  8(%sp), %d0
	movea.l %d0, %a1
	movea.l 12(%sp), %a0
	move.l  16(%sp), %d1
	beq     .Lmemmove_done
	cmpa.l  %a0, %a1
	beq     .Lmemmove_done
	bcs     .Lmemmove_forward
	move.l  %a0, %d2
	add.l   %d1, %d2
	cmp.l   %d2, %d0
	bcc     .Lmemmove_forward
	adda.l  %d1, %a0
	adda.l  %d1, %a1
	cmpi.l  #4, %d1
	bcs     .Lmemmove_bytes
	move.l  %a0, %d2
	eor.l   %d0, %d2
	andi.l  #3, %d2
	bne     .Lmemmove_bytes
.Lmemmove_align:
	move.l  %a1, %d2
	andi.l  #3, %d2
	beq     .Lmemmove_32_test
	move.b  -(%a0), -(%a1)
	subq.l  #1, %d1
	bne     .Lmemmove_align
	bra     .Lmemmove_done
.Lmemmove_32_test:
	cmpi.l  #32, %d1
	bcs     .Lmemmove_4_test
.Lmemmove_32:
	move.l  -(%a0), -(%a1)
	move.l  -(%a0), -(%a1)
	move.l  -(%a0), -(%a1)
	move.l  -(%a0), -(%a1)
	move.l  -(%a0), -(%a1)
	move.l  -(%a0), -(%a1)
	move.l  -(%a0), -(%a1)
	move.l  -(%a0), -(%a1)
	subi.l  #32, %d1
	cmpi.l  #32, %d1
	bcc     .Lmemmove_32
.Lmemmove_4_test:
	cmpi.l  #4, %d1
	bcs     .Lmemmove_bytes
.Lmemmove_4:
	move.l  -(%a0), -(%a1)
	subq.l  #4, %d1
	cmpi.l  #4, %d1
	bcc     .Lmemmove_4
.Lmemmove_bytes:
	tst.l   %d1
	beq     .Lmemmove_done
.Lmemmove_byte:
	move.b  -(%a0), -(%a1)
	subq.l  #1, %d1
	bne     .Lmemmove_byte
.Lmemmove_done:
	move.l  (%sp)+, %d2
	rts
.Lmemmove_forward:
	move.l  (%sp)+, %d2
	bra     memcpy
	.size memmove, .-memmove

	.section .note.GNU-stack, "", @progbits
