include ../../mk/m68k-cross.mk
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
HOST_CC ?= cc
BOOT_BACKEND ?= direct

CFLAGS = -m68030 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
         -fdata-sections -ffunction-sections -Wa,--noexecstack \
         -Wall -Wextra -I../include
LDFLAGS = -T stage0.ld -nostdlib \
          -Wl,--build-id=none,--gc-sections,-Map=stage0.map

ifeq ($(BOOT_BACKEND),host)
CFLAGS += -DASTRA_HOST_BOOT=1
STAGE0_C_SOURCES = stage0.c
STAGE0_MAX_BYTES = 4096
else ifeq ($(BOOT_BACKEND),direct)
STAGE0_C_SOURCES = stage0.c fat_loader.c
STAGE0_MAX_BYTES = 8192
else
$(error BOOT_BACKEND must be direct or host)
endif

all: stage0.hex

stage0.elf: crt0.S $(STAGE0_C_SOURCES) fat_loader.h stage0.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S $(STAGE0_C_SOURCES)

stage0.bin: stage0.elf
	$(OBJCOPY) -O binary $< $@
	@test $$(wc -c < $@) -le $(STAGE0_MAX_BYTES) || \
	  { echo "stage0.bin exceeds $(STAGE0_MAX_BYTES)-byte $(BOOT_BACKEND) boot ROM" >&2; exit 1; }

stage0.hex: stage0.bin
	python3 ../boot/bin2hex.py $< $@

# Diagnostic-only image for proving CPU, reset, Vesta MMIO, and UART startup
# without depending on SDRAM, SD, AstraHost, or the system ROM handoff.
route_probe.elf: crt0.S route_probe.c stage0.ld
	$(CC) $(CFLAGS) -T stage0.ld -nostdlib \
	  -Wl,--build-id=none,--gc-sections,-Map=route_probe.map \
	  -o $@ crt0.S route_probe.c

route_probe.bin: route_probe.elf
	$(OBJCOPY) -O binary $< $@
	@test $$(wc -c < $@) -le 4096 || \
	  { echo "route_probe.bin exceeds the 4096-byte diagnostic BRAM budget" >&2; exit 1; }

route_probe.hex: route_probe.bin
	python3 ../boot/bin2hex.py $< $@

route-probe: route_probe.hex

test_fat_loader: test_fat_loader.c fat_loader.c fat_loader.h
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -O2 -o $@ test_fat_loader.c fat_loader.c

test: test_fat_loader
	./test_fat_loader
	python3 -m pytest tests/test_make_fat32_image.py -q

test-route-probe:
	bash run_route_probe_sim.sh

verify: stage0.elf
	$(OBJDUMP) -h $<
	$(OBJDUMP) -s -j .vectors $< | sed -n '3,4p'

clean:
	rm -f stage0.elf stage0.bin stage0.hex stage0.map \
	  route_probe.elf route_probe.bin route_probe.hex route_probe.map \
	  test_fat_loader

.PHONY: all route-probe test test-route-probe verify clean
