# Astra 68 kernel bootstrap. Freestanding MC68030 image loaded by the system ROM.
include ../../mk/m68k-cross.mk
CC = $(CROSS)gcc
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
HOST_CC ?= cc
HOST_EXTRA_FLAGS ?=
HOST_EXTRA_FLAGS += -I../include

KERNEL_VERSION ?= 0.1.0-dev
PANIC_SELFTEST ?= 0
SCHED_TRACE ?= 0
SOAK_SELFTEST ?= 0
# The K1/K10 qualification pair is a milestone harness, not a boot workload.
K1_QUALIFICATION ?= 0

# Release or debug, and the only thing it decides is how much the kernel says.
#
# A release keeps what explains a machine that misbehaved -- faults,
# quarantines, the boot -- and compiles the chatty sites away entirely, so they
# cost no ROM and no cycles. A debug build keeps everything, including the
# interrupt deliver/acknowledge stream, which is how a device problem is
# actually found and which is far too noisy to ship.
#
# Both are built by the gates, because a release that nobody builds is a
# release that does not compile when it matters.
ASTRA_BUILD ?= release

# The qualification harness carries no diagnostic surface.
#
# It is a device gate, not a machine anyone reads: nothing in it asks the
# kernel what it has been doing, and the introspection that answers that
# question is real code in a kernel whose bootstrap region is nearly full. The
# surface is what puts DEBUG on a process's own handle, so removing it removes
# the syscalls that right guards along with it.
ifeq ($(K1_QUALIFICATION),1)
CPPFLAGS_DEBUG_SURFACE = -DASTRA_KERNEL_DEBUG_SURFACE=0
endif

ifeq ($(ASTRA_BUILD),release)
KERNEL_TRACE_BUILD_LEVEL = KERNEL_TRACE_LEVEL_NOTICE
else ifeq ($(ASTRA_BUILD),debug)
KERNEL_TRACE_BUILD_LEVEL = KERNEL_TRACE_LEVEL_DEBUG
else
$(error ASTRA_BUILD must be release or debug, not '$(ASTRA_BUILD)')
endif

BUILD_DIR = build
BUILD_INFO = $(BUILD_DIR)/kernel_build_info.h
KERNEL_ELF = $(BUILD_DIR)/astra_kernel.elf
KERNEL_BIN = $(BUILD_DIR)/astra_kernel.bin
KERNEL_MAP = $(BUILD_DIR)/astra_kernel.map

CPPFLAGS = -I../include -I$(BUILD_DIR) -include $(BUILD_INFO) \
           -DKERNEL_TRACE_BUILD_LEVEL=$(KERNEL_TRACE_BUILD_LEVEL) \
           $(CPPFLAGS_DEBUG_SURFACE)
# -ffunction-sections with --gc-sections below. A large part of this kernel is
# introspection and test surface -- the *_snapshot, *_stats and
# *_test_fail_* entry points the host suites drive -- and the image itself
# never reaches any of it. Without the pair it was all linked in anyway: 9,604
# bytes of text, 7,599 of ROM payload after LZ4, and two pages of RAM, spent on
# code the machine cannot execute. The suites compile the same sources on the
# host and keep using those entry points, so nothing is deleted to gain this.
#
# -fdata-sections is deliberately not set: measured, it removes four further
# bytes and is not worth the object-layout churn.
# Beyond -Wall -Wextra. Each of these is a class of defect this kernel cannot
# afford rather than a style preference: -Wcast-align and -Wpointer-arith
# because the target is a 68030 where a misaligned access faults;
# -Wjump-misses-init because every allocating path here unwinds through a
# `goto failed`; -Wundef because reading an undefined macro as 0 silently picks
# a branch, which has already cost this project time in lwext4's config guard;
# -Wcast-qual because the handle table enforces capability rights and const is
# load-bearing there; -Wduplicated-cond, -Wduplicated-branches and -Wlogical-op
# because they catch copy-paste in exactly the long condition chains this code
# is made of. The kernel was already clean for all of them bar four sites.
WARNINGS = -Wall -Wextra -Werror -Wshadow -Wcast-align -Wcast-qual \
           -Wpointer-arith -Wwrite-strings -Wundef -Wnull-dereference \
           -Wduplicated-cond -Wduplicated-branches -Wlogical-op \
           -Wjump-misses-init -Warray-bounds=2 -Wformat=2 \
           -Wstrict-prototypes -Wmissing-prototypes -Wshift-overflow=2
