include ../../mk/m68k-cross.mk
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -m68030 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
         -ffunction-sections -fdata-sections -Wa,--noexecstack \
         -Wall -Wextra -Werror \
         -I../include $(EXTRA_CFLAGS)
LINKER_SCRIPT ?= ../boot/astra_st.ld
LDFLAGS = -T $(LINKER_SCRIPT) -nostdlib \
          -Wl,--build-id=none,--gc-sections,-Map=astra_graphics_demo.map
ROM_WORDS ?= 1024
DEMO_SOURCE ?= demo.c

all: astra_graphics_demo.hex

astra_graphics_demo.elf: ../boot/crt0.S $(DEMO_SOURCE) $(LINKER_SCRIPT) \
                         ../include/vesta.h ../include/vega.h \
                         ../include/astraea.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ ../boot/crt0.S $(DEMO_SOURCE)

astra_graphics_demo.bin: astra_graphics_demo.elf
	$(OBJCOPY) -O binary $< $@

astra_graphics_demo.hex: astra_graphics_demo.bin ../boot/bin2hex.py
	python3 ../boot/bin2hex.py $< $@
	@words=$$(wc -l < $@); \
	  test $$words -le $(ROM_WORDS) || { \
	    echo "graphics demo requires $$words words; ROM_WORDS=$(ROM_WORDS)" >&2; \
	    exit 1; \
	  }

verify: astra_graphics_demo.elf astra_graphics_demo.hex
	$(OBJDUMP) -h $<
	@printf 'demo ROM: '; wc -c < astra_graphics_demo.bin

verify-stage0:
	$(MAKE) clean verify LINKER_SCRIPT=../stage0/stage0.ld
	@reset_pc=$$($(OBJDUMP) -s -j .vectors astra_graphics_demo.elf | \
	  awk '/^[[:space:]]+fffc0000 / { print $$3 }'); \
	  test "$$reset_pc" = "fffc0400" || { \
	    echo "stage-0 graphics reset PC is $$reset_pc, expected fffc0400" >&2; \
	    exit 1; \
	  }

clean:
	rm -f astra_graphics_demo.elf astra_graphics_demo.bin \
	      astra_graphics_demo.hex astra_graphics_demo.map

.PHONY: all verify verify-stage0 clean
