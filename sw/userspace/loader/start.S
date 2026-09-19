#include <astra/status.h>
#include <astra/syscall.h>
#include <astra/library.h>
#include <astra/tls.h>

	.section .text.startup, "ax", @progbits
	.balign 2
	.globl _start
	.type _start, @function
_start:
	/* Validate every table and destination before the first write. All bounds
	 * are hidden linker symbols reached PC-relatively, so this call itself
	 * needs no relocation. */
	move.l  #ASTRA_DYNAMIC_IMAGE_BASE, %a0
	lea     __astra_image_end(%pc), %a1
	move.l  %a1, -(%sp)
	lea     __astra_writable_end(%pc), %a1
	move.l  %a1, -(%sp)
	lea     __astra_writable_start(%pc), %a1
	move.l  %a1, -(%sp)
	lea     __astra_dynamic_end(%pc), %a1
	move.l  %a1, -(%sp)
	lea     __astra_dynamic_start(%pc), %a1
	move.l  %a1, -(%sp)
	move.l  %a0, -(%sp)
	bsr.l   astra_loader_self_relocate
	adda.l  #24, %sp
	tst.l   %d0
	beq.s   .Lbad_relocation
	move.l  %d2, -(%sp)
	bsr.l   astra_loader_main
	addq.l  #4, %sp
	move.l  %d0, %d1
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
.Lhalt:
	bra.s   .Lhalt

.Lbad_relocation:
	move.l  #ASTRA_STATUS_INVALID, %d1
	moveq   #ASTRA_SYSCALL_PROCESS_EXIT, %d0
	trap    #ASTRA_SYSCALL_TRAP
	bra.s   .Lhalt
	.size _start, .-_start

	.text
	.balign 2
	.globl astra_loader_enter_program
	.type astra_loader_enter_program, @function
astra_loader_enter_program:
	move.l  4(%sp), %a0
	/* d2 carries Astra's startup pointer into a program, but it is also a
	 * callee-saved register in the m68k ABI.  Preserve the loader's value so
	 * returning programs cannot corrupt astra_loader_main's live state. */
	move.l  %d2, -(%sp)
	move.l  12(%sp), %d2
	jsr     (%a0)
	move.l  (%sp)+, %d2
	rts
	.size astra_loader_enter_program, .-astra_loader_enter_program

	.section .note.GNU-stack, "", @progbits
