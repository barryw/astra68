# Astra 68 — Harte harness ROM. Reuses the proven selftest scaffolding
# (../boot/crt0.S + ../boot/astra_st.ld) so it boots exactly like the selftest.
CROSS   ?= m68k-linux-gnu-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
CPU_CORE ?= tg68k030_mmu2
CPU_CLK_DIV_BIT ?= 0
SDRAM_ENABLE ?= 1
HDMI_ENABLE ?= 1
UART_BAUD ?= 460800
TARGET_FREQ_MHZ ?= 12.5
PNR_SEED ?= 4
CFLAGS   = -m68020 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
           -Wall -Wextra -I../include -I. -DASTRA_HARTE_UART_BAUD=$(UART_BAUD)
CRT0    ?= ../boot/crt0.S       # override with CRT0=crt0_diag.S for the exception-marker build
LD       = ../boot/astra_st.ld

# Every source whose content ends up in the bitstream (firmware + SoC RTL + CPU core).
# BUILD_ID = first 32 bits of the build configuration and selected source set's
# combined SHA-1. The running harness reports it over UART (CMD_ID), so a flashed
# device can always be matched back to the exact core, clock, and RTL source.
SOC_ID_SRCS = ../../fpga/soc/astra_soc.sv ../../fpga/soc/astra_soc.lpf \
              ../../fpga/soc/uart_tx.sv ../../fpga/soc/uart_rx.sv \
              ../../fpga/soc/uart_rx_fifo.sv ../../fpga/soc/spi_sd.sv \
              ../../fpga/soc/astra_host_async_byte_fifo.sv \
              ../../fpga/soc/astra_host_spi_slave.sv \
              ../../fpga/soc/astra_host_boot.sv \
              ../../fpga/soc/boot_memory_map.sv \
              ../../fpga/soc/ecp5pll.sv ../../fpga/soc/sdram32_cpu_bridge.sv \
              ../../fpga/soc/tg68k_cache_store.sv \
              ../../fpga/soc/sdram32_bist.sv ../../fpga/soc/sdram32_controller.sv \
              ../../fpga/soc/astraea_blitter.sv \
              ../../fpga/soc/thirdparty/core_sdram_axi4/sdram_axi_core.v \
              ../../fpga/soc/post_console.sv \
              ../../fpga/soc/post_fonts.hex ../../fpga/soc/oss_flow/mkbit.sh \
              $(sort $(wildcard ../../fpga/soc/hdmi/*.sv))

ifeq ($(CPU_CORE),tg68k030_mmu2)
CPU_ID_SRCS = $(sort $(wildcard ../../fpga/cpu/tg68k_c_030_mmu2/*.vhd)) \
              ../../fpga/cpu/tg68k030_mmu2_wrap.vhd
else ifeq ($(CPU_CORE),tg68k030)
CPU_ID_SRCS = $(sort $(wildcard ../../fpga/cpu/tg68k_c_030_mmu/*.vhd)) \
              ../../fpga/cpu/tg68k030_wrap.vhd
else ifeq ($(CPU_CORE),tg68k)
CPU_ID_SRCS = $(sort $(wildcard ../../fpga/cpu/tg68k_c/*.vhd)) \
              ../../fpga/cpu/tg68k_wrap.vhd
else ifeq ($(CPU_CORE),wf68k)
CPU_ID_SRCS = $(sort $(wildcard ../../fpga/cpu/wf68k30L/*.vhd)) \
              ../../fpga/cpu/wf68k_wrap.vhd
else
$(error unsupported CPU_CORE '$(CPU_CORE)')
endif

BUILD_CONFIG = CPU_CORE=$(CPU_CORE) CPU_CLK_DIV_BIT=$(CPU_CLK_DIV_BIT) \
               SDRAM_ENABLE=$(SDRAM_ENABLE) HDMI_ENABLE=$(HDMI_ENABLE) \
               UART_BAUD=$(UART_BAUD) \
               TARGET_FREQ_MHZ=$(TARGET_FREQ_MHZ) PNR_SEED=$(PNR_SEED)
ID_SRCS = Makefile harness.c harness_exec.S $(CRT0) ../boot/astra_st.ld \
          ../include/vesta.h $(SOC_ID_SRCS) $(CPU_ID_SRCS)

all: rom_harness.hex

# Regenerated whenever any hashed source changes; harness.c #includes it.
build_id.h: $(ID_SRCS) FORCE
	@ID=`{ printf '%s\n' '$(BUILD_CONFIG)'; cat $(ID_SRCS); } | sha1sum | cut -c1-8`; \
	  printf '#define BUILD_ID 0x%su\n' "$$ID" > $@
	@cat $@

harness.elf: harness.c harness_exec.S build_id.h $(CRT0) $(LD)
	$(CC) $(CFLAGS) -T $(LD) -o $@ $(CRT0) harness_exec.S harness.c

harness.bin: harness.elf
	$(OBJCOPY) -O binary $< $@

rom_harness.hex: harness.bin
	python3 ../boot/bin2hex.py $< $@

# Print the BUILD_ID the current source would produce (no compile).
id:
	@ID=`{ printf '%s\n' '$(BUILD_CONFIG)'; cat $(ID_SRCS); } | sha1sum | cut -c1-8`; \
	  printf '0x%su\n' "$$ID"

config:
	@printf '%s\n' '$(BUILD_CONFIG)'

verify: harness.elf
	@echo "== reset vectors (SP @ +0, PC @ +4) =="; $(OBJDUMP) -s -j .vectors $< | sed -n '3p'
	@echo "== symbols =="; $(OBJDUMP) -t $< | grep -E '_start|kmain|_stack_top'

clean:
	rm -f *.elf *.bin rom_harness.hex build_id.h

.PHONY: all id config verify clean FORCE
FORCE:
