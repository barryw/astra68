| Assembly helpers for coretest cases that intentionally clobber the full
| Harte register set. Keep these out of inline C asm so A6/frame handling is
| explicit and ABI-safe.

    .section .text
    .globl coretest_movem_fullshape_asm
    .type coretest_movem_fullshape_asm, @function
coretest_movem_fullshape_asm:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w  #0x001b,%ccr
    movem.l 0x01ff9340,%d0-%d7/%a0-%a6
    movem.l %d0-%d7/%a0-%a6,0x01ff9380
    move.w  %sr,%d0
    move.l  %d0,0x01ff93bc
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

    .section .note.GNU-stack, "", @progbits