CFLAGS = -m68030 -msoft-float -std=gnu11 -Os -ffreestanding -fno-builtin \
         -fno-common -fno-pic -fno-pie -fno-stack-protector \
         -Wa,--noexecstack $(WARNINGS) -ffunction-sections \
         $(ASTRA_DEBUG_FLAGS)
# kernel.ld already KEEPs the three sections nothing references by symbol --
# .text.entry, .vectors and .kernel_trace -- which is what makes collecting the
# rest safe. A new section that only the hardware or the ROM reaches needs the
# same treatment or it will be collected.
LDFLAGS = -T kernel.ld -nostdlib -no-pie -Wl,--gc-sections \
          -Wl,--build-id=none,-Map=$(KERNEL_MAP)

OBJECTS = $(BUILD_DIR)/entry.o $(BUILD_DIR)/vectors.o \
          $(BUILD_DIR)/kernel.o $(BUILD_DIR)/platform.o \
          $(BUILD_DIR)/mmio.o $(BUILD_DIR)/trace.o \
          $(BUILD_DIR)/irq.o $(BUILD_DIR)/interrupt.o \
          $(BUILD_DIR)/monitor.o $(BUILD_DIR)/irq_latency.o \
          $(BUILD_DIR)/fault.o \
          $(BUILD_DIR)/allocation.o $(BUILD_DIR)/object_cache.o \
          $(BUILD_DIR)/memory.o $(BUILD_DIR)/dma.o $(BUILD_DIR)/block.o \
          $(BUILD_DIR)/device.o \
          $(BUILD_DIR)/handle.o $(BUILD_DIR)/bytes.o \
	  $(BUILD_DIR)/context.o \
	  $(BUILD_DIR)/performance.o \
	  $(BUILD_DIR)/thread.o $(BUILD_DIR)/sync.o $(BUILD_DIR)/port.o \
          $(BUILD_DIR)/area.o $(BUILD_DIR)/ring.o \
          $(BUILD_DIR)/elf.o \
          $(BUILD_DIR)/process.o \
          $(BUILD_DIR)/user_test.o \
          $(BUILD_DIR)/dispatch.o $(BUILD_DIR)/worker.o \
          $(BUILD_DIR)/worker_asm.o \
          $(BUILD_DIR)/pmmu.o $(BUILD_DIR)/vm.o \
          $(BUILD_DIR)/exception.o \
          $(BUILD_DIR)/user_copy.o \
          $(BUILD_DIR)/user_copy_asm.o \
          $(BUILD_DIR)/boot_contract.o \
          $(BUILD_DIR)/civil.o $(BUILD_DIR)/divide.o \
          $(BUILD_DIR)/format.o

all: $(KERNEL_ELF) $(KERNEL_BIN)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_INFO): FORCE gen_build_info.py | $(BUILD_DIR)
	python3 gen_build_info.py --version "$(KERNEL_VERSION)" \
		--panic-selftest "$(PANIC_SELFTEST)" \
		--sched-trace "$(SCHED_TRACE)" \
		--soak-selftest "$(SOAK_SELFTEST)" \
		--k1-qualification "$(K1_QUALIFICATION)" \
		--build "$(ASTRA_BUILD)" --output "$@"

$(OBJECTS): $(BUILD_INFO)

