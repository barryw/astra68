# Astra 68 — boot ROM build. Bare-metal m68k via gcc-m68k-linux-gnu (freestanding).
CROSS   ?= m68k-linux-gnu-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
CPU_CORE ?= wf68k
CPU_CLK_DIV_BIT ?= 2
SDRAM_ENABLE ?= 1
HDMI_ENABLE ?= 1
TARGET_FREQ_MHZ ?= 12
PNR_SEED ?= 1

CFLAGS  = -m68020 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
          -Wall -Wextra -I../include $(EXTRA_CFLAGS)
ifeq ($(CPU_CORE),tg68k030)
CFLAGS += -DASTRA_CPU_TG68K030=1
endif
LD      = astra_st.ld
LDFLAGS = -T $(LD) -nostdlib -Wl,-Map=astra_boot.map

SOC_ID_SRCS = ../../fpga/soc/astra_soc.sv ../../fpga/soc/astra_soc.lpf \
              ../../fpga/soc/uart_tx.sv ../../fpga/soc/uart_rx.sv \
              ../../fpga/soc/ecp5pll.sv ../../fpga/soc/sdram32_cpu_bridge.sv \
              ../../fpga/soc/tg68k_cache_store.sv \
              ../../fpga/soc/sdram32_bist.sv ../../fpga/soc/sdram32_controller.sv \
              ../../fpga/soc/astraea_blitter.sv \
              ../../fpga/soc/thirdparty/core_sdram_axi4/sdram_axi_core.v \
              ../../fpga/soc/post_console.sv \
              ../../fpga/soc/post_fonts.hex ../../fpga/soc/oss_flow/mkbit.sh \
              $(sort $(wildcard ../../fpga/soc/hdmi/*.sv))

ifeq ($(CPU_CORE),tg68k030)
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
               TARGET_FREQ_MHZ=$(TARGET_FREQ_MHZ) PNR_SEED=$(PNR_SEED)
ID_SRCS = Makefile boot.c crt0.S $(LD) ../include/vesta.h ../include/vega.h \
          ../include/astraea.h \
          $(SOC_ID_SRCS) $(CPU_ID_SRCS)

all: astra_boot.bin

astra_boot.elf: crt0.S boot.c $(LD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S boot.c

astra_boot.bin: astra_boot.elf
	$(OBJCOPY) -O binary $< $@

verify: astra_boot.elf
	@echo "== sections =="; $(OBJDUMP) -h $< | grep -E 'Idx|vectors|text|data|bss'
	@echo "== reset vectors (SP @ +0, PC @ +4) =="; $(OBJDUMP) -s -j .vectors $< | sed -n '3,4p'
	@echo "== symbols =="; $(OBJDUMP) -t $< | grep -E '_start|kmain|_stack_top' | head

id:
	@ID=`{ printf '%s\n' '$(BUILD_CONFIG)'; cat $(ID_SRCS); } | sha1sum | cut -c1-8`; \
	  printf '0x%su\n' "$$ID"

config:
	@printf '%s\n' '$(BUILD_CONFIG)'

clean:
	rm -f *.elf *.bin *.map

.PHONY: all verify id config clean
