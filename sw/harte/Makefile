# Astra 68 — Harte harness ROM. Reuses the proven selftest scaffolding
# (../boot/crt0.S + ../boot/astra_st.ld) so it boots exactly like the selftest.
CROSS   ?= m68k-linux-gnu-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
CFLAGS   = -m68020 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
           -Wall -Wextra -I../include
CRT0     = ../boot/crt0.S
LD       = ../boot/astra_st.ld

all: rom_harness.hex

harness.elf: harness.c $(CRT0) $(LD)
	$(CC) $(CFLAGS) -T $(LD) -o $@ $(CRT0) harness.c

harness.bin: harness.elf
	$(OBJCOPY) -O binary $< $@

rom_harness.hex: harness.bin
	python3 ../boot/bin2hex.py $< $@

verify: harness.elf
	@echo "== reset vectors (SP @ +0, PC @ +4) =="; $(OBJDUMP) -s -j .vectors $< | sed -n '3p'
	@echo "== symbols =="; $(OBJDUMP) -t $< | grep -E '_start|kmain|_stack_top'

clean:
	rm -f *.elf *.bin rom_harness.hex
