| The one firmware-supplied initial user image, embedded in the ROM file.
| Firmware copies it into RAM and describes it in BootInfo; the kernel owns
| every decision about its contents.

	.section .rodata.user_blob, "a", @progbits
	.balign 16
	.globl  _user_blob_start
_user_blob_start:
	.incbin "../userspace/supervisor/build/m68k/astra_supervisor.elf"
	.globl  _user_blob_end
_user_blob_end:

	.section .note.GNU-stack, "", @progbits
