# Astra 68 — boot ROM build. Bare-metal m68k via gcc-m68k-linux-gnu (freestanding).
CROSS   ?= m68k-linux-gnu-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
CPU_CLK_DIV_BIT ?= 2
SDRAM_ENABLE ?= 1
HDMI_ENABLE ?= 1
TARGET_FREQ_MHZ ?= 12
PNR_SEED ?= 1
ROM_VERSION ?= 0.3
BUILD_INFO = build/rom_build_info.h
KERNEL_DIR = ../kernel
KERNEL_BIN = $(KERNEL_DIR)/astra_kernel.bin
KERNEL_VERSION ?= 0.1.0-dev
KERNEL_PANIC_SELFTEST ?= 0

CFLAGS  = -m68030 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
          -Wall -Wextra \
          -I../include -I../../ndk/include -Ibuild $(EXTRA_CFLAGS)
LD      = astra_st.ld
LDFLAGS = -T $(LD) -nostdlib -Wl,--build-id=none,-Map=astra_boot.map

SOC_ID_SRCS = ../../fpga/soc/astra_soc.sv ../../fpga/soc/astra_soc.lpf \
              ../../fpga/soc/uart_tx.sv ../../fpga/soc/uart_rx.sv \
              ../../fpga/soc/uart_rx_fifo.sv ../../fpga/soc/spi_sd.sv \
              ../../fpga/soc/astra_front_panel.sv \
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

CPU_ID_SRCS = $(sort $(wildcard ../../fpga/cpu/tg68k_c_030_mmu2/*.vhd)) \
              ../../fpga/cpu/tg68k030_mmu2_wrap.vhd

BUILD_CONFIG = CPU=TG68K030_MMU2 CPU_CLK_DIV_BIT=$(CPU_CLK_DIV_BIT) \
               SDRAM_ENABLE=$(SDRAM_ENABLE) HDMI_ENABLE=$(HDMI_ENABLE) \
               TARGET_FREQ_MHZ=$(TARGET_FREQ_MHZ) PNR_SEED=$(PNR_SEED) \
               ROM_VERSION=$(ROM_VERSION)
ID_SRCS = Makefile gen_build_info.py boot.c crt0.S $(LD) \
          kernel_blob.S ../common/boot_contract.c ../include/astra/boot.h \
          ../include/vesta.h ../include/vega.h \
          ../include/astraea.h \
          ../../ndk/include/astra/attributes.h \
          ../../ndk/include/astra/types.h \
          ../../ndk/include/astra/resource.h \
          ../../ndk/include/astra/front_panel.h \
          ../../ndk/src/front_panel.c \
          ../../ndk/src/internal/memory_map.h \
          ../../ndk/src/internal/mmio.h \
          $(sort $(wildcard $(KERNEL_DIR)/*.c $(KERNEL_DIR)/*.S \
                    $(KERNEL_DIR)/*.ld $(KERNEL_DIR)/*.py $(KERNEL_DIR)/Makefile)) \
          $(SOC_ID_SRCS) $(CPU_ID_SRCS)

all: astra_boot.bin astra68.rom

$(BUILD_INFO): FORCE gen_build_info.py
	@mkdir -p $(dir $@)
	@python3 gen_build_info.py --version "$(ROM_VERSION)" --output "$@"

$(KERNEL_BIN): $(KERNEL_DIR)/Makefile $(KERNEL_DIR)/kernel.ld \
               $(KERNEL_DIR)/entry.S $(KERNEL_DIR)/vectors.S \
               $(KERNEL_DIR)/kernel.c $(KERNEL_DIR)/gen_build_info.py \
               ../common/boot_contract.c ../include/astra/boot.h
	$(MAKE) -C $(KERNEL_DIR) all CROSS=$(CROSS) \
		KERNEL_VERSION=$(KERNEL_VERSION) PANIC_SELFTEST=$(KERNEL_PANIC_SELFTEST)

astra_boot.elf: crt0.S kernel_blob.S boot.c ../common/boot_contract.c \
                ../../ndk/src/front_panel.c $(LD) $(BUILD_INFO) $(KERNEL_BIN)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S kernel_blob.S boot.c \
		../common/boot_contract.c ../../ndk/src/front_panel.c

astra_boot.bin: astra_boot.elf
	$(OBJCOPY) -O binary $< $@

astra68.rom: astra_boot.bin package_rom.py
	python3 package_rom.py $< $@

verify: astra_boot.elf
	@echo "== sections =="; $(OBJDUMP) -h $< | grep -E 'Idx|vectors|text|data|bss'
	@echo "== reset vectors (SP @ +0, PC @ +4) =="; $(OBJDUMP) -s -j .vectors $< | sed -n '3,4p'
	@echo "== symbols =="; $(OBJDUMP) -t $< | grep -E '_start|kmain|_stack_top|_kernel_blob|boot_kernel_handoff' | head
	@echo "== embedded kernel =="; $(OBJDUMP) -h $(KERNEL_DIR)/astra_kernel.elf | \
		grep -E 'Idx|text.entry|vectors|text|data|bss|kernel_stack'
	@bytes=$$(wc -c < $(KERNEL_BIN)); \
	  test $$bytes -le $$((0x80000)); \
	  printf 'kernel payload: %s bytes\n' "$$bytes"

id:
	@ID=`{ printf '%s\n' '$(BUILD_CONFIG)'; cat $(ID_SRCS); } | sha1sum | cut -c1-8`; \
	  printf '0x%su\n' "$$ID"

config:
	@printf '%s\n' '$(BUILD_CONFIG)'

clean:
	rm -f *.elf *.bin *.map *.rom
	rm -rf build
	$(MAKE) -C $(KERNEL_DIR) clean

FORCE:

.PHONY: all verify id config clean FORCE
