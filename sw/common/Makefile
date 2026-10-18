ROOT := ../..
HOST_CC ?= cc
HOST_AR ?= ar
TARGET_CC ?= m68k-astra-gcc
TARGET_AR ?= m68k-astra-ar
AARCH64_CC ?= aarch64-linux-gnu-gcc
AARCH64_AR ?= aarch64-linux-gnu-ar
CPPFLAGS := -I$(ROOT)/sw/include -I$(ROOT)/ndk/include
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -pedantic
SOURCES := afnt.c json.c crc32.c utf8.c font_catalog.c sha256.c font_store.c
HOST_OBJECTS := $(patsubst %.c,build/host/%.o,$(SOURCES))
M68K_OBJECTS := $(patsubst %.c,build/m68k/%.o,$(SOURCES))
AARCH64_OBJECTS := $(patsubst %.c,build/aarch64/%.o,$(SOURCES))

all: host m68k aarch64
host: build/host/libastraafnt.a
m68k: build/m68k/libastraafnt.a
aarch64: build/aarch64/libastraafnt.a

build/host/%.o: %.c $(ROOT)/sw/include/astra/afnt.h $(ROOT)/sw/include/astra/json.h $(ROOT)/sw/include/astra/crc32.h $(ROOT)/sw/include/astra/font_catalog.h $(ROOT)/sw/include/astra/sha256.h $(ROOT)/sw/include/astra/font_store.h $(ROOT)/ndk/include/astra/utf8.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/m68k/%.o: %.c $(ROOT)/sw/include/astra/afnt.h $(ROOT)/sw/include/astra/json.h $(ROOT)/sw/include/astra/crc32.h $(ROOT)/sw/include/astra/font_catalog.h $(ROOT)/sw/include/astra/sha256.h $(ROOT)/sw/include/astra/font_store.h $(ROOT)/ndk/include/astra/utf8.h
	@mkdir -p $(@D)
	$(TARGET_CC) $(CPPFLAGS) $(CFLAGS) -m68040 -ffixed-a4 \
		-ffreestanding -fno-builtin -fPIC -c $< -o $@

build/aarch64/%.o: %.c $(ROOT)/sw/include/astra/afnt.h $(ROOT)/sw/include/astra/json.h $(ROOT)/sw/include/astra/crc32.h $(ROOT)/sw/include/astra/font_catalog.h $(ROOT)/sw/include/astra/sha256.h $(ROOT)/sw/include/astra/font_store.h $(ROOT)/ndk/include/astra/utf8.h
	@mkdir -p $(@D)
	$(AARCH64_CC) $(CPPFLAGS) $(CFLAGS) -fPIC -c $< -o $@

build/host/test-font-store: tests/test_font_store.c font_store.c sha256.c $(ROOT)/sw/include/astra/font_store.h $(ROOT)/sw/include/astra/sha256.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_font_store.c font_store.c sha256.c -o $@

build/host/test-afnt-glyph: tests/test_afnt_glyph.c afnt.c json.c crc32.c utf8.c $(ROOT)/sw/include/astra/afnt.h $(ROOT)/sw/include/astra/json.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_afnt_glyph.c afnt.c json.c crc32.c utf8.c -o $@

build/host/test-json: tests/test_json.c json.c utf8.c $(ROOT)/sw/include/astra/json.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) tests/test_json.c json.c utf8.c -o $@

test: build/host/test-font-store build/host/test-afnt-glyph build/host/test-json
	./build/host/test-font-store
	./build/host/test-afnt-glyph
	./build/host/test-json

sanitize: tests/test_font_store.c font_store.c sha256.c
	@mkdir -p build/host
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_font_store.c font_store.c sha256.c -o build/host/test-font-store-sanitize
	./build/host/test-font-store-sanitize
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_afnt_glyph.c afnt.c json.c crc32.c utf8.c -o build/host/test-afnt-glyph-sanitize
	./build/host/test-afnt-glyph-sanitize
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_json.c json.c utf8.c -o build/host/test-json-sanitize
	./build/host/test-json-sanitize

build/host/libastraafnt.a: $(HOST_OBJECTS)
	$(HOST_AR) rcs $@ $^

build/m68k/libastraafnt.a: $(M68K_OBJECTS)
	$(TARGET_AR) rcs $@ $^

build/aarch64/libastraafnt.a: $(AARCH64_OBJECTS)
	$(AARCH64_AR) rcs $@ $^

clean:
	rm -rf build

analyze:
	@mkdir -p build/analyzer
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -fanalyzer -c json.c -o build/analyzer/json.o
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -fanalyzer -c afnt.c -o build/analyzer/afnt.o

.PHONY: all host m68k aarch64 test sanitize analyze clean
