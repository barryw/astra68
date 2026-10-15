ifneq ($(KERNELRELEASE),)
obj-m += astra_display_capture.o
obj-m += astra_graphics_arena.o

ifeq ($(ASTRA_INCLUDE),)
$(error ASTRA_INCLUDE must name the matching Astra source headers)
endif
ccflags-y += -Wall -Wextra -Werror -I$(ASTRA_INCLUDE)
else
CC := $(CROSS_COMPILE)gcc
ROOT := ../../..
OUT ?= $(ROOT)/build/de25-capture
CERTIFIER := $(OUT)/astra-display-capture-certify
BANDWIDTH := $(OUT)/astra-arena-bandwidth
REMOTE_DESKTOP := $(OUT)/astra-remote-desktop
CFLAGS := -std=c11 -O2 -pipe -Wall -Wextra -Werror -Wformat=2 \
	-Wshadow -Wstrict-prototypes -D_FORTIFY_SOURCE=2 \
	-I$(ROOT)/sw/include
DE25_SYSROOT ?= $(HOME)/.cache/astra68/de25-jammy-arm64
KERNEL_SOURCE ?= $(HOME)/astra-linux-6.12.11
KERNEL_OUTPUT ?= $(HOME)/astra-linux-6.12.11-build
MODULE_OUT ?= $(ROOT)/build/de25-linux/module
EXPECTED_KERNEL_RELEASE ?= 6.12.11-gd7d192a9ddd9
QEMU_USER ?= qemu-aarch64
HOST_CC ?= cc
STORAGE_CATALOG_SOURCES := astra_storage_catalog.c test_storage_catalog.c \
	$(ROOT)/sw/common/utf8.c
STORAGE_CATALOG_FLAGS := -std=gnu11 -O2 -Wall -Wextra -Werror \
	-Wshadow -Wstrict-prototypes -I$(ROOT)/ndk/include -I$(ROOT)/sw/include
STORAGE_PROBE_SOURCES := astra_storage_probe.c test_storage_probe.c
STORAGE_PROBE_LIBS := -lblkid
STORAGE_RESOLVER := $(ROOT)/emu/qemu/qemu-9.2/hw/m68k
STORAGE_MOUNT_SOURCES := astra_storage_mount.c test_storage_mount.c \
	$(STORAGE_RESOLVER)/astra68-resolve.c
STORAGE_PATH_SOURCES := astra_storage_path.c astra_storage_transport.c test_storage_path.c \
	$(STORAGE_RESOLVER)/astra68-resolve.c
STORAGE_PROVIDER_SOURCES := astra_storage_provider.c astra_storage_discovery.c astra_storage_catalog.c \
	astra_storage_mount.c astra_storage_probe.c astra_storage_path.c astra_storage_work.c \
	astra_storage_transport.c \
	$(STORAGE_RESOLVER)/astra68-resolve.c $(ROOT)/sw/common/utf8.c
STORAGE_HEADERS := astra_storage_catalog.h astra_storage_discovery.h astra_storage_probe.h \
	astra_storage_mount.h astra_storage_path.h astra_storage_provider.h \
	astra_storage_work.h astra_storage_transport.h \
	$(ROOT)/sw/include/astra/storage_provider.h $(ROOT)/sw/include/astra/volume_abi.h \
	$(STORAGE_RESOLVER)/astra68-resolve.h
COMPILER_INCLUDE := $(shell $(CC) -print-file-name=include)
REMOTE_CFLAGS := $(CFLAGS) -I$(ROOT)/ndk/include --sysroot=$(DE25_SYSROOT) -nostdinc \
	-I$(COMPILER_INCLUDE) \
	-isystem $(DE25_SYSROOT)/usr/include/aarch64-linux-gnu \
	-isystem $(DE25_SYSROOT)/usr/include
REMOTE_LDFLAGS := --sysroot=$(DE25_SYSROOT) \
	-Wl,-rpath-link,$(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu \
	-lvncserver

.PHONY: all remote-desktop modules test clean storage-catalog-test \
	storage-catalog-sanitize storage-catalog-target-test storage-probe-test \
	storage-probe-target-test storage-mount-test storage-path-test storage-provider-test
.PHONY: storage-provider-sanitize
.PHONY: storage-cleanup-test storage-cleanup-sanitize
.PHONY: storage-provider storage-provider-target
.PHONY: storage-launcher-test

$(OUT)/test-storage-client: astra_storage_transport.c test_storage_client.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) \
		astra_storage_transport.c test_storage_client.c -o $@

$(OUT)/test-storage-client-aarch64: astra_storage_transport.c test_storage_client.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 -I$(STORAGE_RESOLVER) \
		astra_storage_transport.c test_storage_client.c -o $@

