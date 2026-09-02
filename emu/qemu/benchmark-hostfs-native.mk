CROSS ?= arm-linux-gnueabihf-
CC := $(CROSS)gcc
BUILD ?= build/native-hostfs
TARGET := $(BUILD)/benchmark-hostfs-native
CFLAGS := -std=c11 -O3 -pipe -Wall -Wextra -Werror -Wformat=2 \
	-Wshadow -Wstrict-prototypes -D_FORTIFY_SOURCE=2 \
	-ffunction-sections -fdata-sections
LDFLAGS := -static -pthread -Wl,--gc-sections

.PHONY: all analyze clean

all: $(TARGET)

$(TARGET): benchmark-hostfs-native.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $< $(LDFLAGS) -o $@

analyze:
	$(CC) $(CFLAGS) -fanalyzer -fsyntax-only benchmark-hostfs-native.c

clean:
	rm -rf $(BUILD)
