    .section .text
    .long 0x00300000
    .long 0xffe00100
    .org 0x100
    .if MODE == 1
    move.l #0x4b53444e,0xfff00018
    .elseif MODE == 2
    move.l #0x4b525354,0xfff00018
    .else
    move.l #0x4b314f4b,0xfff00018
    .endif
1:  bra.s 1b