storage-launcher-test: $(OUT)/astra-storage $(OUT)/test-storage-client
	python3 test_storage_launcher.py $^

storage-provider: $(OUT)/astra-storage

storage-provider-target: $(OUT)/astra-storage-aarch64

$(OUT)/astra-storage: $(STORAGE_PROVIDER_SOURCES) astra_storage_main.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) \
		$(STORAGE_PROVIDER_SOURCES) astra_storage_main.c -o $@ -lblkid -lmount -ludev

$(OUT)/astra-storage-aarch64: $(STORAGE_PROVIDER_SOURCES) astra_storage_main.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 -I$(ROOT)/ndk/include -I$(STORAGE_RESOLVER) \
		$(STORAGE_PROVIDER_SOURCES) astra_storage_main.c -o $@ \
		-Wl,-rpath-link,$(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu -lblkid -lmount -ludev

all: $(CERTIFIER) $(BANDWIDTH)

modules:
	test "$$(cat $(KERNEL_OUTPUT)/include/config/kernel.release)" = "$(EXPECTED_KERNEL_RELEASE)"
	mkdir -p $(MODULE_OUT)
	cp Makefile astra_graphics_arena.c astra_de25_media.h \
		astra_host_arena_uapi.h astra_display_capture.c \
		astra_display_capture_uapi.h $(MODULE_OUT)/
	$(MAKE) -C $(KERNEL_SOURCE) O=$(KERNEL_OUTPUT) \
		M=$(abspath $(MODULE_OUT)) ARCH=arm64 \
		ASTRA_INCLUDE=$(abspath $(ROOT)/sw/include) \
		CROSS_COMPILE=aarch64-linux-gnu- LOCALVERSION= modules
	test "$$(modinfo -F vermagic $(MODULE_OUT)/astra_graphics_arena.ko | \
		sed 's/ *$$//')" = '$(EXPECTED_KERNEL_RELEASE) SMP preempt mod_unload aarch64'

$(BANDWIDTH): astra_arena_bandwidth.c astra_de25_media.h Makefile
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@

remote-desktop: $(REMOTE_DESKTOP)

$(CERTIFIER): astra_display_capture_certify.c \
		astra_display_capture_uapi.h \
		$(ROOT)/sw/include/astra/display_capture.h Makefile
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@

$(REMOTE_DESKTOP): astra_remote_desktop.c astra_display_capture_uapi.h \
		$(ROOT)/sw/include/astra/display_capture.h \
		$(ROOT)/sw/include/astra/audio_host.h Makefile
	test -r $(DE25_SYSROOT)/usr/include/rfb/rfb.h
	test -r $(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu/libvncserver.so
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) $< -o $@ $(REMOTE_LDFLAGS)
	$(QEMU_USER) -L $(DE25_SYSROOT) $@ --self-test

test: $(REMOTE_DESKTOP)
	python3 test_remote_desktop_client.py
	python3 test_remote_desktop_lifecycle.py $(REMOTE_DESKTOP) \
		--qemu-user $(QEMU_USER) --sysroot $(DE25_SYSROOT)

$(OUT)/test-storage-catalog: $(STORAGE_CATALOG_SOURCES) \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) $(STORAGE_CATALOG_SOURCES) -o $@

$(OUT)/test-storage-catalog-sanitize: $(STORAGE_CATALOG_SOURCES) \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -g -fsanitize=address,undefined \
		-fno-omit-frame-pointer $(STORAGE_CATALOG_SOURCES) -o $@

$(OUT)/test-storage-catalog-aarch64: $(STORAGE_CATALOG_SOURCES) \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 -I$(ROOT)/ndk/include \
		$(STORAGE_CATALOG_SOURCES) -o $@

$(OUT)/test-storage-probe: $(STORAGE_PROBE_SOURCES) \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) $(STORAGE_PROBE_SOURCES) \
		-o $@ $(STORAGE_PROBE_LIBS)

$(OUT)/test-storage-probe-aarch64: $(STORAGE_PROBE_SOURCES) \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 $(STORAGE_PROBE_SOURCES) \
		-o $@ -Wl,-rpath-link,$(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu \
		$(STORAGE_PROBE_LIBS)

$(OUT)/test-storage-mount: $(STORAGE_MOUNT_SOURCES) \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) \
		$(STORAGE_MOUNT_SOURCES) -o $@ -lmount

