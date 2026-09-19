# Canonical products for Astra's universal shared-library namespace.
#
# Consumers name these products; library-owner Makefiles remain responsible
# for producing and validating them.  Keeping the filenames here prevents a
# Kit, program, or release build from silently selecting an obsolete product
# left in a generated directory after an ABI or SONAME change.
ASTRA_USERSPACE_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
ASTRA_REPOSITORY_ROOT := $(abspath $(ASTRA_USERSPACE_ROOT)/../..)

ASTRA_SYSTEM_LIBRARY := \
	$(ASTRA_REPOSITORY_ROOT)/ndk/build/m68k/libraries/system.library.1
ASTRA_COMPILER_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/compiler/build/m68k/libraries/compiler.library.1
ASTRA_UNWIND_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/compiler/build/m68k/libraries/unwind.library.1
ASTRA_RUNTIME_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/runtime/build/m68k/libraries/runtime.library.1
ASTRA_STREAMS_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/streams/build/m68k/libraries/streams.library.1
ASTRA_FILESYSTEM_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/vfs/build/m68k/libraries/filesystem.library.2
ASTRA_LIBC_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/posix/build/m68k/libraries/libc.library.1
ASTRA_TERMINFO_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/terminfo/build/m68k/libraries/terminfo.library.6
ASTRA_CXX_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/cxx/build/m68k/libraries/cxx.library.1
ASTRA_LUA_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/lua/build/m68k/libraries/lua.library.5
ASTRA_NTP_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/ntp/build/m68k/libraries/ntp.library.1
ASTRA_GRAPHICS_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/graphics/build/m68k/libraries/graphics.library.2
ASTRA_FONT_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/graphics/build/m68k/libraries/font.library.2
ASTRA_INTERFACE_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/interface/build/m68k/libraries/interface.library.5
ASTRA_NETWORK_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/network/build/m68k/libraries/network.library.1
ASTRA_CONFIG_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/config/build/m68k/libraries/config.library.1
ASTRA_LOADER_LIBRARY := \
	$(ASTRA_USERSPACE_ROOT)/loader/build/m68k/loader.library.1

# The process interpreter is deliberately not a normal member of the closure:
# it is the closed bootstrap image that loads these libraries.
ASTRA_SHARED_LIBRARIES := \
	$(ASTRA_SYSTEM_LIBRARY) \
	$(ASTRA_COMPILER_LIBRARY) \
	$(ASTRA_UNWIND_LIBRARY) \
	$(ASTRA_RUNTIME_LIBRARY) \
	$(ASTRA_STREAMS_LIBRARY) \
	$(ASTRA_FILESYSTEM_LIBRARY) \
	$(ASTRA_LIBC_LIBRARY) \
	$(ASTRA_TERMINFO_LIBRARY) \
	$(ASTRA_CXX_LIBRARY) \
	$(ASTRA_LUA_LIBRARY) \
	$(ASTRA_NTP_LIBRARY) \
	$(ASTRA_GRAPHICS_LIBRARY) \
	$(ASTRA_FONT_LIBRARY) \
	$(ASTRA_INTERFACE_LIBRARY) \
	$(ASTRA_NETWORK_LIBRARY) \
	$(ASTRA_CONFIG_LIBRARY)

ASTRA_SHARED_LIBRARY_DIRS := $(sort $(dir $(ASTRA_SHARED_LIBRARIES)))
ASTRA_SHARED_LIBRARY_SEARCH_FLAGS := \
	$(foreach directory,$(ASTRA_SHARED_LIBRARY_DIRS),\
		-L$(directory) -Wl,-rpath-link,$(directory))

# Canonical dependency and owner metadata for the universal library namespace.
# Programs declare only direct library files; this graph identifies the
# outermost owners that must be validated before linking.  Adding a library is
# therefore a single registration here, not another private dependency walker
# in every native or POSIX application Makefile.
ASTRA_LIBRARY_KEYS := system compiler unwind runtime streams filesystem libc \
	terminfo cxx lua ntp graphics font interface \
	network config

ASTRA_LIBRARY_FILE_system := $(ASTRA_SYSTEM_LIBRARY)
ASTRA_LIBRARY_FILE_compiler := $(ASTRA_COMPILER_LIBRARY)
ASTRA_LIBRARY_FILE_unwind := $(ASTRA_UNWIND_LIBRARY)
ASTRA_LIBRARY_FILE_runtime := $(ASTRA_RUNTIME_LIBRARY)
ASTRA_LIBRARY_FILE_streams := $(ASTRA_STREAMS_LIBRARY)
ASTRA_LIBRARY_FILE_filesystem := $(ASTRA_FILESYSTEM_LIBRARY)
ASTRA_LIBRARY_FILE_libc := $(ASTRA_LIBC_LIBRARY)
ASTRA_LIBRARY_FILE_terminfo := $(ASTRA_TERMINFO_LIBRARY)
ASTRA_LIBRARY_FILE_cxx := $(ASTRA_CXX_LIBRARY)
ASTRA_LIBRARY_FILE_lua := $(ASTRA_LUA_LIBRARY)
ASTRA_LIBRARY_FILE_ntp := $(ASTRA_NTP_LIBRARY)
ASTRA_LIBRARY_FILE_graphics := $(ASTRA_GRAPHICS_LIBRARY)
ASTRA_LIBRARY_FILE_font := $(ASTRA_FONT_LIBRARY)
ASTRA_LIBRARY_FILE_interface := $(ASTRA_INTERFACE_LIBRARY)
ASTRA_LIBRARY_FILE_network := $(ASTRA_NETWORK_LIBRARY)
ASTRA_LIBRARY_FILE_config := $(ASTRA_CONFIG_LIBRARY)

