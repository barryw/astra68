ROOT := ../..
HOST_CC ?= cc
HOST_AR ?= ar
TARGET_CC ?= m68k-astra-gcc
TARGET_AR ?= m68k-astra-ar
AARCH64_CC ?= aarch64-linux-gnu-gcc
AARCH64_AR ?= aarch64-linux-gnu-ar
CPPFLAGS := -I$(ROOT)/sw/include
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -pedantic
SOURCES := afnt.c crc32.c
HOST_OBJECTS := $(patsubst %.c,build/host/%.o,$(SOURCES))
M68K_OBJECTS := $(patsubst %.c,build/m68k/%.o,$(SOURCES))
AARCH64_OBJECTS := $(patsubst %.c,build/aarch64/%.o,$(SOURCES))

all: host m68k aarch64
host: build/host/libastraafnt.a
m68k: build/m68k/libastraafnt.a
aarch64: build/aarch64/libastraafnt.a

build/host/%.o: %.c $(ROOT)/sw/include/astra/afnt.h $(ROOT)/sw/include/astra/crc32.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/m68k/%.o: %.c $(ROOT)/sw/include/astra/afnt.h $(ROOT)/sw/include/astra/crc32.h
	@mkdir -p $(@D)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -m68040 -ffixed-a4 \
		-ffreestanding -fno-builtin -fPIC -c $< -o $@

build/aarch64/%.o: %.c $(ROOT)/sw/include/astra/afnt.h $(ROOT)/sw/include/astra/crc32.h
	@mkdir -p $(@D)
	$(AARCH64_CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

build/host/libastraafnt.a: $(HOST_OBJECTS)
	$(HOST_AR) rcs $@ $^

build/m68k/libastraafnt.a: $(M68K_OBJECTS)
	$(TARGET_AR) rcs $@ $^

build/aarch64/libastraafnt.a: $(AARCH64_OBJECTS)
	$(AARCH64_AR) rcs $@ $^

clean:
	rm -rf build

.PHONY: all host m68k aarch64 clean
