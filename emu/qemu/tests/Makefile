CC ?= cc
CFLAGS ?= -O2 -std=gnu11 -Wall -Wextra -Werror
M68K := ../qemu-9.2/hw/m68k

build/test-astra68-resolve-sanitize: CFLAGS += -g -fsanitize=address,undefined -fno-omit-frame-pointer

build/test-astra68-resolve build/test-astra68-resolve-sanitize: test-astra68-resolve.c $(M68K)/astra68-resolve.c $(M68K)/astra68-resolve.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(M68K) $< $(M68K)/astra68-resolve.c -o $@

test: build/test-astra68-resolve
	./build/test-astra68-resolve

sanitize: build/test-astra68-resolve-sanitize
	./build/test-astra68-resolve-sanitize

.PHONY: test sanitize
