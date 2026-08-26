# Astra 68 — boot ROM build. Bare-metal m68k via freestanding GCC.
include ../../mk/m68k-cross.mk
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump
HOST_CC ?= cc
CPU_CLK_DIV_BIT ?= 0
SDRAM_ENABLE ?= 1
HDMI_ENABLE ?= 1
SD_BOOT_ENABLE ?= 1
ASTRA_HOST_ENABLE ?= 1
ROM_WORDS ?= 1024
UART_MONITOR ?= 0
UART_BAUD ?= 115200
TARGET_FREQ_MHZ ?= 12.5
PNR_SEED ?= 4
PNR_PLACER ?= heap
PNR_ROUTER ?= router1
PNR_TIMING_WEIGHT ?= 20
PNR_HEAP_CELL_PLACEMENT_TIMEOUT ?= 0
PNR_TIMING_RIPUP ?= 0
PNR_TIMING_ALLOW_FAIL ?= 0
PNR_ROUTER2_ALT_WEIGHTS ?= 0
PNR_THREADS ?=
SYNTH_ECP5_FLAGS ?= -abc2
SYNTH_KEEP_HIERARCHY ?=
ASTRA_FLOORPLAN_MODE ?= critical
ASTRA_FLOORPLAN_ENFORCE ?= host_io,astraea_blitter,astraea_blitter_cdc,astraea_blitter_control
RESOURCE_PROFILE ?= complete_chipset
ROM_VERSION ?= 0.3
BUILD_INFO = build/rom_build_info.h
ROM_PAYLOADS = build/rom_payloads.h
KERNEL_PAYLOAD = build/astra_kernel.lz4
USER_PAYLOAD = build/astra_supervisor.lz4
SPLASH_SOURCE = assets/astra_boot_splash.png
SPLASH_FONT = ../../fpga/soc/post_fonts.hex
SPLASH_PACKER = pack_boot_splash.py
SPLASH_PAYLOAD = assets/astra_boot_splash.pal8.lz4
KERNEL_DIR = ../kernel
KERNEL_BIN = $(KERNEL_DIR)/astra_kernel.bin
USER_IMAGE_DIR = ../userspace/supervisor
USER_IMAGE = $(USER_IMAGE_DIR)/build/m68k/astra_supervisor.image.elf
KERNEL_VERSION ?= 0.1.0-dev
KERNEL_PANIC_SELFTEST ?= 0
KERNEL_SCHED_TRACE ?= 0
KERNEL_SOAK_SELFTEST ?= 0
KERNEL_K1_QUALIFICATION ?= 0
# Passed through to the kernel, because this target rebuilds it. Without the
# pass-through a `make ASTRA_BUILD=debug` in sw/kernel was silently undone the
# moment the ROM was built: the sub-make ran with the default and the image
# shipped a release kernel under a debug name.
ASTRA_BUILD ?= release

CFLAGS  = -m68030 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
          -Wa,--noexecstack -Wall -Wextra \
          -I../include -I../../ndk/include -Ibuild $(EXTRA_CFLAGS)
LD      = astra_st.ld
LDFLAGS = -T $(LD) -nostdlib -Wl,--build-id=none,-Map=astra_boot.map

