include ../../mk/m68k-cross.mk
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump

CFLAGS = -m68030 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
         -ffunction-sections -fdata-sections -Wa,--noexecstack \
         -Wall -Wextra -Werror \
         -I../include $(EXTRA_CFLAGS)
LDFLAGS = -T ../boot/astra_st.ld -nostdlib \
          -Wl,--build-id=none,--gc-sections,-Map=astra_graphics_demo.map
ROM_WORDS ?= 1024

all: astra_graphics_demo.hex

astra_graphics_demo.elf: ../boot/crt0.S demo.c ../boot/astra_st.ld \
                         ../include/vesta.h ../include/vega.h \
                         ../include/astraea.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ ../boot/crt0.S demo.c

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

clean:
	rm -f astra_graphics_demo.elf astra_graphics_demo.bin \
	      astra_graphics_demo.hex astra_graphics_demo.map

.PHONY: all verify clean
