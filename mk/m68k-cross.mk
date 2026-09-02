ifndef ASTRA_M68K_CROSS_MK
ASTRA_M68K_CROSS_MK := 1

# Select an installed freestanding m68k toolchain while preserving CROSS=...
# supplied by callers and release automation.
ifeq ($(origin CROSS), undefined)
ifneq ($(shell command -v m68k-astra-gcc 2>/dev/null),)
CROSS = m68k-astra-
else ifneq ($(shell command -v m68k-linux-gnu-gcc 2>/dev/null),)
CROSS = m68k-linux-gnu-
else ifneq ($(shell command -v m68k-elf-gcc 2>/dev/null),)
CROSS = m68k-elf-
else
CROSS = m68k-linux-gnu-
endif
endif

# Debug information for every m68k object. It costs nothing in what ships: the
# kernel is objcopy'd to a raw binary and the user image is stripped before it
# is packed, so both payloads are byte-identical with and without this. What it
# buys is a debugger that can name a function and a line instead of answering
# `?? ()`, which is the difference between diagnosing a fault and guessing at
# one. Set ASTRA_DEBUG_FLAGS= to build without it.
ASTRA_DEBUG_FLAGS ?= -g

# A4 is Astra's thread pointer register. GNU m68k local-exec TLS still calls
# __m68k_read_tp; reserving A4 makes that helper one move while leaving A5 to
# the established PIC/GOT ABI used by Kits.
ASTRA_TARGET_ABI_FLAGS ?= -m68030 -msoft-float -ffixed-a4 -D__astra__=1

# An Astra compiler owns its matching libc headers and archives.  Keeping a
# second path in each program is how old headers were mixed with a new libc.
ASTRA_TOOLCHAIN_TRIPLE := $(shell "$(CROSS)gcc" -dumpmachine 2>/dev/null)
ifeq ($(ASTRA_TOOLCHAIN_TRIPLE),m68k-astra)
PICOLIBC ?= $(shell "$(CROSS)gcc" -print-sysroot 2>/dev/null)
endif
PICOLIBC ?= $(HOME)/picolibc-astra

# Make does not otherwise notice changed tools, sysroot contents, or flags.
# Hash them once per make process and attach the build identity invisibly to
# every target. Recursive sub-makes inherit the expensive content hash.
ifndef ASTRA_TOOLCHAIN_CONTENT_ID
ASTRA_TOOLCHAIN_CONTENT_ID := $(shell \
	cc=$$(command -v "$(CROSS)gcc" 2>/dev/null) || { printf missing; exit; }; \
	prefix=$$(CDPATH= cd -- "$$(dirname "$$cc")/.." && pwd); \
	triple=$$("$$cc" -dumpmachine 2>/dev/null); \
	{ printf 'cc=%s\ntriple=%s\nsysroot=%s\n' \
		"$$cc" "$$triple" "$(PICOLIBC)"; \
	  "$$cc" -dumpspecs 2>/dev/null; \
	  for name in cc1 cc1plus as ld; do \
		path=$$("$$cc" -print-prog-name=$$name 2>/dev/null); \
		path=$$(command -v "$$path" 2>/dev/null || printf '%s' "$$path"); \
		printf '%s=%s\n' "$$name" "$$path"; \
		test ! -f "$$path" || cksum "$$path"; \
	  done; \
	  for archive in libc.a libm.a libgcc.a libstdc++.a libsupc++.a; do \
		path=$$("$$cc" -print-file-name=$$archive 2>/dev/null); \
		test ! -f "$$path" || cksum "$$path"; \
	  done; \
	  for directory in "$(PICOLIBC)/include" \
		"$$prefix/$$triple/include/c++"; do \
		test ! -d "$$directory" || \
			find "$$directory" -type f -exec cksum {} + | sort; \
	  done; } | cksum | awk '{print $$1 "-" $$2}')
export ASTRA_TOOLCHAIN_CONTENT_ID
endif

ASTRA_TOOLCHAIN_ID = $(shell \
	printf '%s\n' '$(ASTRA_TOOLCHAIN_CONTENT_ID)' '$(ASTRA_DEBUG_FLAGS)' \
		'$(ASTRA_TARGET_ABI_FLAGS)' '$(CPPFLAGS)' '$(CFLAGS)' \
		'$(CXXFLAGS)' '$(TARGET_FLAGS)' '$(CXX_TARGET_FLAGS)' \
		'$(LDFLAGS)' '$(LINK_FLAGS)' | \
	cksum | awk '{print $$1 "-" $$2}')
ASTRA_TOOLCHAIN_STAMP = build/.toolchain/$(ASTRA_TOOLCHAIN_ID)

ASTRA_EXISTING_PRODUCTS := $(shell test ! -d build || \
	find build -type f ! -path 'build/.toolchain/*')

$(ASTRA_EXISTING_PRODUCTS): .EXTRA_PREREQS = $(ASTRA_TOOLCHAIN_STAMP)

build/.toolchain/%:
	@mkdir -p $(@D)
	@touch $@

# `ar r` leaves members that are no longer named by the build.  A renamed or
# removed object can therefore survive indefinitely and win symbol selection.
# Recreate archives off to the side, then publish the exact member set at once.
define ASTRA_REPLACE_ARCHIVE
	@mkdir -p $(@D)
	rm -f $@.tmp
	$(AR) rcs $@.tmp $^
	mv $@.tmp $@
endef

# Astra's loader accepts only base-relative relocations into writable segments.
# Keep that contract in one place so every Kit rejects non-PIC inputs at build
# time instead of failing later during system startup.
define ASTRA_CHECK_DSO
	@set -e; for library in $(1); do \
		dynamic=$$($(READELF) -dW "$$library"); \
		! printf '%s\n' "$$dynamic" | grep -q TEXTREL; \
		relocations=$$($(READELF) -rW "$$library"); \
		! printf '%s\n' "$$relocations" | \
			grep -E 'R_68K_(GLOB_DAT|JMP_SLOT|32|PC32)'; \
	done
endef

endif
