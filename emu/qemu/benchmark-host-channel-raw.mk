include ../../mk/m68k-cross.mk

CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
BUILD ?= build/raw-host-channel
RAW_COMMAND_CAPACITY ?= 64
RAW_ITERATIONS ?= 100000
RAW_OPERATION ?= 0
RAW_EXPECTED_STATUS ?= ASTRA_STATUS_UNSUPPORTED
CFLAGS := -m68030 -msoft-float -O3 -ffreestanding -fno-builtin -nostdlib \
	-Wa,--noexecstack -Wall -Wextra -Werror -I../../sw/include
LDFLAGS := -T ../../sw/boot/astra.ld -nostdlib -Wl,--build-id=none
SOURCE := benchmark-host-channel-raw.c
START := ../../sw/boot/crt0.S

.PHONY: all clean

all: $(BUILD)/empty.bin $(BUILD)/command.bin

$(BUILD)/empty.elf: $(SOURCE) $(START) ../../sw/boot/astra.ld \
		../../sw/include/astra/host.h ../../sw/include/vesta.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DRAW_COMMANDS=0 -DRAW_ITERATIONS=$(RAW_ITERATIONS) \
		$(LDFLAGS) -o $@ $(START) $(SOURCE)

$(BUILD)/command.elf: $(SOURCE) $(START) ../../sw/boot/astra.ld \
		../../sw/include/astra/host.h ../../sw/include/vesta.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -DRAW_COMMANDS=1 \
		-DRAW_COMMAND_CAPACITY=$(RAW_COMMAND_CAPACITY) \
		-DRAW_ITERATIONS=$(RAW_ITERATIONS) \
		-DRAW_OPERATION=$(RAW_OPERATION) \
		-DRAW_EXPECTED_STATUS=$(RAW_EXPECTED_STATUS) \
		$(LDFLAGS) -o $@ $(START) $(SOURCE)

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

clean:
	rm -rf $(BUILD)
