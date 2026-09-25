ifneq ($(KERNELRELEASE),)
obj-m += astra_display_capture.o

ccflags-y += -Wall -Wextra -Werror -I$(src)/../../../sw/include
else
CC := $(CROSS_COMPILE)gcc
ROOT := ../../..
OUT ?= $(ROOT)/build/de25-capture
CERTIFIER := $(OUT)/astra-display-capture-certify
REMOTE_DESKTOP := $(OUT)/astra-remote-desktop
CFLAGS := -std=c11 -O2 -pipe -Wall -Wextra -Werror -Wformat=2 \
	-Wshadow -Wstrict-prototypes -D_FORTIFY_SOURCE=2 \
	-I$(ROOT)/sw/include
DE25_SYSROOT ?= $(HOME)/.cache/astra68/de25-jammy-arm64
QEMU_USER ?= qemu-aarch64
COMPILER_INCLUDE := $(shell $(CC) -print-file-name=include)
REMOTE_CFLAGS := $(CFLAGS) --sysroot=$(DE25_SYSROOT) -nostdinc \
	-I$(COMPILER_INCLUDE) \
	-isystem $(DE25_SYSROOT)/usr/include/aarch64-linux-gnu \
	-isystem $(DE25_SYSROOT)/usr/include
REMOTE_LDFLAGS := --sysroot=$(DE25_SYSROOT) \
	-Wl,-rpath-link,$(DE25_SYSROOT)/usr/lib/aarch64-linux-gnu \
	-lvncserver

.PHONY: all remote-desktop test clean

all: $(CERTIFIER)

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

clean:
	rm -f $(CERTIFIER) $(REMOTE_DESKTOP)
endif
