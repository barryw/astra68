# Astra 68 kernel bootstrap. Freestanding MC68030 image loaded by the system ROM.
include ../../mk/m68k-cross.mk
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
HOST_CC ?= cc
HOST_EXTRA_FLAGS ?=

KERNEL_VERSION ?= 0.1.0-dev
PANIC_SELFTEST ?= 0
SCHED_TRACE ?= 0
SOAK_SELFTEST ?= 0
BUILD_DIR = build
BUILD_INFO = $(BUILD_DIR)/kernel_build_info.h

CPPFLAGS = -I../include -I$(BUILD_DIR) -include $(BUILD_INFO)
CFLAGS = -m68030 -msoft-float -std=gnu11 -Os -ffreestanding -fno-builtin \
         -fno-common -fno-pic -fno-pie -fno-stack-protector \
         -Wa,--noexecstack -Wall -Wextra -Werror
LDFLAGS = -T kernel.ld -nostdlib -no-pie \
          -Wl,--build-id=none,-Map=astra_kernel.map

OBJECTS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/vectors.o \
          $(BUILD_DIR)/kernel.o $(BUILD_DIR)/platform.o \
          $(BUILD_DIR)/memory.o $(BUILD_DIR)/dma.o $(BUILD_DIR)/block.o \
          $(BUILD_DIR)/handle.o $(BUILD_DIR)/bytes.o \
          $(BUILD_DIR)/context.o \
          $(BUILD_DIR)/performance.o \
          $(BUILD_DIR)/thread.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/port.o \
          $(BUILD_DIR)/area.o $(BUILD_DIR)/ring.o \
          $(BUILD_DIR)/process.o \
          $(BUILD_DIR)/user_test.o \
          $(BUILD_DIR)/dispatch.o $(BUILD_DIR)/worker.o \
          $(BUILD_DIR)/worker_asm.o \
          $(BUILD_DIR)/pmmu.o $(BUILD_DIR)/vm.o \
          $(BUILD_DIR)/exception.o $(BUILD_DIR)/user_copy.o \
          $(BUILD_DIR)/user_copy_asm.o \
          $(BUILD_DIR)/boot_contract.o

all: astra_kernel.elf astra_kernel.bin

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_INFO): FORCE gen_build_info.py | $(BUILD_DIR)
	python3 gen_build_info.py --version "$(KERNEL_VERSION)" \
		--panic-selftest "$(PANIC_SELFTEST)" \
		--sched-trace "$(SCHED_TRACE)" \
		--soak-selftest "$(SOAK_SELFTEST)" --output "$@"

$(OBJECTS): $(BUILD_INFO)

$(BUILD_DIR)/entry.o: entry.S kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/vectors.o: vectors.S context.h dispatch.h thread.h user_copy.h kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel.o: kernel.c $(BUILD_INFO) bytes.h dispatch.h process.h thread.h \
                       ../include/astra/boot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/platform.o: platform.c platform.h ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/memory.o: memory.c memory.h ../include/astra/boot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/dma.o: dma.c dma.h generation.h memory.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/block.o: block.c block.h dma.h generation.h platform.h \
                      ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/handle.o: handle.c handle.h bytes.h generation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/bytes.o: bytes.c bytes.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/context.o: context.c context.h bytes.h exception.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/performance.o: performance.c performance.h bytes.h generation.h \
                            platform.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/thread.o: thread.c thread.h bytes.h generation.h context.h handle.h \
                       performance.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sync.o: sync.c sync.h thread.h bytes.h generation.h \
                     ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/port.o: port.c port.h thread.h handle.h bytes.h generation.h \
                     ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/area.o: area.c area.h handle.h memory.h vm.h bytes.h \
                     generation.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/ring.o: ring.c ring.h area.h thread.h bytes.h generation.h \
                     vm.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/process.o: process.c process.h thread.h sync.h port.h area.h ring.h context.h handle.h block.h \
                        bytes.h \
                        generation.h \
                        performance.h platform.h \
                        memory.h vm.h exception.h user_copy.h \
                        qualification.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/dispatch.o: dispatch.c dispatch.h process.h thread.h context.h \
                         performance.h \
                         exception.h panic.h platform.h user_copy.h \
                         worker.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/worker.o: worker.c worker.h process.h context.h panic.h \
                       platform.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/worker_asm.o: worker.S worker.h context.h kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user_test.o: user_test.S qualification.h \
                          ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/pmmu.o: pmmu.S pmmu.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/vm.o: vm.c vm.h pmmu.h memory.h thread.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/exception.o: exception.c exception.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user_copy.o: user_copy.c user_copy.h exception.h vm.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user_copy_asm.o: user_copy.S user_copy.h | $(BUILD_DIR)
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
	@$(OBJDUMP) -t astra_kernel.elf | grep -E '_kernel_(entry|image_start|file_end|memory_end|stack_top|worker_stack_(guard|top)|vectors)'
	@bytes=$$(wc -c < astra_kernel.bin); \
	  test $$bytes -le $$((0x80000)); \
	  printf 'kernel payload: %s bytes\n' "$$bytes"

