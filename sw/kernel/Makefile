# Astra 68 kernel bootstrap. Freestanding MC68030 image loaded by the system ROM.
CROSS ?= m68k-linux-gnu-
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
HOST_CC ?= cc

KERNEL_VERSION ?= 0.1.0-dev
PANIC_SELFTEST ?= 0
BUILD_DIR = build
BUILD_INFO = $(BUILD_DIR)/kernel_build_info.h

CPPFLAGS = -I../include -I$(BUILD_DIR) \
           -DASTRA_KERNEL_PANIC_SELFTEST=$(PANIC_SELFTEST)
CFLAGS = -m68030 -msoft-float -std=gnu11 -Os -ffreestanding -fno-builtin \
         -fno-common -fno-pic -fno-pie -fno-stack-protector \
         -Wall -Wextra -Werror
LDFLAGS = -T kernel.ld -nostdlib -no-pie \
          -Wl,--build-id=none,-Map=astra_kernel.map

OBJECTS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/vectors.o \
          $(BUILD_DIR)/kernel.o $(BUILD_DIR)/boot_contract.o

all: astra_kernel.elf astra_kernel.bin

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_INFO): FORCE gen_build_info.py | $(BUILD_DIR)
	python3 gen_build_info.py --version "$(KERNEL_VERSION)" --output "$@"

$(BUILD_DIR)/entry.o: entry.S kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/vectors.o: vectors.S kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel.o: kernel.c $(BUILD_INFO) ../include/astra/boot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/boot_contract.o: ../common/boot_contract.c ../include/astra/boot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

astra_kernel.elf: $(OBJECTS) kernel.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

astra_kernel.bin: astra_kernel.elf
	$(OBJCOPY) -O binary $< $@

verify: astra_kernel.elf astra_kernel.bin
	@echo "== kernel sections =="
	@$(OBJDUMP) -h astra_kernel.elf
	@echo "== kernel entry and bounds =="
	@$(OBJDUMP) -t astra_kernel.elf | grep -E '_kernel_(entry|image_start|file_end|memory_end|stack_top|vectors)'
	@bytes=$$(wc -c < astra_kernel.bin); \
	  test $$bytes -le $$((0x80000)); \
	  printf 'kernel payload: %s bytes\n' "$$bytes"

test: $(BUILD_DIR)/test_boot_contract
	./$(BUILD_DIR)/test_boot_contract

$(BUILD_DIR)/test_boot_contract: tests/test_boot_contract.c \
                                  ../common/boot_contract.c \
                                  ../include/astra/boot.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -I../include \
		../common/boot_contract.c tests/test_boot_contract.c -o $@

clean:
	rm -rf $(BUILD_DIR) astra_kernel.elf astra_kernel.bin astra_kernel.map

FORCE:

.PHONY: all clean test verify FORCE