$(BUILD_DIR)/entry.o: entry.S kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/vectors.o: vectors.S context.h dispatch.h thread.h user_copy.h \
                        irq_latency.h kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/kernel.o: kernel.c $(BUILD_INFO) allocation.h bytes.h device.h dispatch.h fault.h format.h interrupt.h irq_latency.h monitor.h process.h thread.h trace.h \
                       ../include/astra/boot.h ../include/astra/supervisor.h \
                       | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/platform.o: platform.c platform.h mmio.h ../include/astraea.h \
                         ../include/ohci.h ../include/vega.h \
                         ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/mmio.o: mmio.c mmio.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/trace.o: trace.c trace.h platform.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/irq.o: irq.c irq.h thread.h bytes.h generation.h trace.h \
                     object_cache.h allocation.h \
                     ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/interrupt.o: interrupt.c interrupt.h irq.h monitor.h platform.h trace.h worker.h \
                           ../include/astraea.h ../include/ohci.h \
                           ../include/vega.h ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/monitor.o: monitor.c monitor.h bytes.h worker.h irq.h irq_latency.h handle.h memory.h \
                         performance.h platform.h process.h thread.h trace.h \
                         vm.h ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/allocation.o: allocation.c allocation.h \
                           ../include/astra/integer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/object_cache.o: object_cache.c object_cache.h allocation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/memory.o: memory.c memory.h allocation.h \
                       ../include/astra/boot.h \
                       ../include/astra/integer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/dma.o: dma.c dma.h generation.h memory.h object_cache.h \
                    allocation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/block.o: block.c block.h dma.h generation.h platform.h \
                      object_cache.h allocation.h \
                      ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/device.o: device.c device.h bytes.h generation.h object_cache.h \
                       allocation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/handle.o: handle.c handle.h bytes.h generation.h \
                       object_cache.h allocation.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/bytes.o: bytes.c bytes.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/context.o: context.c context.h bytes.h exception.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/performance.o: performance.c performance.h bytes.h generation.h \
                            platform.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/irq_latency.o: irq_latency.c irq_latency.h performance.h \
                            bytes.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/thread.o: thread.c thread.h bytes.h generation.h context.h handle.h \
                       object_cache.h allocation.h \
                       performance.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/sync.o: sync.c sync.h thread.h bytes.h generation.h \
                     object_cache.h allocation.h \
                     ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/port.o: port.c port.h thread.h handle.h bytes.h generation.h \
                     object_cache.h allocation.h \
                     ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/area.o: area.c area.h handle.h memory.h vm.h bytes.h \
                     object_cache.h allocation.h \
                     generation.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/elf.o: elf.c elf.h bytes.h ../include/astra/endian.h \
                    ../include/astra/integer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/ring.o: ring.c ring.h area.h thread.h bytes.h generation.h \
			     object_cache.h allocation.h \
                     vm.h ../include/astra/syscall.h \
                     ../include/astra/integer.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/process.o: process.c process.h thread.h sync.h port.h area.h ring.h context.h handle.h block.h device.h interrupt.h \
                        bytes.h \
                        generation.h \
                        object_cache.h allocation.h \
                        performance.h platform.h \
                        memory.h vm.h exception.h user_copy.h \
                        qualification.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/dispatch.o: dispatch.c dispatch.h process.h thread.h context.h \
                         fault.h interrupt.h trace.h \
                         performance.h \
                         exception.h panic.h platform.h user_copy.h \
                         worker.h ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/worker.o: worker.c worker.h process.h context.h panic.h \
                       platform.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/worker_asm.o: worker.S worker.h context.h irq_latency.h \
                           kernel.ld | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user_test.o: user_test.S qualification.h \
                          ../include/astra/syscall.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/pmmu.o: pmmu.S pmmu.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/vm.o: vm.c vm.h pmmu.h memory.h thread.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/exception.o: exception.c exception.h ../include/astra/endian.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/fault.o: fault.c fault.h exception.h platform.h vm.h \
                      ../include/vesta.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user_copy.o: user_copy.c user_copy.h exception.h vm.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/user_copy_asm.o: user_copy.S user_copy.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/boot_contract.o: ../common/boot_contract.c ../include/astra/boot.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/format.o: format.c format.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/civil.o: ../common/civil.c ../include/astra/civil.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/divide.o: ../common/divide.c ../include/astra/divide.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(KERNEL_ELF): $(OBJECTS) kernel.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJECTS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

verify: $(KERNEL_ELF) $(KERNEL_BIN)
	@if $(OBJDUMP) -t $(BUILD_DIR)/irq.o | \
		grep -q 'astra_u32_increment_saturating'; then \
		echo "IRQ saturating counter was not inlined"; exit 1; \
	fi
	@echo "== kernel sections =="
	@$(OBJDUMP) -h $(KERNEL_ELF)
	@echo "== kernel entry and bounds =="
	@$(OBJDUMP) -t $(KERNEL_ELF) | grep -E '_kernel_(entry|image_start|file_end|memory_end|stack_top|worker_stack_(guard|top)|vectors)'
	@bytes=$$(wc -c < $(KERNEL_BIN)); \
	  test $$bytes -le $$((0x80000)); \
	  printf 'kernel payload: %s bytes\n' "$$bytes"

