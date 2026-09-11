# Astra OS and NDK releases share the public version contract.
ASTRA_VERSION_HEADER := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../ndk/include/astra/version.h)
ASTRA_OS_VERSION := $(shell sed -n 's/^\#define ASTRA_OS_VERSION_STRING //p' $(ASTRA_VERSION_HEADER) | tr -d '"')
ifeq ($(ASTRA_OS_VERSION),)
$(error missing ASTRA_OS_VERSION_STRING in $(ASTRA_VERSION_HEADER))
endif
