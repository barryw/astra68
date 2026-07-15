# Astra 68 core validation ROM.
# Directed CPU/SoC tests that must pass before Harte or application ROM work.
CROSS    ?= m68k-linux-gnu-
CC        = $(CROSS)gcc
OBJCOPY   = $(CROSS)objcopy
OBJDUMP   = $(CROSS)objdump

CFLAGS = -m68030 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
         -fno-pic -Wall -Wextra -I../include $(EXTRA_CFLAGS)
LD     = ../boot/astra_st.ld
CRT0   = crt0_coretest.S

all: rom_coretest.hex

coretest.elf: $(CRT0) coretest_movem.S coretest.c $(LD)
	$(CC) $(CFLAGS) -T $(LD) -o $@ $(CRT0) coretest_movem.S coretest.c

coretest.bin: coretest.elf
	$(OBJCOPY) -O binary $< $@

rom_coretest.hex: coretest.bin
	python3 ../boot/bin2hex.py $< $@

disasm: coretest.elf
	$(OBJDUMP) -dr $< | less

check-disasm: coretest.elf
	$(OBJDUMP) -dr $< | grep -E '@\(1ff91[0-9a-f]{2},%d4:l\)'
	@if $(OBJDUMP) -dr $< | grep -Eq 'pmove.*%d[0-7]'; then \
		echo "invalid MC68030 PMOVE data-register EA in coretest" >&2; \
		exit 1; \
	fi

clean:
	rm -f coretest.elf coretest.bin rom_coretest.hex
