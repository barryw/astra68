M68K_TOOL_PREFIX ?= $(shell if command -v m68k-elf-as >/dev/null 2>&1; then \
	echo m68k-elf; else echo m68k-linux-gnu; fi)
M68K_AS = $(M68K_TOOL_PREFIX)-as
M68K_ASFLAGS = -march=68040 -g --gdwarf-sections -I$(CURDIR)
M68K_LD = $(M68K_TOOL_PREFIX)-ld
M68K_LDFLAGS = -Ttext 0x10000 --oformat binary

export

default: all

all:
	@$(MAKE) -C mc68000 all
	@$(MAKE) -C mc68040 all

clean:
	@$(MAKE) -C mc68000 clean
	@$(MAKE) -C mc68040 clean

.PHONY: clean all