test: $(BUILD_DIR)/test_boot_contract $(BUILD_DIR)/test_civil \
      $(BUILD_DIR)/test_divide $(BUILD_DIR)/test_generation \
      $(BUILD_DIR)/test_format \
      $(BUILD_DIR)/test_allocation \
      $(BUILD_DIR)/test_memory \
      $(BUILD_DIR)/test_mmio $(BUILD_DIR)/test_trace $(BUILD_DIR)/test_irq \
      $(BUILD_DIR)/test_platform $(BUILD_DIR)/test_interrupt \
      $(BUILD_DIR)/test_monitor \
      $(BUILD_DIR)/test_dma $(BUILD_DIR)/test_block $(BUILD_DIR)/test_device \
      $(BUILD_DIR)/test_vm \
      $(BUILD_DIR)/test_exception $(BUILD_DIR)/test_fault \
      $(BUILD_DIR)/test_user_copy \
      $(BUILD_DIR)/test_handle $(BUILD_DIR)/test_bytes \
      $(BUILD_DIR)/test_context \
      $(BUILD_DIR)/test_performance $(BUILD_DIR)/test_irq_latency \
      $(BUILD_DIR)/test_thread $(BUILD_DIR)/test_sync \
      $(BUILD_DIR)/test_port $(BUILD_DIR)/test_area $(BUILD_DIR)/test_ring \
      $(BUILD_DIR)/test_process $(BUILD_DIR)/test_worker \
      $(BUILD_DIR)/test_dispatch $(BUILD_DIR)/test_elf
	./$(BUILD_DIR)/test_boot_contract
	./$(BUILD_DIR)/test_civil
	./$(BUILD_DIR)/test_divide
	./$(BUILD_DIR)/test_generation
	./$(BUILD_DIR)/test_format
	./$(BUILD_DIR)/test_allocation
	./$(BUILD_DIR)/test_memory
	./$(BUILD_DIR)/test_mmio
	./$(BUILD_DIR)/test_trace
	./$(BUILD_DIR)/test_irq
	./$(BUILD_DIR)/test_platform
	./$(BUILD_DIR)/test_interrupt
	./$(BUILD_DIR)/test_monitor
	./$(BUILD_DIR)/test_dma
	./$(BUILD_DIR)/test_block
	./$(BUILD_DIR)/test_device
	./$(BUILD_DIR)/test_vm
	./$(BUILD_DIR)/test_exception
	./$(BUILD_DIR)/test_fault
	./$(BUILD_DIR)/test_user_copy
	./$(BUILD_DIR)/test_handle
	./$(BUILD_DIR)/test_bytes
	./$(BUILD_DIR)/test_context
	./$(BUILD_DIR)/test_performance
	./$(BUILD_DIR)/test_irq_latency
	./$(BUILD_DIR)/test_thread
	./$(BUILD_DIR)/test_sync
	./$(BUILD_DIR)/test_port
	./$(BUILD_DIR)/test_area
	./$(BUILD_DIR)/test_ring
	./$(BUILD_DIR)/test_process
	./$(BUILD_DIR)/test_worker
	./$(BUILD_DIR)/test_dispatch
	./$(BUILD_DIR)/test_elf

$(BUILD_DIR)/test_elf: tests/test_elf.c elf.c elf.h bytes.c bytes.h \
                       memory.h process.h vm.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		-I../include bytes.c elf.c tests/test_elf.c -o $@

$(BUILD_DIR)/test_civil: tests/test_civil.c ../common/civil.c \
                         ../common/divide.c ../include/astra/civil.h \
                         ../include/astra/divide.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I../include \
		../common/civil.c ../common/divide.c tests/test_civil.c -o $@

$(BUILD_DIR)/test_divide: tests/test_divide.c ../common/divide.c \
                          ../include/astra/divide.h \
                          ../include/astra/integer.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I../include \
		../common/divide.c tests/test_divide.c -o $@

$(BUILD_DIR)/test_generation: tests/test_generation.c generation.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		tests/test_generation.c -o $@

$(BUILD_DIR)/test_format: tests/test_format.c format.c format.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		format.c tests/test_format.c -o $@

$(BUILD_DIR)/test_boot_contract: tests/test_boot_contract.c \
                                  ../common/boot_contract.c \
                                  ../include/astra/boot.h \
                                  ../include/ohci.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I../include \
		../common/boot_contract.c tests/test_boot_contract.c -o $@

$(BUILD_DIR)/test_allocation: tests/test_allocation.c allocation.c \
                               allocation.h object_cache.c \
                               object_cache.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		allocation.c object_cache.c tests/test_allocation.c -o $@

