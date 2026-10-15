HOST_CC ?= cc
ROOT := ../../../..
CPPFLAGS := -I$(ROOT)/ndk/include -I$(ROOT)/sw/include -I../../runtime/include
CFLAGS := -std=c11 -O2 -Wall -Wextra -Werror -pedantic -ffunction-sections -fdata-sections
SOURCES := volume_cli.c test_volume_cli.c
HEADERS := volume_cli.h $(ROOT)/ndk/include/astra/volume.h $(ROOT)/sw/include/astra/volume_abi.h

.PHONY: test sanitize
test: build/test_volume_cli
	$<

sanitize: build/test_volume_cli_sanitize
	$<

build/test_volume_cli: $(SOURCES) $(HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) -Wl,--gc-sections -o $@

build/test_volume_cli_sanitize: $(SOURCES) $(HEADERS) Makefile
	mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) -g -fsanitize=address,undefined -fno-omit-frame-pointer \
		$(SOURCES) -Wl,--gc-sections -o $@
