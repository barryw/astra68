# Select an installed freestanding m68k toolchain while preserving CROSS=...
# supplied by callers and release automation.
ifeq ($(origin CROSS), undefined)
ifneq ($(shell command -v m68k-linux-gnu-gcc 2>/dev/null),)
CROSS = m68k-linux-gnu-
else ifneq ($(shell command -v m68k-elf-gcc 2>/dev/null),)
CROSS = m68k-elf-
else
CROSS = m68k-linux-gnu-
endif
endif