$(BUILD_DIR)/test_memory: tests/test_memory.c memory.c memory.h \
                          allocation.c allocation.h \
                          ../common/boot_contract.c \
                          ../include/astra/boot.h \
                          ../include/ohci.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -I. -I../include \
		../common/boot_contract.c allocation.c memory.c \
		tests/test_memory.c -o $@

$(BUILD_DIR)/test_mmio: tests/test_mmio.c mmio.c mmio.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MMIO_HOST_TEST=1 -I. \
		mmio.c tests/test_mmio.c -o $@

$(BUILD_DIR)/test_trace: tests/test_trace.c trace.c trace.h bytes.c bytes.h \
			    platform.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_TRACE_HOST_TEST=1 -I. -I../include \
		bytes.c trace.c tests/test_trace.c -o $@

$(BUILD_DIR)/test_irq: tests/test_irq.c irq.c irq.h thread.c thread.h trace.h \
			 allocation.c allocation.h object_cache.c object_cache.h \
			 bytes.c bytes.h generation.h \
			 performance.c performance.h platform.h \
			 context.c context.h exception.c exception.h \
			 handle.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		allocation.c object_cache.c bytes.c exception.c context.c \
		performance.c thread.c irq.c tests/test_irq.c -o $@

$(BUILD_DIR)/test_platform: tests/test_platform.c platform.c platform.h \
                              mmio.c mmio.h \
                              ../include/astraea.h ../include/ohci.h \
                              ../include/vega.h ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PLATFORM_HOST_TEST=1 -DKERNEL_MMIO_HOST_TEST=1 \
		-I. -I../include mmio.c platform.c tests/test_platform.c -o $@

$(BUILD_DIR)/test_interrupt: tests/test_interrupt.c interrupt.c interrupt.h trace.h \
			 irq.c irq.h monitor.h platform.c platform.h mmio.c mmio.h \
			 thread.c thread.h allocation.c allocation.h \
			 object_cache.c object_cache.h bytes.c bytes.h generation.h \
			 performance.c performance.h context.c context.h \
			 exception.c exception.h handle.h \
			 ../include/astra/syscall.h ../include/astraea.h \
			 ../include/ohci.h ../include/vega.h \
			 ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PLATFORM_HOST_TEST=1 -DKERNEL_MMIO_HOST_TEST=1 \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		allocation.c object_cache.c bytes.c exception.c context.c \
		performance.c thread.c mmio.c platform.c irq.c interrupt.c \
		tests/test_interrupt.c -o $@


$(BUILD_DIR)/test_monitor: tests/test_monitor.c monitor.c monitor.h bytes.c bytes.h \
			    performance.c performance.h worker.h \
			    irq.h irq_latency.h handle.h memory.h performance.h platform.h \
			    process.h thread.h trace.h vm.h ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MONITOR_HOST_TEST=1 -DKERNEL_PERFORMANCE_HOST_TEST=1 \
		-I. -I../include bytes.c performance.c monitor.c \
		tests/test_monitor.c -o $@

$(BUILD_DIR)/test_dma: tests/test_dma.c dma.c dma.h generation.h \
                       allocation.c allocation.h object_cache.c object_cache.h \
                       memory.c memory.h \
                       ../common/boot_contract.c \
                       ../include/astra/boot.h \
                       ../include/ohci.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -I. -I../include \
		../common/boot_contract.c allocation.c object_cache.c memory.c \
		dma.c tests/test_dma.c -o $@

$(BUILD_DIR)/test_block: tests/test_block.c block.c block.h dma.c dma.h \
                         generation.h allocation.c allocation.h \
                         object_cache.c object_cache.h \
                         memory.c memory.h platform.h \
                         ../common/boot_contract.c \
                         ../include/astra/boot.h ../include/ohci.h \
                         ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -I. -I../include \
		../common/boot_contract.c allocation.c object_cache.c memory.c \
		dma.c block.c \
		tests/test_block.c -o $@

$(BUILD_DIR)/test_device: tests/test_device.c device.c device.h bytes.c bytes.h generation.h \
                           allocation.c allocation.h object_cache.c \
                           object_cache.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		allocation.c object_cache.c bytes.c device.c tests/test_device.c -o $@

