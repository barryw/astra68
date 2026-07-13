CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -g -std=c11 -Wall -Wextra -Werror -pedantic

TEST_BINARY := build/test_pmmu030

.PHONY: all test clean

all: $(TEST_BINARY)

test: $(TEST_BINARY)
	./$(TEST_BINARY)

$(TEST_BINARY): pmmu030.c pmmu030.h tests/test_pmmu030.c
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -I. pmmu030.c tests/test_pmmu030.c -o $@

clean:
	rm -rf build
