ifndef ASTRA_M68K_CROSS_MK
ASTRA_M68K_CROSS_MK := 1
ASTRA_M68K_CROSS_FILE := $(abspath $(lastword $(MAKEFILE_LIST)))
ASTRA_M68K_CROSS_DIR := $(dir $(ASTRA_M68K_CROSS_FILE))
ASTRA_REPOSITORY_BUILD_ROOT := $(abspath $(ASTRA_M68K_CROSS_DIR)/..)

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
ASTRA_TARGET_ABI_FLAGS ?= -m68040 -msoft-float -ffixed-a4 -D__astra__=1

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
	  for archive in libc.a libm.a libgcc.a libgcc_builtins_shared.a \
		libgcc_unwind_shared.a libstdc++.a libstdc++_shared.a \
		libsupc++.a crtbegin.o crtend.o crtbeginS.o crtendS.o; do \
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

# Build identity is immutable within one Make invocation.  A recursive value
# used to recalculate after every generated .d include, both forking hundreds
# of checksum processes and allowing one invocation to name multiple identity
# stamps.  Hash source policy files once; generated dependency files remain
# normal prerequisites and must never redefine the toolchain.
ASTRA_BUILD_POLICY_INPUTS := $(sort \
	$(filter-out %.d,$(MAKEFILE_LIST)) \
	$(wildcard $(ASTRA_M68K_CROSS_DIR)*.mk \
		$(ASTRA_M68K_CROSS_DIR)*.ld \
		$(ASTRA_REPOSITORY_BUILD_ROOT)/sw/userspace/program.mk \
		$(ASTRA_REPOSITORY_BUILD_ROOT)/sw/userspace/libraries.mk \
		$(ASTRA_REPOSITORY_BUILD_ROOT)/sw/userspace/runtime/*.mk \
		$(ASTRA_REPOSITORY_BUILD_ROOT)/sw/userspace/runtime/*.ld))
ASTRA_TOOLCHAIN_ID := $(shell \
	{ printf '%s\n' '$(ASTRA_TOOLCHAIN_CONTENT_ID)' '$(ASTRA_DEBUG_FLAGS)' \
		'$(ASTRA_TARGET_ABI_FLAGS)' '$(CPPFLAGS)' '$(CFLAGS)' \
		'$(CXXFLAGS)' '$(COMMON_FLAGS)' '$(TARGET_FLAGS)' \
		'$(CXX_TARGET_FLAGS)' '$(LIBRARY_FLAGS)' \
		'$(ASTRA_SHARED_PIC_FLAGS)' '$(ASTRA_SHARED_LINK_FLAGS)' \
		'$(LDFLAGS)' '$(LINK_FLAGS)'; \
	  for makefile in $(ASTRA_BUILD_POLICY_INPUTS); do \
		cksum "$$makefile"; \
	  done; } | \
	cksum | awk '{print $$1 "-" $$2}')
ASTRA_TOOLCHAIN_STAMP := build/.toolchain/$(ASTRA_TOOLCHAIN_ID)

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
	$(AR) rcs $@.tmp $(filter %.o,$^)
	mv $@.tmp $@
endef

# Keep the loader's supported m68k relocation set in one build-time gate so a
# Kit cannot reach startup with a relocation the eager dynamic loader rejects.
# Symbol relocations are required for real shared dependencies; text
# relocations remain forbidden.
define ASTRA_CHECK_DSO
	@set -e; for library in $(1); do \
		header=$$($(READELF) -hW "$$library"); \
		program=$$($(READELF) -lW "$$library"); \
		dynamic=$$($(READELF) -dW "$$library"); \
		printf '%s\n' "$$header" | grep -q 'Type:.*DYN'; \
		printf '%s\n' "$$program" | grep -q ' DYNAMIC '; \
		printf '%s\n' "$$program" | grep -q ' GNU_RELRO '; \
		! printf '%s\n' "$$program" | grep -q ' INTERP '; \
		printf '%s\n' "$$dynamic" | grep -q SONAME; \
		printf '%s\n' "$$dynamic" | grep -q BIND_NOW; \
		! printf '%s\n' "$$dynamic" | grep -q TEXTREL; \
		leaked=$$($(READELF) --wide --dyn-syms "$$library" | awk \
			'$$7 != "UND" && $$8 ~ /^(astra_library|ASTRA_(PAGE_SIZE|LIBRARY_(METADATA|SIZE|EXPORTS_ADDRESS))|__astra_library_(start|end))(@@?.*)?$$/ {print $$8}'); \
		test -z "$$leaked" || { \
			printf '%s: leaked shared-library layout symbols:\n%s\n' \
				"$$library" "$$leaked" >&2; \
			exit 1; \
		}; \
		unsupported=$$($(READELF) -rW "$$library" | awk \
			'$$3 ~ /^R_68K_/ && $$3 !~ /^R_68K_(NONE|32|PC32|GLOB_DAT|JMP_SLOT|RELATIVE|TLS_DTPMOD32|TLS_DTPREL32|TLS_TPREL32)$$/ {print $$3}' | \
			sort -u); \
		test -z "$$unsupported" || { \
			printf '%s: unsupported dynamic relocations:\n%s\n' \
				"$$library" "$$unsupported" >&2; \
			exit 1; \
		}; \
	done
endef

endif
