# Astra 68 — Harte harness ROM. Reuses the proven selftest scaffolding
# (../boot/crt0.S + ../boot/astra_st.ld) so it boots exactly like the selftest.
CROSS   ?= m68k-linux-gnu-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
CFLAGS   = -m68020 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
           -Wall -Wextra -I../include -I.
CRT0    ?= ../boot/crt0.S       # override with CRT0=crt0_diag.S for the exception-marker build
LD       = ../boot/astra_st.ld

# Every source whose content ends up in the bitstream (firmware + SoC RTL + CPU core).
# BUILD_ID = first 32 bits of their combined SHA-1. Any change to any of them -> new
# BUILD_ID, which the running harness reports over UART (CMD_ID) so a flashed device can
# always be matched back to exact source. Order is fixed for a reproducible hash.
ID_SRCS = harness.c harness_exec.S $(CRT0) ../boot/astra_st.ld ../include/vesta.h \
          ../../fpga/soc/astra_soc.sv ../../fpga/soc/uart_tx.sv ../../fpga/soc/uart_rx.sv \
          $(sort $(wildcard ../../fpga/cpu/wf68k30L/*.vhd)) ../../fpga/cpu/wf68k_wrap.vhd

all: rom_harness.hex

# Regenerated whenever any hashed source changes; harness.c #includes it.
build_id.h: $(ID_SRCS)
	@printf '#define BUILD_ID 0x%su\n' "`cat $(ID_SRCS) | sha1sum | cut -c1-8`" > $@
	@cat $@

harness.elf: harness.c harness_exec.S build_id.h $(CRT0) $(LD)
	$(CC) $(CFLAGS) -T $(LD) -o $@ $(CRT0) harness_exec.S harness.c

harness.bin: harness.elf
	$(OBJCOPY) -O binary $< $@

rom_harness.hex: harness.bin
	python3 ../boot/bin2hex.py $< $@

# Print the BUILD_ID the current source would produce (no compile).
id:
	@printf '0x%su\n' "`cat $(ID_SRCS) | sha1sum | cut -c1-8`"

verify: harness.elf
	@echo "== reset vectors (SP @ +0, PC @ +4) =="; $(OBJDUMP) -s -j .vectors $< | sed -n '3p'
	@echo "== symbols =="; $(OBJDUMP) -t $< | grep -E '_start|kmain|_stack_top'

clean:
	rm -f *.elf *.bin rom_harness.hex build_id.h