$(OUT)/test-storage-mount-aarch64: $(STORAGE_MOUNT_SOURCES) \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 -I$(STORAGE_RESOLVER) $(STORAGE_MOUNT_SOURCES) \
		-o $@ -Wl,-rpath-link,$(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu -lmount

storage-catalog-test: $(OUT)/test-storage-catalog
	$<

storage-catalog-sanitize: $(OUT)/test-storage-catalog-sanitize
	$<

storage-catalog-target-test: $(OUT)/test-storage-catalog-aarch64
	$(QEMU_USER) -L $(DE25_SYSROOT) $<

storage-probe-test: $(OUT)/test-storage-probe
	python3 test_storage_probe.py $<

storage-probe-target-test: $(OUT)/test-storage-probe-aarch64
	python3 test_storage_probe.py $< \
		--qemu-user $(QEMU_USER) --sysroot $(DE25_SYSROOT)

storage-mount-test: $(OUT)/test-storage-mount
	python3 test_storage_mount.py $<

$(OUT)/test-storage-path: $(STORAGE_PATH_SOURCES) $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) \
		-I$(STORAGE_RESOLVER) $(STORAGE_PATH_SOURCES) -o $@

$(OUT)/test-storage-path-aarch64: $(STORAGE_PATH_SOURCES) $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 -I$(STORAGE_RESOLVER) \
		$(STORAGE_PATH_SOURCES) -o $@

storage-path-test: $(OUT)/test-storage-path
	$<

$(OUT)/test-storage-provider: $(STORAGE_PROVIDER_SOURCES) test_storage_provider.c \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) \
		$(STORAGE_PROVIDER_SOURCES) test_storage_provider.c -o $@ -lblkid -lmount -ludev

$(OUT)/test-storage-provider-aarch64: $(STORAGE_PROVIDER_SOURCES) test_storage_provider.c \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 -I$(ROOT)/ndk/include -I$(STORAGE_RESOLVER) \
		$(STORAGE_PROVIDER_SOURCES) test_storage_provider.c -o $@ \
		-Wl,-rpath-link,$(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu -lblkid -lmount -ludev

storage-provider-test: $(OUT)/test-storage-provider
	python3 test_storage_mount.py $< --provider

$(OUT)/test-storage-provider-sanitize: $(STORAGE_PROVIDER_SOURCES) test_storage_provider.c \
		$(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(STORAGE_PROVIDER_SOURCES) test_storage_provider.c -o $@ -lblkid -lmount -ludev

.PHONY: storage-discovery-test storage-discovery-sanitize
$(OUT)/test-storage-discovery: $(STORAGE_PROVIDER_SOURCES) test_storage_discovery.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) \
		$(STORAGE_PROVIDER_SOURCES) test_storage_discovery.c -o $@ -lblkid -lmount -ludev

$(OUT)/test-storage-discovery-sanitize: $(STORAGE_PROVIDER_SOURCES) test_storage_discovery.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(STORAGE_PROVIDER_SOURCES) test_storage_discovery.c -o $@ -lblkid -lmount -ludev

$(OUT)/test-storage-discovery-aarch64: $(STORAGE_PROVIDER_SOURCES) test_storage_discovery.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(CC) $(REMOTE_CFLAGS) -std=gnu11 -I$(ROOT)/ndk/include -I$(STORAGE_RESOLVER) \
		$(STORAGE_PROVIDER_SOURCES) test_storage_discovery.c -o $@ \
		-Wl,-rpath-link,$(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu -lblkid -lmount -ludev

storage-discovery-test: $(OUT)/test-storage-discovery
	$<

storage-discovery-sanitize: $(OUT)/test-storage-discovery-sanitize
	$<

storage-provider-sanitize: $(OUT)/test-storage-provider-sanitize
	python3 test_storage_mount.py $< --provider

STORAGE_CLEANUP_SOURCES := test_storage_cleanup.c $(filter-out astra_storage_provider.c,$(STORAGE_PROVIDER_SOURCES))
$(OUT)/test-storage-cleanup: $(STORAGE_CLEANUP_SOURCES) astra_storage_provider.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) -ffunction-sections -fdata-sections \
		$(STORAGE_CLEANUP_SOURCES) -Wl,--gc-sections -o $@ -lblkid -lmount -ludev

$(OUT)/test-storage-cleanup-sanitize: $(STORAGE_CLEANUP_SOURCES) astra_storage_provider.c $(STORAGE_HEADERS) Makefile
	mkdir -p $(OUT)
	$(HOST_CC) $(STORAGE_CATALOG_FLAGS) -I$(STORAGE_RESOLVER) -g -ffunction-sections -fdata-sections \
		-fsanitize=address,undefined -fno-omit-frame-pointer \
		$(STORAGE_CLEANUP_SOURCES) -Wl,--gc-sections -o $@ -lblkid -lmount -ludev

storage-cleanup-test: $(OUT)/test-storage-cleanup
	$<

storage-cleanup-sanitize: $(OUT)/test-storage-cleanup-sanitize
	$<

clean:
	rm -f $(CERTIFIER) $(BANDWIDTH) $(REMOTE_DESKTOP)
endif