$(BUILD_DIR)/test_vm: tests/test_vm.c vm.c vm.h pmmu.h memory.c memory.h \
                      allocation.c allocation.h bytes.c bytes.h \
                      ../common/boot_contract.c ../include/astra/boot.h \
                      ../include/ohci.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_VM_HOST_TEST=1 -DKERNEL_MEMORY_HOST_TEST=1 \
		-I. -I../include ../common/boot_contract.c allocation.c bytes.c memory.c vm.c \
		tests/test_vm.c -o $@

$(BUILD_DIR)/test_exception: tests/test_exception.c exception.c \
                                exception.h ../include/astra/endian.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-I. -I../include \
		exception.c tests/test_exception.c -o $@

$(BUILD_DIR)/test_fault: tests/test_fault.c fault.c fault.h exception.h \
                          platform.h vm.h ../include/vesta.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-I. -I../include fault.c tests/test_fault.c -o $@

$(BUILD_DIR)/test_user_copy: tests/test_user_copy.c user_copy.c user_copy.h \
                             exception.c exception.h vm.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_USER_COPY_HOST_TEST=1 -I. -I../include \
		exception.c user_copy.c tests/test_user_copy.c -o $@

$(BUILD_DIR)/test_handle: tests/test_handle.c handle.c handle.h bytes.c bytes.h \
                         generation.h allocation.c allocation.h \
                         object_cache.c object_cache.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		allocation.c object_cache.c bytes.c handle.c tests/test_handle.c -o $@

$(BUILD_DIR)/test_bytes: tests/test_bytes.c bytes.c bytes.h \
                         ../include/astra/endian.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) -I. \
		-I../include \
		bytes.c tests/test_bytes.c -o $@

$(BUILD_DIR)/test_context: tests/test_context.c context.c context.h \
                             bytes.c bytes.h \
                             exception.c exception.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-I. -I../include \
		bytes.c exception.c context.c tests/test_context.c -o $@

$(BUILD_DIR)/test_performance: tests/test_performance.c performance.c \
                                performance.h bytes.c bytes.h generation.h \
                                platform.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. \
		bytes.c performance.c tests/test_performance.c -o $@

$(BUILD_DIR)/test_irq_latency: tests/test_irq_latency.c irq_latency.c \
				       irq_latency.h performance.c performance.h \
				       bytes.c bytes.h generation.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. \
		bytes.c performance.c irq_latency.c \
		tests/test_irq_latency.c -o $@

$(BUILD_DIR)/test_thread: tests/test_thread.c thread.c thread.h \
			    allocation.c allocation.h object_cache.c object_cache.h \
			    bytes.c bytes.h generation.h \
                            performance.c performance.h platform.h \
                            context.c context.h exception.c exception.h \
                            handle.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		allocation.c object_cache.c bytes.c exception.c context.c \
		performance.c thread.c \
		tests/test_thread.c -o $@

$(BUILD_DIR)/test_sync: tests/test_sync.c sync.c sync.h thread.c thread.h \
			   allocation.c allocation.h object_cache.c object_cache.h \
			   bytes.c bytes.h generation.h \
                           performance.c performance.h platform.h \
                           context.c context.h exception.c exception.h \
                           handle.h ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		allocation.c object_cache.c bytes.c exception.c context.c \
		performance.c thread.c sync.c \
		tests/test_sync.c -o $@

$(BUILD_DIR)/test_port: tests/test_port.c port.c port.h handle.c handle.h \
			  allocation.c allocation.h object_cache.c object_cache.h \
			  thread.c thread.h bytes.c bytes.h generation.h \
			  performance.c performance.h platform.h \
			  context.c context.h exception.c exception.h \
			  ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		allocation.c object_cache.c bytes.c exception.c context.c \
		handle.c performance.c thread.c \
		port.c tests/test_port.c -o $@

$(BUILD_DIR)/test_area: tests/test_area.c area.c area.h vm.c vm.h \
			  memory.c memory.h bytes.c bytes.h generation.h \
			  allocation.c allocation.h object_cache.c object_cache.h \
			  ../common/boot_contract.c ../include/astra/boot.h \
			  ../include/ohci.h \
			  ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -DKERNEL_VM_HOST_TEST=1 \
		-DKERNEL_MEMORY_HOST_TEST=1 \
		-DKERNEL_AREA_HOST_TEST=1 -I. -I../include \
		../common/boot_contract.c allocation.c object_cache.c memory.c \
		bytes.c vm.c area.c \
		tests/test_area.c -o $@