ASTRA_LIBRARY_DEPS_system := runtime
ASTRA_LIBRARY_DEPS_compiler :=
ASTRA_LIBRARY_DEPS_unwind := libc runtime compiler
ASTRA_LIBRARY_DEPS_runtime := compiler
ASTRA_LIBRARY_DEPS_streams := runtime
ASTRA_LIBRARY_DEPS_filesystem := system runtime
ASTRA_LIBRARY_DEPS_libc := streams filesystem network runtime compiler
ASTRA_LIBRARY_DEPS_terminfo := libc runtime compiler
ASTRA_LIBRARY_DEPS_cxx := libc runtime compiler unwind
ASTRA_LIBRARY_DEPS_lua := libc runtime compiler
ASTRA_LIBRARY_DEPS_ntp := libc runtime compiler
ASTRA_LIBRARY_DEPS_graphics := system runtime compiler
ASTRA_LIBRARY_DEPS_font := graphics runtime compiler
ASTRA_LIBRARY_DEPS_interface := graphics font system runtime compiler
ASTRA_LIBRARY_DEPS_network := runtime compiler
ASTRA_LIBRARY_DEPS_config := system runtime compiler filesystem

ASTRA_LIBRARY_OWNER_system := $(ASTRA_REPOSITORY_ROOT)/ndk:library
ASTRA_LIBRARY_OWNER_compiler := $(ASTRA_USERSPACE_ROOT)/compiler:library
ASTRA_LIBRARY_OWNER_unwind := $(ASTRA_USERSPACE_ROOT)/compiler:unwind-library
ASTRA_LIBRARY_OWNER_runtime := $(ASTRA_USERSPACE_ROOT)/runtime:library
ASTRA_LIBRARY_OWNER_streams := $(ASTRA_USERSPACE_ROOT)/streams:library
ASTRA_LIBRARY_OWNER_filesystem := $(ASTRA_USERSPACE_ROOT)/vfs:library
ASTRA_LIBRARY_OWNER_libc := $(ASTRA_USERSPACE_ROOT)/posix:library
ASTRA_LIBRARY_OWNER_terminfo := $(ASTRA_USERSPACE_ROOT)/terminfo:library
ASTRA_LIBRARY_OWNER_cxx := $(ASTRA_USERSPACE_ROOT)/cxx:library
ASTRA_LIBRARY_OWNER_lua := $(ASTRA_USERSPACE_ROOT)/lua:library
ASTRA_LIBRARY_OWNER_ntp := $(ASTRA_USERSPACE_ROOT)/ntp:library
ASTRA_LIBRARY_OWNER_graphics := $(ASTRA_USERSPACE_ROOT)/graphics:graphics-library
ASTRA_LIBRARY_OWNER_font := $(ASTRA_USERSPACE_ROOT)/graphics:font-library
ASTRA_LIBRARY_OWNER_interface := $(ASTRA_USERSPACE_ROOT)/interface:interface-library
ASTRA_LIBRARY_OWNER_network := $(ASTRA_USERSPACE_ROOT)/network:library
ASTRA_LIBRARY_OWNER_config := $(ASTRA_USERSPACE_ROOT)/config:library

# Release and NDK gates use the owners' full ABI contracts.  Application
# builds intentionally use the freshness targets above so launching one
# program never certifies unrelated libraries.
ASTRA_LIBRARY_CONTRACT_OWNER_system := $(ASTRA_REPOSITORY_ROOT)/ndk:system-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_compiler := $(ASTRA_USERSPACE_ROOT)/compiler:compiler-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_unwind := $(ASTRA_USERSPACE_ROOT)/compiler:unwind-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_runtime := $(ASTRA_USERSPACE_ROOT)/runtime:runtime-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_streams := $(ASTRA_USERSPACE_ROOT)/streams:streams-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_filesystem := $(ASTRA_USERSPACE_ROOT)/vfs:library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_libc := $(ASTRA_USERSPACE_ROOT)/posix:libc-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_terminfo := $(ASTRA_USERSPACE_ROOT)/terminfo:library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_cxx := $(ASTRA_USERSPACE_ROOT)/cxx:cxx-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_lua := $(ASTRA_USERSPACE_ROOT)/lua:lua-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_ntp := $(ASTRA_USERSPACE_ROOT)/ntp:ntp-library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_graphics := $(ASTRA_USERSPACE_ROOT)/graphics:library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_font := $(ASTRA_USERSPACE_ROOT)/graphics:library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_interface := $(ASTRA_USERSPACE_ROOT)/interface:library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_network := $(ASTRA_USERSPACE_ROOT)/network:library-contract
ASTRA_LIBRARY_CONTRACT_OWNER_config := $(ASTRA_USERSPACE_ROOT)/config:library-contract

astra_library_key = $(strip $(foreach key,$(ASTRA_LIBRARY_KEYS),\
	$(if $(filter $(ASTRA_LIBRARY_FILE_$(key)),$(1)),$(key))))
astra_library_keys = $(strip $(foreach library,$(1),\
	$(call astra_library_key,$(library))))
astra_library_transitive_deps = $(sort $(foreach dependency,\
	$(ASTRA_LIBRARY_DEPS_$(1)),$(dependency) \
	$(call astra_library_transitive_deps,$(dependency))))
astra_library_root_keys = $(filter-out $(sort $(foreach key,$(1),\
	$(call astra_library_transitive_deps,$(key)))),$(1))
astra_library_owner_targets = $(sort $(foreach key,\
	$(call astra_library_root_keys,$(1)),$(ASTRA_LIBRARY_OWNER_$(key))))
astra_library_contract_owner_targets = $(sort $(foreach key,$(1),\
	$(ASTRA_LIBRARY_CONTRACT_OWNER_$(key))))
