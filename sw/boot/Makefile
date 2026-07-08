# Astra 68 — boot ROM build. Bare-metal m68k via gcc-m68k-linux-gnu (freestanding).
CROSS   ?= m68k-linux-gnu-
CC       = $(CROSS)gcc
OBJCOPY  = $(CROSS)objcopy
OBJDUMP  = $(CROSS)objdump

CFLAGS  = -m68020 -msoft-float -Os -ffreestanding -fno-builtin -nostdlib \
          -Wall -Wextra -I../include
LDFLAGS = -T astra.ld -nostdlib -Wl,-Map=astra_boot.map

all: astra_boot.bin

astra_boot.elf: crt0.S boot.c astra.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ crt0.S boot.c

astra_boot.bin: astra_boot.elf
	$(OBJCOPY) -O binary $< $@

verify: astra_boot.elf
	@echo "== sections =="; $(OBJDUMP) -h $< | grep -E 'Idx|vectors|text|data|bss'
	@echo "== reset vectors (SP @ +0, PC @ +4) =="; $(OBJDUMP) -s -j .vectors $< | sed -n '3,4p'
	@echo "== symbols =="; $(OBJDUMP) -t $< | grep -E '_start|kmain|_stack_top' | head

clean:
	rm -f *.elf *.bin *.map