SOC_ID_SRCS = ../../fpga/soc/astra_soc.sv ../../fpga/soc/astra_soc.lpf \
              ../../fpga/soc/uart_tx.sv ../../fpga/soc/uart_rx.sv \
              ../../fpga/soc/uart_rx_fifo.sv ../../fpga/soc/spi_sd.sv \
              ../../fpga/soc/astra_front_panel.sv ../../fpga/soc/vesta_irq_timer.sv \
              ../../fpga/soc/vesta_bus_fault.sv \
              ../../fpga/soc/astra_host_async_byte_fifo.sv \
              ../../fpga/soc/astra_host_spi_slave.sv \
              ../../fpga/soc/astra_async_fifo.sv \
              ../../fpga/soc/astra_host_runtime.sv \
              ../../fpga/soc/astra_host_service.sv \
              ../../fpga/soc/boot_memory_map.sv \
              ../../fpga/soc/ecp5pll.sv ../../fpga/soc/sdram32_cpu_bridge.sv \
              ../../fpga/soc/tg68k_cache_store.sv \
              ../../fpga/soc/sdram32_bist.sv ../../fpga/soc/sdram32_controller.sv \
              ../../fpga/soc/astraea_blitter.sv \
              ../../fpga/soc/astraea_pixel_port.sv \
              ../../fpga/soc/astraea_draw.sv \
              ../../fpga/soc/astraea_copper.sv ../../fpga/soc/astraea_chip.sv \
              ../../fpga/soc/vega_sprite_builder.sv \
              ../../fpga/soc/vega_video.sv \
              ../../fpga/soc/thirdparty/core_sdram_axi4/sdram_axi_core.v \
              ../../fpga/soc/post_console.sv \
              ../../fpga/soc/post_fonts.hex ../../fpga/soc/oss_flow/mkbit.sh \
              ../../fpga/soc/oss_flow/astra_clocks.sdc \
              ../../fpga/soc/oss_flow/place_hdmi_serializer.py \
              ../../fpga/soc/oss_flow/prepare_route_input.py \
              ../../fpga/soc/oss_flow/check_timing.py \
              ../../fpga/soc/oss_flow/check_resource_budget.py \
              ../../fpga/soc/oss_flow/resource_budgets.json \
              $(sort $(wildcard ../../fpga/soc/hdmi/*.sv))

CPU_ID_SRCS = $(sort $(wildcard ../../fpga/cpu/tg68k_c_030_mmu2/*.vhd)) \
              ../../fpga/cpu/tg68k030_mmu2_wrap.vhd

STAGE0_ID_SRCS = ../stage0/Makefile ../stage0/crt0.S ../stage0/stage0.c \
                 ../stage0/stage0.ld ../stage0/fat_loader.c \
                 ../stage0/fat_loader.h

USER_IMAGE_ID_SRCS = $(USER_IMAGE_DIR)/Makefile \
                     $(sort $(wildcard $(USER_IMAGE_DIR)/src/*.c \
                             $(USER_IMAGE_DIR)/include/*.h)) \
                     ../userspace/runtime/Makefile \
                     ../userspace/runtime/astra_user.ld \
                     $(sort $(wildcard ../userspace/runtime/src/*.c \
                             ../userspace/runtime/src/m68k/*.S \
                             ../userspace/runtime/include/astra/*.h))

KERNEL_ID_SRCS = $(KERNEL_DIR)/Makefile \
                 $(sort $(wildcard $(KERNEL_DIR)/*.c $(KERNEL_DIR)/*.h \
                         $(KERNEL_DIR)/*.S $(KERNEL_DIR)/*.ld \
                         $(KERNEL_DIR)/*.py))

BUILD_CONFIG = CPU=TG68K030_MMU2 CPU_CLK_DIV_BIT=$(CPU_CLK_DIV_BIT) \
               SDRAM_ENABLE=$(SDRAM_ENABLE) HDMI_ENABLE=$(HDMI_ENABLE) \
               SD_BOOT_ENABLE=$(SD_BOOT_ENABLE) \
               ASTRA_HOST_ENABLE=$(ASTRA_HOST_ENABLE) ROM_WORDS=$(ROM_WORDS) \
               UART_MONITOR=$(UART_MONITOR) UART_BAUD=$(UART_BAUD) \
               TARGET_FREQ_MHZ=$(TARGET_FREQ_MHZ) PNR_SEED=$(PNR_SEED) \
               PNR_PLACER=$(PNR_PLACER) PNR_ROUTER=$(PNR_ROUTER) \
               PNR_TIMING_WEIGHT=$(PNR_TIMING_WEIGHT) \
               PNR_HEAP_CELL_PLACEMENT_TIMEOUT=$(PNR_HEAP_CELL_PLACEMENT_TIMEOUT) \
               PNR_TIMING_RIPUP=$(PNR_TIMING_RIPUP) \
               PNR_TIMING_ALLOW_FAIL=$(PNR_TIMING_ALLOW_FAIL) \
               PNR_ROUTER2_ALT_WEIGHTS=$(PNR_ROUTER2_ALT_WEIGHTS) \
               PNR_THREADS=$(if $(strip $(PNR_THREADS)),$(PNR_THREADS),default) \
               PNR_FLOW=split SYNTH_ECP5_FLAGS=$(SYNTH_ECP5_FLAGS) \
               SYNTH_KEEP_HIERARCHY=$(if $(strip $(SYNTH_KEEP_HIERARCHY)),$(SYNTH_KEEP_HIERARCHY),none) \
               ASTRA_FLOORPLAN_MODE=$(ASTRA_FLOORPLAN_MODE) \
               ASTRA_FLOORPLAN_ENFORCE=$(ASTRA_FLOORPLAN_ENFORCE) \
               RESOURCE_PROFILE=$(RESOURCE_PROFILE) STAGE0_BOOT_BACKEND=host \
               ROM_VERSION=$(ROM_VERSION) KERNEL_VERSION=$(KERNEL_VERSION) \
               KERNEL_PANIC_SELFTEST=$(KERNEL_PANIC_SELFTEST) \
               KERNEL_SCHED_TRACE=$(KERNEL_SCHED_TRACE) \
               KERNEL_SOAK_SELFTEST=$(KERNEL_SOAK_SELFTEST) \
               KERNEL_K1_QUALIFICATION=$(KERNEL_K1_QUALIFICATION)
ID_SRCS = Makefile ../../mk/m68k-cross.mk gen_build_info.py pack_payload.py \
          boot.c crt0.S $(LD) \
          kernel_blob.S user_blob.S splash_blob.S splash.c splash.h lz4_legacy.c \
          lz4_legacy.h $(SPLASH_PACKER) $(SPLASH_SOURCE) assets/README.md \
          ../common/boot_contract.c ../include/astra/boot.h \
          ../include/astra/syscall.h ../include/astra/crc32.h \
          ../common/crc32.c ../include/vesta.h ../include/vega.h \
          ../include/astraea.h ../include/ohci.h \
          ../../ndk/include/astra/attributes.h \
          ../../ndk/include/astra/types.h \
          ../../ndk/include/astra/resource.h \
          ../../ndk/include/astra/front_panel.h \
          ../../ndk/src/front_panel.c \
          ../../ndk/src/internal/memory_map.h \
          ../../ndk/src/internal/mmio.h \
          $(KERNEL_ID_SRCS) $(USER_IMAGE_ID_SRCS) \
          $(SOC_ID_SRCS) $(CPU_ID_SRCS) $(STAGE0_ID_SRCS)

# Arty loads this image into QEMU host memory. The SDRAM-backed ULX3S package
# remains explicit because its independent stage-0 contract is still 256 KiB.
all: astra_boot.bin

legacy-rom: astra68.rom

$(SPLASH_PAYLOAD): $(SPLASH_SOURCE) $(SPLASH_FONT) $(SPLASH_PACKER)
	python3 $(SPLASH_PACKER) --image $(SPLASH_SOURCE) \
		--font $(SPLASH_FONT) --output $@

$(BUILD_INFO): FORCE gen_build_info.py
	@mkdir -p $(dir $@)
	@python3 gen_build_info.py --version "$(ROM_VERSION)" --output "$@"

$(KERNEL_BIN): FORCE $(KERNEL_ID_SRCS) ../common/boot_contract.c \
               ../include/astra/boot.h ../include/astra/syscall.h
	$(MAKE) -C $(KERNEL_DIR) all CROSS=$(CROSS) \
		KERNEL_VERSION=$(KERNEL_VERSION) PANIC_SELFTEST=$(KERNEL_PANIC_SELFTEST) \
		SCHED_TRACE=$(KERNEL_SCHED_TRACE) SOAK_SELFTEST=$(KERNEL_SOAK_SELFTEST) \
		K1_QUALIFICATION=$(KERNEL_K1_QUALIFICATION) \
		ASTRA_BUILD=$(ASTRA_BUILD)

$(USER_IMAGE): FORCE $(USER_IMAGE_ID_SRCS)
	$(MAKE) -C $(USER_IMAGE_DIR) all CROSS=$(CROSS)

# Both images ship compressed and firmware decodes them into their load
# addresses. Sizes and CRCs are built in as constants rather than a parsed
# container header.
$(ROM_PAYLOADS) $(KERNEL_PAYLOAD) $(USER_PAYLOAD) &: pack_payload.py \
                pack_boot_splash.py $(KERNEL_BIN) $(USER_IMAGE)
	@mkdir -p build
	python3 pack_payload.py \
		--payload KERNEL=$(KERNEL_BIN)=$(KERNEL_PAYLOAD) \
		--payload USER=$(USER_IMAGE)=$(USER_PAYLOAD) \
		--header $(ROM_PAYLOADS)

astra_boot.elf: crt0.S kernel_blob.S user_blob.S splash_blob.S splash.c \
                splash.h lz4_legacy.c lz4_legacy.h \
                $(SPLASH_PAYLOAD) boot.c \
                ../common/boot_contract.c ../../ndk/src/front_panel.c \
                ../include/ohci.h \
                $(LD) $(BUILD_INFO) $(ROM_PAYLOADS) \
                $(KERNEL_PAYLOAD) $(USER_PAYLOAD)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S kernel_blob.S user_blob.S \
		splash_blob.S splash.c lz4_legacy.c boot.c \
		../common/boot_contract.c ../common/crc32.c \
		../../ndk/src/front_panel.c

astra_boot.bin: astra_boot.elf
	$(OBJCOPY) -O binary $< $@

astra68.rom: astra_boot.bin package_rom.py
	python3 package_rom.py $< $@

verify: astra_boot.elf
	@echo "== sections =="; $(OBJDUMP) -h $< | grep -E 'Idx|vectors|text|data|bss'
	@echo "== reset vectors (SP @ +0, PC @ +4) =="; $(OBJDUMP) -s -j .vectors $< | sed -n '3,4p'
	@echo "== symbols =="; $(OBJDUMP) -t $< | grep -E '_start|kmain|_stack_top|_kernel_blob|_splash_blob|boot_kernel_handoff' | head
	@echo "== embedded kernel =="; $(OBJDUMP) -h $(KERNEL_DIR)/astra_kernel.elf | \
		grep -E 'Idx|text.entry|vectors|text|data|bss|kernel_stack'
	@bytes=$$(wc -c < $(KERNEL_BIN)); \
	  test $$bytes -le $$((0x80000)); \
	  printf 'kernel payload: %s bytes\n' "$$bytes"

test_lz4_legacy: test_lz4_legacy.c lz4_legacy.c lz4_legacy.h \
                 ../common/crc32.c ../include/astra/crc32.h \
                 $(SPLASH_PAYLOAD)
	$(HOST_CC) -std=c11 -O2 -Wall -Wextra -Werror -o $@ \
		-I../include test_lz4_legacy.c lz4_legacy.c ../common/crc32.c

test: test_lz4_legacy
	./test_lz4_legacy
	PYTHONPATH=../.. python3 -m pytest tests -q

id:
	@ID=`{ printf '%s\n' '$(BUILD_CONFIG)'; cat $(ID_SRCS); } | sha1sum | cut -c1-8`; \
	  printf '0x%su\n' "$$ID"

config:
	@printf '%s\n' '$(BUILD_CONFIG)'

clean:
	rm -f *.elf *.bin *.map *.rom test_lz4_legacy $(SPLASH_PAYLOAD)
	rm -rf build
	$(MAKE) -C $(KERNEL_DIR) clean

FORCE:

.PHONY: all legacy-rom verify id config test clean FORCE
