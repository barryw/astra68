include ../mk/m68k-cross.mk
include ../mk/astra-version.mk
include ../sw/userspace/libraries.mk

# The NDK is the public face of Astra: headers, documentation, examples, and
# the relocatable archive that packages them with the shipped libraries. The
# implementations live with the components that own them under sw/userspace;
# system.library is sw/userspace/system.

CC := $(CROSS)gcc
READELF := $(CROSS)readelf
HOST_CC ?= cc
HOST_CXX ?= c++
CPU_FLAGS ?= -m68040
SYSTEM := ../sw/userspace/system

CPPFLAGS := -Iinclude -I../sw/include -I../sw/userspace/runtime/include
TARGET_CFLAGS := $(CPU_FLAGS) -Os -ffreestanding -fno-builtin -std=c11 \
	-Wall -Wextra -Werror

EXAMPLE_OBJECTS := build/m68k/port_message_example.o \
	build/m68k/bulk_ring_example.o \
	build/m68k/interface_controls_example.o \
	build/m68k/interface_tabs_example.o \
	build/m68k/interface_scroll_example.o \
	build/m68k/interface_splitter_example.o \
	build/m68k/interface_disclosure_example.o \
	build/m68k/text_clipboard_example.o build/m68k/text_field_example.o \
	build/m68k/undo_example.o
# The kernel and service ABI headers applications see: the ones the public
# headers include, and those that declare exported functions. The rest of
# sw/include is firmware and private wire formats.
KERNEL_ABI_HEADERS := $(addprefix ../sw/include/astra/,address_space.h \
	application_service.h block.h bytes.h civil.h compiler.h display.h \
	divide.h event.h event_descriptor.h gui.h host.h input.h library.h \
	limits.h message_abi.h network.h ntp.h pcm_format.h posix_process.h \
	proc.h process.h program.h service_manager_abi.h shutdown.h status.h \
	object_abi.h stream_service.h syscall.h vfs_service.h)