test: $(BUILD_DIR)/test_boot_contract $(BUILD_DIR)/test_memory \
      $(BUILD_DIR)/test_platform \
      $(BUILD_DIR)/test_dma $(BUILD_DIR)/test_block $(BUILD_DIR)/test_vm \
      $(BUILD_DIR)/test_exception $(BUILD_DIR)/test_user_copy \
      $(BUILD_DIR)/test_handle $(BUILD_DIR)/test_bytes \
      $(BUILD_DIR)/test_context \
      $(BUILD_DIR)/test_performance \
      $(BUILD_DIR)/test_thread $(BUILD_DIR)/test_sync \
      $(BUILD_DIR)/test_port $(BUILD_DIR)/test_area $(BUILD_DIR)/test_ring \
      $(BUILD_DIR)/test_process $(BUILD_DIR)/test_worker \
      $(BUILD_DIR)/test_dispatch
	./$(BUILD_DIR)/test_boot_contract
	./$(BUILD_DIR)/test_memory
	./$(BUILD_DIR)/test_platform
	./$(BUILD_DIR)/test_dma
	./$(BUILD_DIR)/test_block
	./$(BUILD_DIR)/test_vm
	./$(BUILD_DIR)/test_exception
	./$(BUILD_DIR)/test_user_copy
	./$(BUILD_DIR)/test_handle
	./$(BUILD_DIR)/test_bytes
	./$(BUILD_DIR)/test_context
	./$(BUILD_DIR)/test_performance
	./$(BUILD_DIR)/test_thread
	./$(BUILD_DIR)/test_sync
	./$(BUILD_DIR)/test_port
	./$(BUILD_DIR)/test_area
	./$(BUILD_DIR)/test_ring
	./$(BUILD_DIR)/test_process
	./$(BUILD_DIR)/test_worker
	./$(BUILD_DIR)/test_dispatch

$(BUILD_DIR)/test_boot_contract: tests/test_boot_contract.c \
                                  ../common/boot_contract.c \
                                  ../include/astra/boot.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I../include \
		../common/boot_contract.c tests/test_boot_contract.c -o $@

$(BUILD_DIR)/test_memory: tests/test_memory.c memory.c memory.h \
                          ../common/boot_contract.c \
                          ../include/astra/boot.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -I. -I../include \
		../common/boot_contract.c memory.c tests/test_memory.c -o $@

$(BUILD_DIR)/test_platform: tests/test_platform.c platform.c platform.h \
                              ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PLATFORM_HOST_TEST=1 -I. -I../include \
		platform.c tests/test_platform.c -o $@

$(BUILD_DIR)/test_dma: tests/test_dma.c dma.c dma.h generation.h \
                       memory.c memory.h \
                       ../common/boot_contract.c \
                       ../include/astra/boot.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -I. -I../include \
		../common/boot_contract.c memory.c dma.c tests/test_dma.c -o $@

$(BUILD_DIR)/test_block: tests/test_block.c block.c block.h dma.c dma.h \
                         generation.h memory.c memory.h platform.h \
                         ../common/boot_contract.c \
                         ../include/astra/boot.h ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -I. -I../include \
		../common/boot_contract.c memory.c dma.c block.c \
		tests/test_block.c -o $@

$(BUILD_DIR)/test_vm: tests/test_vm.c vm.c vm.h pmmu.h memory.c memory.h \
                      ../common/boot_contract.c ../include/astra/boot.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -DKERNEL_VM_HOST_TEST=1 \
		-I. -I../include ../common/boot_contract.c memory.c vm.c \
		tests/test_vm.c -o $@

$(BUILD_DIR)/test_exception: tests/test_exception.c exception.c \
                                exception.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		exception.c tests/test_exception.c -o $@

$(BUILD_DIR)/test_user_copy: tests/test_user_copy.c user_copy.c user_copy.h \
                             exception.c exception.h vm.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_USER_COPY_HOST_TEST=1 -I. \
		exception.c user_copy.c tests/test_user_copy.c -o $@

$(BUILD_DIR)/test_handle: tests/test_handle.c handle.c handle.h bytes.c bytes.h \
                         generation.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		bytes.c handle.c tests/test_handle.c -o $@

$(BUILD_DIR)/test_bytes: tests/test_bytes.c bytes.c bytes.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		bytes.c tests/test_bytes.c -o $@

$(BUILD_DIR)/test_context: tests/test_context.c context.c context.h \
                             bytes.c bytes.h \
                             exception.c exception.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		bytes.c exception.c context.c tests/test_context.c -o $@