$(BUILD_DIR)/test_ring: tests/test_ring.c ring.c ring.h area.c area.h \
			  vm.c vm.h memory.c memory.h thread.c thread.h \
			  allocation.c allocation.h object_cache.c object_cache.h \
			  performance.c performance.h context.c context.h \
			  exception.c exception.h bytes.c bytes.h generation.h \
			  ../common/boot_contract.c ../include/astra/boot.h \
			  ../include/ohci.h \
			  ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -DKERNEL_VM_HOST_TEST=1 \
		-DKERNEL_MEMORY_HOST_TEST=1 \
		-DKERNEL_AREA_HOST_TEST=1 -DKERNEL_PERFORMANCE_HOST_TEST=1 \
		-DKERNEL_RING_HOST_TEST=1 \
		-I. -I../include ../common/boot_contract.c allocation.c \
		object_cache.c memory.c bytes.c \
		exception.c context.c performance.c thread.c vm.c area.c ring.c \
		tests/test_ring.c -o $@

$(BUILD_DIR)/test_process: tests/test_process.c process.c process.h \
			    allocation.c allocation.h object_cache.c object_cache.h \
			    performance.c performance.h bytes.c bytes.h generation.h \
                            trace.c trace.h \
                            thread.c thread.h sync.c sync.h \
                            port.c port.h \
                            area.c area.h ring.c ring.h \
                            irq.c irq.h \
                            interrupt.h worker.h \
                            context.c context.h \
                            handle.c handle.h \
                            exception.c exception.h vm.c vm.h pmmu.h \
                            user_copy.c user_copy.h \
                            block.c block.h device.c device.h dma.c dma.h memory.c memory.h \
                            elf.c elf.h \
                            platform.h ../common/boot_contract.c \
                            ../common/divide.c ../include/astra/divide.h \
                            ../include/astra/boot.h ../include/ohci.h \
                            ../include/astra/process.h \
                            ../include/astra/syscall.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_MEMORY_NO_POISON=1 -DKERNEL_VM_HOST_TEST=1 \
		-DKERNEL_MEMORY_HOST_TEST=1 \
		-DKERNEL_AREA_HOST_TEST=1 \
		-DKERNEL_USER_COPY_HOST_TEST=1 \
		-DKERNEL_PROCESS_HOST_TEST=1 -DKERNEL_PERFORMANCE_HOST_TEST=1 \
		-DASTRA_KERNEL_SOAK_SELFTEST=1 \
		-I. -I../include \
		../common/boot_contract.c ../common/divide.c allocation.c object_cache.c memory.c \
		dma.c block.c device.c vm.c exception.c \
		user_copy.c \
		bytes.c context.c handle.c performance.c thread.c sync.c port.c \
		area.c ring.c irq.c elf.c trace.c process.c \
		tests/test_process.c -o $@

$(BUILD_DIR)/test_dispatch: tests/test_dispatch.c dispatch.c dispatch.h \
			     fault.h interrupt.h trace.h \
			     performance.c performance.h bytes.c bytes.h generation.h \
                             exception.c exception.h process.h context.h \
                             panic.h platform.h user_copy.h worker.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_PERFORMANCE_HOST_TEST=1 -I. -I../include \
		bytes.c exception.c performance.c dispatch.c \
		tests/test_dispatch.c -o $@

$(BUILD_DIR)/test_worker: tests/test_worker.c worker.c worker.h process.h \
			   performance.c performance.h bytes.c bytes.h \
			   context.h panic.h platform.h | $(BUILD_DIR)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror $(HOST_EXTRA_FLAGS) \
		-DKERNEL_WORKER_HOST_TEST=1 -DKERNEL_PERFORMANCE_HOST_TEST=1 \
		-I. -I../include bytes.c performance.c worker.c \
		tests/test_worker.c -o $@

clean:
	rm -rf $(BUILD_DIR)
	rm -f astra_kernel.elf astra_kernel.bin astra_kernel.map
	rm -f *.gcov.json.gz

# Line coverage over the host suites. The instrumented build is not the one
# anything else should link against, so this cleans before and after.
coverage:
	$(MAKE) clean
	$(MAKE) test HOST_EXTRA_FLAGS="--coverage -O0 -Wno-error"
	python3 tools/coverage.py
	$(MAKE) clean

FORCE:

.PHONY: all clean coverage test verify FORCE