# Headers of the third-party APIs the shipped libraries export:
# lua.library (Lua 5.5) and terminfo.library (ncurses' terminfo layer).
LUA_SOURCE := ../third_party/lua/src
LUA_HEADERS := lua.h luaconf.h lauxlib.h lualib.h
NCURSES_PREFIX ?= $(HOME)/ncurses-astra
TERMINFO_HEADERS := term.h termcap.h ncurses_dll.h
DIST_NAME := astra68-ndk-$(ASTRA_OS_VERSION)
DIST_DIR := build/dist
DIST_STAGE := $(DIST_DIR)/stage/$(DIST_NAME)
DIST_ARCHIVE := $(DIST_DIR)/$(DIST_NAME).tar.xz
DOC_STAMP := build/docs/.complete
DOC_INPUTS := $(shell find docs -type f) $(wildcard include/astra/*.h examples/*.c) \
	$(KERNEL_ABI_HEADERS)
OS_COMPONENTS := runtime system graphics vfs streams posix network events \
	config interface audio
NDK_EXTERNAL_LIBRARY_KEYS := $(ASTRA_LIBRARY_KEYS)
NDK_SHARED_LIBRARY_CONTRACTS := $(call astra_library_contract_owner_targets,\
	$(NDK_EXTERNAL_LIBRARY_KEYS))
NDK_SHARED_LIBRARY_ARTIFACTS := $(ASTRA_SHARED_LIBRARIES) \
	$(ASTRA_LOADER_LIBRARY)

all: header-contract

test: header-contract
	python3 tests/test_distribution_makefile.py
	python3 tests/test_documentation.py

check: test

build/m68k/%_example.o: examples/%.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(TARGET_CFLAGS) -c $< -o $@

example: $(EXAMPLE_OBJECTS)

$(DOC_STAMP): $(DOC_INPUTS)
	sh docs/build.sh all
	@test -f build/docs/html/index.html
	@test -f build/docs/astra68-ndk.pdf
	@touch $@

docs: $(DOC_STAMP)

docs-html:
	sh docs/build.sh html

docs-pdf:
	sh docs/build.sh pdf

docs-check:
	sh docs/build.sh check
	@touch $(DOC_STAMP)

docs-clean:
	rm -rf build/docs

loader-library-contract:
	$(MAKE) -C ../sw/userspace/loader contract

shared-library-contracts: loader-library-contract
	@set -eu; for owner in $(NDK_SHARED_LIBRARY_CONTRACTS); do \
		directory=$${owner%%:*}; \
		target=$${owner#*:}; \
		test "$$directory" != "$$owner" && test -n "$$target" || { \
			printf 'invalid Astra library contract owner: %s\n' \
				"$$owner" >&2; \
			exit 2; \
		}; \
		$(MAKE) -C "$$directory" "$$target" || exit $$?; \
	done

developer-components: shared-library-contracts
	$(MAKE) -C $(SYSTEM) all pic
	$(MAKE) -C ../sw/userspace/runtime all pic
	$(MAKE) -C ../sw/userspace/graphics all pic
	$(MAKE) -C ../sw/userspace/vfs all
	$(MAKE) -C ../sw/userspace/streams all
	$(MAKE) -C ../sw/userspace/posix all
	$(MAKE) -C ../sw/userspace/network all
	$(MAKE) -C ../sw/userspace/events all

$(DIST_ARCHIVE): developer-components $(DOC_STAMP)
	rm -rf $(DIST_STAGE)
	install -d $(DIST_STAGE)/include/posix $(DIST_STAGE)/include/SDL2 \
		$(DIST_STAGE)/lib/m68040 \
		$(DIST_STAGE)/make $(DIST_STAGE)/src $(DIST_STAGE)/examples \
		$(DIST_STAGE)/docs $(DIST_STAGE)/tools $(DIST_STAGE)/licenses
	$(MAKE) install-headers DESTDIR=$(abspath $(DIST_STAGE)) PREFIX=
	cp -R ../sw/userspace/posix/include/. $(DIST_STAGE)/include/posix/
	cp -R ../sw/userspace/sdl2/build/vendor/SDL2/include/. \
		$(DIST_STAGE)/include/SDL2/
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_net/include/SDL_net.h \
		$(DIST_STAGE)/include/SDL2/SDL_net.h
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_image/include/SDL_image.h \
		$(DIST_STAGE)/include/SDL2/SDL_image.h
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_mixer/include/SDL_mixer.h \
		$(DIST_STAGE)/include/SDL2/SDL_mixer.h
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_ttf/SDL_ttf.h \
		$(DIST_STAGE)/include/SDL2/SDL_ttf.h
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2/LICENSE.txt \
		$(DIST_STAGE)/licenses/SDL2.txt
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_net/LICENSE.txt \
		$(DIST_STAGE)/licenses/SDL2_net.txt
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_image/LICENSE.txt \
		$(DIST_STAGE)/licenses/SDL2_image.txt
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_mixer/LICENSE.txt \
		$(DIST_STAGE)/licenses/SDL2_mixer.txt
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_ttf/LICENSE.txt \
		$(DIST_STAGE)/licenses/SDL2_ttf.txt
	install -m 0644 ../sw/userspace/sdl2/build/vendor/SDL2_ttf/external/freetype/docs/FTL.TXT \
		$(DIST_STAGE)/licenses/FreeType.txt
	install -d $(DIST_STAGE)/include/lua $(DIST_STAGE)/include/ncursesw
	install -m 0644 $(addprefix $(LUA_SOURCE)/,$(LUA_HEADERS)) \
		$(DIST_STAGE)/include/lua/
	install -m 0644 $(addprefix $(NCURSES_PREFIX)/include/ncursesw/,\
		$(TERMINFO_HEADERS)) $(DIST_STAGE)/include/ncursesw/
	install -m 0644 README.md $(DIST_STAGE)/README.md
	install -m 0644 make/*.mk $(DIST_STAGE)/make/
	install -m 0644 ../sw/userspace/posix/kit/program.c \
		$(DIST_STAGE)/src/astra-posix-program.c
	install -m 0644 examples/*.c $(DIST_STAGE)/examples/
	install -m 0644 ../mk/astra-shared-library.ld \
		../sw/userspace/runtime/astra_user.ld \
		../sw/userspace/runtime/astra_static_user.ld \
		../sw/userspace/runtime/astra_user_contract.ld \
		../sw/userspace/runtime/astra_user_sections.ld \
		../sw/userspace/runtime/build/m68k/crt0.o \
		../sw/userspace/runtime/build/m68k/crt0-hosted.o \
		../sw/userspace/runtime/build/m68k/crt0-dynamic.o \
		$(SYSTEM)/build/m68k/libastra.a $(SYSTEM)/build/m68k/libastra-pic.a \
		../sw/userspace/runtime/build/m68k/libastrart.a \
		../sw/userspace/runtime/build/m68k/libastrart-pic.a \
		../sw/userspace/graphics/build/m68k/libastragraphics.a \
		../sw/userspace/graphics/build/m68k/libastragraphics-pic.a \
		../sw/userspace/vfs/build/m68k/libastravfs.a \
		../sw/userspace/streams/build/m68k/libastrastreams.a \
		../sw/userspace/posix/build/m68k/libastraposix.a \
		../sw/userspace/network/build/m68k/libastranetwork.a \
		../sw/userspace/events/build/m68k/libastraevents.a \
		$(DIST_STAGE)/lib/m68040/
	install -m 0644 $(NDK_SHARED_LIBRARY_ARTIFACTS) \
		$(DIST_STAGE)/lib/m68040/
	install -m 0755 ../tools/check_dynamic_executable.py \
		$(DIST_STAGE)/tools/
	install -m 0644 ../tools/check_dynamic_relocations.py \
		$(DIST_STAGE)/tools/
	mv $(DIST_STAGE)/lib/m68040/astra-shared-library.ld \
		$(DIST_STAGE)/lib/m68040/astra_library.ld
	cp -R build/docs/html $(DIST_STAGE)/docs/html
	cp -R build/docs/doxygen/xml $(DIST_STAGE)/docs/xml
	install -m 0644 build/docs/astra68-ndk.pdf $(DIST_STAGE)/docs/
	printf '%s\n' 'name=Astra 68 Native Developer Kit' \
		'version=$(ASTRA_OS_VERSION)' \
		'astra_os_version=$(ASTRA_OS_VERSION)' 'target=m68k-astra' \
		'cpu=mc68040' > $(DIST_STAGE)/METADATA
	cd $(DIST_STAGE) && find . -type f ! -name SHA256SUMS \
		-printf '%P\0' | LC_ALL=C sort -z | xargs -0 sha256sum \
		> SHA256SUMS
	@mkdir -p $(DIST_DIR)
	tar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
		-cJf $(DIST_ARCHIVE).tmp -C $(DIST_DIR)/stage $(DIST_NAME)
	mv $(DIST_ARCHIVE).tmp $(DIST_ARCHIVE)

dist-check: $(DIST_ARCHIVE)
	python3 tests/test_distribution.py $(DIST_ARCHIVE) $(ASTRA_OS_VERSION) \
		$(foreach library,$(NDK_SHARED_LIBRARY_ARTIFACTS),\
			--library $(notdir $(library)))
	rm -rf $(DIST_DIR)/check
	install -d $(DIST_DIR)/check
	tar -xJf $(DIST_ARCHIVE) -C $(DIST_DIR)/check
	$(MAKE) -f tests/distribution_smoke.mk \
		ASTRA_NDK_ROOT=$(abspath $(DIST_DIR)/check/$(DIST_NAME)) \
		BUILD_DIR=$(abspath $(DIST_DIR)/smoke) all

dist: dist-check
	@echo "NDK: $(DIST_ARCHIVE)"

os-library-tests:
	@set -e; for component in $(OS_COMPONENTS); do \
		$(MAKE) -C ../sw/userspace/$$component all test sanitize analyze; \
	done

certify: check example dist-check os-library-tests

sdk: certify

# Every shipped header compiles alone, in C and C++, from the installed tree
# only: a public header that needs an unshipped one fails here.
header-contract:
	rm -rf build/header-contract
	$(MAKE) install-headers DESTDIR=$(abspath build/header-contract) PREFIX=
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -pedantic \
		-Ibuild/header-contract/include -fsyntax-only tests/headers.c
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror -pedantic \
		-Ibuild/header-contract/include -fsyntax-only tests/headers.cpp
	@set -e; for header in build/header-contract/include/astra/*.h; do \
		name=$${header##*/}; \
		printf '#include <astra/%s>\ntypedef int astra_header_contract;\n' "$$name" | $(HOST_CC) -std=c11 \
			-Wall -Wextra -Werror -pedantic \
			-Ibuild/header-contract/include -fsyntax-only -x c - || \
			{ echo "header-contract: astra/$$name (C)" >&2; exit 1; }; \
		printf '#include <astra/%s>\ntypedef int astra_header_contract;\n' "$$name" | $(HOST_CXX) -std=c++17 \
			-Wall -Wextra -Werror -pedantic \
			-Ibuild/header-contract/include -fsyntax-only -x c++ - || \
			{ echo "header-contract: astra/$$name (C++)" >&2; exit 1; }; \
	done
	@echo "NDK header contract: PASS"

install-headers:
	install -d $(DESTDIR)$(PREFIX)/include/astra
	install -m 0644 include/astra/*.h $(DESTDIR)$(PREFIX)/include/astra/
	install -m 0644 $(KERNEL_ABI_HEADERS) $(DESTDIR)$(PREFIX)/include/astra/

clean:
	rm -rf build

.PHONY: all test check example docs docs-html docs-pdf \
	docs-check docs-clean loader-library-contract shared-library-contracts \
	developer-components dist dist-check \
	os-library-tests certify sdk header-contract install-headers clean
