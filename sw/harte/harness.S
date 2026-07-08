    .equ UART, 0xFFF00500
    .equ TXST, 0xFFF00504        | bit0 = tx ready
    .equ RXST, 0xFFF00508        | bit0 = rx ready
    .equ RXDT, 0xFFF0050C        | rx data (read clears ready)
    .section .vectors,"a",@progbits
    .balign 4
    .long _stack_top             | vec0 SSP
    .long _start                 | vec1 PC
    .rept 254
    .long _halt
    .endr
    .section .text
    .globl _start
_start:
    move.l  #_stack_top, %sp
    | ---- main loop: read a frame, dispatch ----
main:
    bsr     rx_byte              | expect SYNC 0x55
    cmp.b   #0x55, %d0
    bne     main
    bsr     rx_byte              | LEN -> d7
    move.b  %d0, %d7
    bsr     rx_byte              | CMD -> d6
    move.b  %d0, %d6
    cmp.b   #0x02, %d6           | PING?
    bne     main                 | (RUN=0x01 handled in Task 4)
    bsr     rx_byte              | ping byte -> d5
    move.b  %d0, %d5
    bsr     rx_byte              | cksum (ignore for skeleton)
    | ---- PONG: 0xAA LEN=2 CMD=0x80 byte cksum ----
    move.b  #0xAA, %d0 ; bsr tx_byte
    move.b  #0x02, %d0 ; bsr tx_byte
    move.b  #0x80, %d1 ; move.b %d1,%d0 ; bsr tx_byte
    move.b  %d5, %d0  ; bsr tx_byte
    move.b  #0x80, %d0 ; add.b %d5,%d0 ; bsr tx_byte    | cksum = 0x80+byte
    bra     main
| ---- helpers ----
rx_byte:                          | -> d0.b
    move.l  #RXST, %a0
1:  move.l  (%a0), %d0 ; btst #0, %d0 ; beq 1b
    move.l  #RXDT, %a0 ; move.l (%a0), %d0 ; and.l #0xFF, %d0
    rts
tx_byte:                          | d0.b ->
    move.l  %d0, -(%sp)
    move.l  #TXST, %a0
1:  move.l  (%a0), %d1 ; btst #0, %d1 ; beq 1b
    move.l  (%sp)+, %d0
    move.l  #UART, %a0 ; move.l %d0, (%a0)
    rts
_halt:  stop #0x2700
    bra _halt
    .section .note.GNU-stack,"",@progbits