$(BUILD_DIR)/test_performance: tests/test_performance.c performance.c \
                                performance.h bytes.c bytes.h generation.h \
                                platform.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. \
		bytes.c performance.c tests/test_performance.c -o $@

$(BUILD_DIR)/test_thread: tests/test_thread.c thread.c thread.h \
			    bytes.c bytes.h generation.h \
                            performance.c performance.h platform.h \
                            context.c context.h exception.c exception.h \
                            handle.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		bytes.c exception.c context.c performance.c thread.c \
		tests/test_thread.c -o $@

$(BUILD_DIR)/test_sync: tests/test_sync.c sync.c sync.h thread.c thread.h \
			   bytes.c bytes.h generation.h \
                           performance.c performance.h platform.h \
                           context.c context.h exception.c exception.h \
                           handle.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		bytes.c exception.c context.c performance.c thread.c sync.c \
		tests/test_sync.c -o $@

$(BUILD_DIR)/test_port: tests/test_port.c port.c port.h handle.c handle.h \
			  thread.c thread.h bytes.c bytes.h generation.h \
			  performance.c performance.h platform.h \
			  context.c context.h exception.c exception.h \
			  ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		bytes.c exception.c context.c handle.c performance.c thread.c \
		port.c tests/test_port.c -o $@

$(BUILD_DIR)/test_area: tests/test_area.c area.c area.h vm.c vm.h \
			  memory.c memory.h bytes.c bytes.h generation.h \
			  ../common/boot_contract.c ../include/astra/boot.h \
			  ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -DKERNEL_VM_HOST_TEST=1 \
		-DKERNEL_MEMORY_HOST_TEST=1 \
		-DKERNEL_AREA_HOST_TEST=1 -I. -I../include \
		../common/boot_contract.c memory.c bytes.c vm.c area.c \
		tests/test_area.c -o $@

$(BUILD_DIR)/test_ring: tests/test_ring.c ring.c ring.h area.c area.h \
			  vm.c vm.h memory.c memory.h thread.c thread.h \
			  performance.c performance.h context.c context.h \
			  exception.c exception.h bytes.c bytes.h generation.h \
			  ../common/boot_contract.c ../include/astra/boot.h \
			  ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -DKERNEL_VM_HOST_TEST=1 \
		-DKERNEL_MEMORY_HOST_TEST=1 \
		-DKERNEL_AREA_HOST_TEST=1 -DKERNEL_PERFORMANCE_HOST_TEST=1 \
		-DKERNEL_RING_HOST_TEST=1 \
		-I. -I../include ../common/boot_contract.c memory.c bytes.c \
		exception.c context.c performance.c thread.c vm.c area.c ring.c \
		tests/test_ring.c -o $@

$(BUILD_DIR)/test_process: tests/test_process.c process.c process.h \
			    performance.c performance.h bytes.c bytes.h generation.h \
                            thread.c thread.h sync.c sync.h \
                            port.c port.h \
                            area.c area.h ring.c ring.h \
                            context.c context.h \
                            handle.c handle.h \
                            exception.c exception.h vm.c vm.h pmmu.h \
                            user_copy.c user_copy.h \
                            block.c block.h dma.c dma.h memory.c memory.h \
                            platform.h ../common/boot_contract.c \
                            ../include/astra/boot.h \
                            ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -DKERNEL_VM_HOST_TEST=1 \
		-DKERNEL_MEMORY_HOST_TEST=1 \
		-DKERNEL_AREA_HOST_TEST=1 \
		-DKERNEL_USER_COPY_HOST_TEST=1 \
		-DKERNEL_PROCESS_HOST_TEST=1 -DKERNEL_PERFORMANCE_HOST_TEST=1 \
		-DASTRA_KERNEL_SOAK_SELFTEST=1 \
		-I. -I../include \
		../common/boot_contract.c memory.c dma.c block.c vm.c exception.c \
		user_copy.c \
		bytes.c context.c handle.c performance.c thread.c sync.c port.c \
		area.c ring.c process.c \
		tests/test_process.c -o $@

$(BUILD_DIR)/test_dispatch: tests/test_dispatch.c dispatch.c dispatch.h \
			     performance.c performance.h bytes.c bytes.h generation.h \
                             exception.c exception.h process.h context.h \
                             panic.h platform.h user_copy.h worker.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		bytes.c exception.c performance.c dispatch.c \
		tests/test_dispatch.c -o $@

$(BUILD_DIR)/test_worker: tests/test_worker.c worker.c worker.h process.h \
                           context.h panic.h platform.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_WORKER_HOST_TEST=1 -I. -I../include \
		worker.c tests/test_worker.c -o $@

clean:
	rm -rf $(BUILD_DIR) astra_kernel.elf astra_kernel.bin astra_kernel.map

FORCE:

.PHONY: all clean test verify FORCE
