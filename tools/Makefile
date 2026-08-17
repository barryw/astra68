HOST_CC ?= cc
CPPFLAGS := -I../ndk/include -I../sw/include
CFLAGS := -O2 -std=c11 -Wall -Wextra -Werror

all: build/astra-bundle

build/astra-bundle: bundle_tool.c ../ndk/src/bundle.c \
		../ndk/include/astra/bundle.h
	@mkdir -p $(@D)
	$(HOST_CC) $(CPPFLAGS) $(CFLAGS) bundle_tool.c ../ndk/src/bundle.c -o $@

test: build/astra-bundle
	python3 ./tests/test_bundle_tool.py ./build/astra-bundle

clean:
	rm -rf build

.PHONY: all test clean
