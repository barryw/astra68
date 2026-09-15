ifneq ($(KERNELRELEASE),)
obj-m += astra_display_capture.o

ccflags-y += -Wall -Wextra -Werror -I$(src)/../../../sw/include
else
CC := $(CROSS_COMPILE)gcc
ROOT := ../../..
OUT ?= $(ROOT)/build/de25-capture
CERTIFIER := $(OUT)/astra-display-capture-certify
CFLAGS := -std=c11 -O2 -pipe -Wall -Wextra -Werror -Wformat=2 \
	-Wshadow -Wstrict-prototypes -D_FORTIFY_SOURCE=2 \
	-I$(ROOT)/sw/include

.PHONY: all clean

all: $(CERTIFIER)

$(CERTIFIER): astra_display_capture_certify.c \
		astra_display_capture_uapi.h \
		$(ROOT)/sw/include/astra/display_capture.h Makefile
	mkdir -p $(OUT)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(CERTIFIER)
endif
