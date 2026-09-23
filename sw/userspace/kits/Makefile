include ../libraries.mk
COMPILER_LIBRARY := $(ASTRA_COMPILER_LIBRARY)
UNWIND_LIBRARY := $(ASTRA_UNWIND_LIBRARY)
RUNTIME_LIBRARY := $(ASTRA_RUNTIME_LIBRARY)
SYSTEM_LIBRARY := $(ASTRA_SYSTEM_LIBRARY)
LOADER_LIBRARY := $(ASTRA_LOADER_LIBRARY)
STREAMS_LIBRARY := $(ASTRA_STREAMS_LIBRARY)
LIBC_LIBRARY := $(ASTRA_LIBC_LIBRARY)
TERMINFO_LIBRARY := $(ASTRA_TERMINFO_LIBRARY)
CXX_LIBRARY := $(ASTRA_CXX_LIBRARY)
LUA_LIBRARY := $(ASTRA_LUA_LIBRARY)
NTP_LIBRARY := $(ASTRA_NTP_LIBRARY)
GRAPHICS_LIBRARY := $(ASTRA_GRAPHICS_LIBRARY)
FONT_LIBRARY := $(ASTRA_FONT_LIBRARY)
FILESYSTEM_LIBRARY := $(ASTRA_FILESYSTEM_LIBRARY)
INTERFACE_LIBRARY := $(ASTRA_INTERFACE_LIBRARY)
NETWORK_LIBRARY := $(ASTRA_NETWORK_LIBRARY)
CONFIG_LIBRARY := $(ASTRA_CONFIG_LIBRARY)
BUNDLE_TOOL := ../../../tools/build/astra-bundle
BUNDLE_DEPENDENCY_TOOL := ../../../tools/check_bundle_dependencies.py
RUNTIME := build/Runtime.kit
STREAMS := build/Streams.kit
POSIX := build/Posix.kit
GRAPHICS := build/Graphics.kit
FILESYSTEM := build/Filesystem.kit
INTERFACE := build/Interface.kit
NETWORK := build/Network.kit
CONFIGURATION := build/Configuration.kit
CXX := build/Cxx.kit
LUA := build/Lua.kit
RUNTIME_STAMP := build/.Runtime.kit.stamp
STREAMS_STAMP := build/.Streams.kit.stamp
POSIX_STAMP := build/.Posix.kit.stamp
CXX_STAMP := build/.Cxx.kit.stamp
LUA_STAMP := build/.Lua.kit.stamp
COMPILER_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides compiler\.library //p' Runtime.kit/manifest)
COMPILER_LIBRARY_ABI := $(word 1,$(COMPILER_LIBRARY_PROVIDES))
COMPILER_LIBRARY_VERSION := $(word 2,$(COMPILER_LIBRARY_PROVIDES))
RUNTIME_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides runtime\.library //p' Runtime.kit/manifest)
RUNTIME_LIBRARY_ABI := $(word 1,$(RUNTIME_LIBRARY_PROVIDES))
RUNTIME_LIBRARY_VERSION := $(word 2,$(RUNTIME_LIBRARY_PROVIDES))
SYSTEM_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides system\.library //p' Runtime.kit/manifest)
SYSTEM_LIBRARY_ABI := $(word 1,$(SYSTEM_LIBRARY_PROVIDES))
SYSTEM_LIBRARY_VERSION := $(word 2,$(SYSTEM_LIBRARY_PROVIDES))
LOADER_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides loader\.library //p' Runtime.kit/manifest)
LOADER_LIBRARY_ABI := $(word 1,$(LOADER_LIBRARY_PROVIDES))
LOADER_LIBRARY_VERSION := $(word 2,$(LOADER_LIBRARY_PROVIDES))
STREAMS_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides streams\.library //p' Streams.kit/manifest)
STREAMS_LIBRARY_ABI := $(word 1,$(STREAMS_LIBRARY_PROVIDES))
STREAMS_LIBRARY_VERSION := $(word 2,$(STREAMS_LIBRARY_PROVIDES))
LIBC_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides libc\.library //p' Posix.kit/manifest)
LIBC_LIBRARY_ABI := $(word 1,$(LIBC_LIBRARY_PROVIDES))
LIBC_LIBRARY_VERSION := $(word 2,$(LIBC_LIBRARY_PROVIDES))
TERMINFO_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides terminfo\.library //p' Posix.kit/manifest)
TERMINFO_LIBRARY_ABI := $(word 1,$(TERMINFO_LIBRARY_PROVIDES))
TERMINFO_LIBRARY_VERSION := $(word 2,$(TERMINFO_LIBRARY_PROVIDES))
UNWIND_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides unwind\.library //p' Cxx.kit/manifest)
UNWIND_LIBRARY_ABI := $(word 1,$(UNWIND_LIBRARY_PROVIDES))
UNWIND_LIBRARY_VERSION := $(word 2,$(UNWIND_LIBRARY_PROVIDES))
CXX_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides cxx\.library //p' Cxx.kit/manifest)
CXX_LIBRARY_ABI := $(word 1,$(CXX_LIBRARY_PROVIDES))
CXX_LIBRARY_VERSION := $(word 2,$(CXX_LIBRARY_PROVIDES))
LUA_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides lua\.library //p' Lua.kit/manifest)
LUA_LIBRARY_ABI := $(word 1,$(LUA_LIBRARY_PROVIDES))
LUA_LIBRARY_VERSION := $(word 2,$(LUA_LIBRARY_PROVIDES))
GRAPHICS_STAMP := build/.Graphics.kit.stamp
GRAPHICS_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides graphics\.library //p' Graphics.kit/manifest)
GRAPHICS_LIBRARY_ABI := $(word 1,$(GRAPHICS_LIBRARY_PROVIDES))
GRAPHICS_LIBRARY_VERSION := $(word 2,$(GRAPHICS_LIBRARY_PROVIDES))
FONT_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides font\.library //p' Graphics.kit/manifest)
FONT_LIBRARY_ABI := $(word 1,$(FONT_LIBRARY_PROVIDES))
FONT_LIBRARY_VERSION := $(word 2,$(FONT_LIBRARY_PROVIDES))
FILESYSTEM_STAMP := build/.Filesystem.kit.stamp
FILESYSTEM_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides filesystem\.library //p' Filesystem.kit/manifest)
FILESYSTEM_LIBRARY_ABI := $(word 1,$(FILESYSTEM_LIBRARY_PROVIDES))
FILESYSTEM_LIBRARY_VERSION := $(word 2,$(FILESYSTEM_LIBRARY_PROVIDES))
INTERFACE_STAMP := build/.Interface.kit.stamp
INTERFACE_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides interface\.library //p' Interface.kit/manifest)
INTERFACE_LIBRARY_ABI := $(word 1,$(INTERFACE_LIBRARY_PROVIDES))
INTERFACE_LIBRARY_VERSION := $(word 2,$(INTERFACE_LIBRARY_PROVIDES))
NETWORK_STAMP := build/.Network.kit.stamp
CONFIGURATION_STAMP := build/.Configuration.kit.stamp
NETWORK_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides network\.library //p' Network.kit/manifest)
NETWORK_LIBRARY_ABI := $(word 1,$(NETWORK_LIBRARY_PROVIDES))
NETWORK_LIBRARY_VERSION := $(word 2,$(NETWORK_LIBRARY_PROVIDES))
NTP_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides ntp\.library //p' Network.kit/manifest)
NTP_LIBRARY_ABI := $(word 1,$(NTP_LIBRARY_PROVIDES))
NTP_LIBRARY_VERSION := $(word 2,$(NTP_LIBRARY_PROVIDES))
CONFIG_LIBRARY_PROVIDES := $(shell sed -n \
	's/^provides config\.library //p' Configuration.kit/manifest)
CONFIG_LIBRARY_ABI := $(word 1,$(CONFIG_LIBRARY_PROVIDES))
CONFIG_LIBRARY_VERSION := $(word 2,$(CONFIG_LIBRARY_PROVIDES))
LIBRARIES := $(ASTRA_SHARED_LIBRARIES) $(LOADER_LIBRARY)
PROVIDER_MANIFESTS := $(wildcard *.kit/manifest)
DEPENDENCY_MANIFESTS := $(PROVIDER_MANIFESTS) $(wildcard ../apps/*.app/manifest)
KIT_NAMES := $(sort $(patsubst %/manifest,%,$(PROVIDER_MANIFESTS)))
KIT_INVENTORY := build/.kits

all: $(RUNTIME_STAMP) $(STREAMS_STAMP) $(POSIX_STAMP) $(GRAPHICS_STAMP) \
	$(FILESYSTEM_STAMP) $(INTERFACE_STAMP) \
	$(NETWORK_STAMP) $(CONFIGURATION_STAMP) $(CXX_STAMP) $(LUA_STAMP) \
	$(KIT_INVENTORY) provider-coverage

$(COMPILER_LIBRARY): FORCE
	$(MAKE) -C ../compiler compiler-library-contract

$(UNWIND_LIBRARY): FORCE
	$(MAKE) -C ../compiler unwind-library-contract

$(RUNTIME_LIBRARY): FORCE
	$(MAKE) -C ../runtime runtime-library-contract

$(SYSTEM_LIBRARY): FORCE
	$(MAKE) -C ../../../ndk system-library-contract

$(LOADER_LIBRARY): FORCE
	$(MAKE) -C ../loader all

$(STREAMS_LIBRARY): FORCE
	$(MAKE) -C ../streams streams-library-contract

$(LIBC_LIBRARY): FORCE
	$(MAKE) -C ../posix libc-library-contract

$(TERMINFO_LIBRARY): FORCE
	$(MAKE) -C ../terminfo library-contract

$(CXX_LIBRARY): FORCE
	$(MAKE) -C ../cxx cxx-library-contract

$(LUA_LIBRARY): FORCE
	$(MAKE) -C ../lua lua-library-contract

$(NTP_LIBRARY): FORCE
	$(MAKE) -C ../ntp ntp-library-contract

$(GRAPHICS_LIBRARY) $(FONT_LIBRARY) &: FORCE
	$(MAKE) -C ../graphics library-contract

$(FILESYSTEM_LIBRARY): FORCE
	$(MAKE) -C ../vfs library-contract

$(INTERFACE_LIBRARY): FORCE
	$(MAKE) -C ../interface library-contract

$(NETWORK_LIBRARY): FORCE
	$(MAKE) -C ../network library-contract

$(CONFIG_LIBRARY): FORCE
	$(MAKE) -C ../config library-contract

$(BUNDLE_TOOL): FORCE
	$(MAKE) -C ../../../tools all
	@test -s $@

$(RUNTIME_STAMP): Runtime.kit/manifest $(COMPILER_LIBRARY) $(RUNTIME_LIBRARY) \
		$(SYSTEM_LIBRARY) $(LOADER_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(RUNTIME)
	@mkdir -p $(RUNTIME)/libraries/compiler.library/abi-$(COMPILER_LIBRARY_ABI)/$(COMPILER_LIBRARY_VERSION)/m68k-68040
	@mkdir -p $(RUNTIME)/libraries/runtime.library/abi-$(RUNTIME_LIBRARY_ABI)/$(RUNTIME_LIBRARY_VERSION)/m68k-68040
	@mkdir -p $(RUNTIME)/libraries/system.library/abi-$(SYSTEM_LIBRARY_ABI)/$(SYSTEM_LIBRARY_VERSION)/m68k-68040
	@mkdir -p $(RUNTIME)/libraries/loader.library/abi-$(LOADER_LIBRARY_ABI)/$(LOADER_LIBRARY_VERSION)/m68k-68040
	cp Runtime.kit/manifest $(RUNTIME)/manifest
	cp $(COMPILER_LIBRARY) \
		$(RUNTIME)/libraries/compiler.library/abi-$(COMPILER_LIBRARY_ABI)/$(COMPILER_LIBRARY_VERSION)/m68k-68040/compiler.library
	cp $(RUNTIME_LIBRARY) \
		$(RUNTIME)/libraries/runtime.library/abi-$(RUNTIME_LIBRARY_ABI)/$(RUNTIME_LIBRARY_VERSION)/m68k-68040/runtime.library
	cp $(SYSTEM_LIBRARY) \
		$(RUNTIME)/libraries/system.library/abi-$(SYSTEM_LIBRARY_ABI)/$(SYSTEM_LIBRARY_VERSION)/m68k-68040/system.library
	cp $(LOADER_LIBRARY) \
		$(RUNTIME)/libraries/loader.library/abi-$(LOADER_LIBRARY_ABI)/$(LOADER_LIBRARY_VERSION)/m68k-68040/loader.library
	$(BUNDLE_TOOL) check $(RUNTIME)
	@touch $@

$(STREAMS_STAMP): Streams.kit/manifest $(STREAMS_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(STREAMS)
	@mkdir -p $(STREAMS)/libraries/streams.library/abi-$(STREAMS_LIBRARY_ABI)/$(STREAMS_LIBRARY_VERSION)/m68k-68040
	cp Streams.kit/manifest $(STREAMS)/manifest
	cp $(STREAMS_LIBRARY) \
		$(STREAMS)/libraries/streams.library/abi-$(STREAMS_LIBRARY_ABI)/$(STREAMS_LIBRARY_VERSION)/m68k-68040/streams.library
	$(BUNDLE_TOOL) check $(STREAMS)
	@touch $@

$(POSIX_STAMP): Posix.kit/manifest $(LIBC_LIBRARY) $(TERMINFO_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(POSIX)
	@mkdir -p $(POSIX)/libraries/libc.library/abi-$(LIBC_LIBRARY_ABI)/$(LIBC_LIBRARY_VERSION)/m68k-68040
	@mkdir -p $(POSIX)/libraries/terminfo.library/abi-$(TERMINFO_LIBRARY_ABI)/$(TERMINFO_LIBRARY_VERSION)/m68k-68040
	cp Posix.kit/manifest $(POSIX)/manifest
	cp $(LIBC_LIBRARY) \
		$(POSIX)/libraries/libc.library/abi-$(LIBC_LIBRARY_ABI)/$(LIBC_LIBRARY_VERSION)/m68k-68040/libc.library
	cp $(TERMINFO_LIBRARY) \
		$(POSIX)/libraries/terminfo.library/abi-$(TERMINFO_LIBRARY_ABI)/$(TERMINFO_LIBRARY_VERSION)/m68k-68040/terminfo.library
	$(BUNDLE_TOOL) check $(POSIX)
	@touch $@

$(CXX_STAMP): Cxx.kit/manifest $(UNWIND_LIBRARY) $(CXX_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(CXX)
	@mkdir -p $(CXX)/libraries/unwind.library/abi-$(UNWIND_LIBRARY_ABI)/$(UNWIND_LIBRARY_VERSION)/m68k-68040
	@mkdir -p $(CXX)/libraries/cxx.library/abi-$(CXX_LIBRARY_ABI)/$(CXX_LIBRARY_VERSION)/m68k-68040
	cp Cxx.kit/manifest $(CXX)/manifest
	cp $(UNWIND_LIBRARY) \
		$(CXX)/libraries/unwind.library/abi-$(UNWIND_LIBRARY_ABI)/$(UNWIND_LIBRARY_VERSION)/m68k-68040/unwind.library
	cp $(CXX_LIBRARY) \
		$(CXX)/libraries/cxx.library/abi-$(CXX_LIBRARY_ABI)/$(CXX_LIBRARY_VERSION)/m68k-68040/cxx.library
	$(BUNDLE_TOOL) check $(CXX)
	@touch $@

$(LUA_STAMP): Lua.kit/manifest $(LUA_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(LUA)
	@mkdir -p $(LUA)/libraries/lua.library/abi-$(LUA_LIBRARY_ABI)/$(LUA_LIBRARY_VERSION)/m68k-68040
	cp Lua.kit/manifest $(LUA)/manifest
	cp $(LUA_LIBRARY) \
		$(LUA)/libraries/lua.library/abi-$(LUA_LIBRARY_ABI)/$(LUA_LIBRARY_VERSION)/m68k-68040/lua.library
	$(BUNDLE_TOOL) check $(LUA)
	@touch $@

$(GRAPHICS_STAMP): Graphics.kit/manifest $(GRAPHICS_LIBRARY) \
		$(FONT_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(GRAPHICS)
	@mkdir -p $(GRAPHICS)/libraries/graphics.library/abi-$(GRAPHICS_LIBRARY_ABI)/$(GRAPHICS_LIBRARY_VERSION)/m68k-68040
	@mkdir -p $(GRAPHICS)/libraries/font.library/abi-$(FONT_LIBRARY_ABI)/$(FONT_LIBRARY_VERSION)/m68k-68040
	cp Graphics.kit/manifest $(GRAPHICS)/manifest
	cp $(GRAPHICS_LIBRARY) \
		$(GRAPHICS)/libraries/graphics.library/abi-$(GRAPHICS_LIBRARY_ABI)/$(GRAPHICS_LIBRARY_VERSION)/m68k-68040/graphics.library
	cp $(FONT_LIBRARY) \
		$(GRAPHICS)/libraries/font.library/abi-$(FONT_LIBRARY_ABI)/$(FONT_LIBRARY_VERSION)/m68k-68040/font.library
	$(BUNDLE_TOOL) check $(GRAPHICS)
	@touch $@

$(FILESYSTEM_STAMP): Filesystem.kit/manifest $(FILESYSTEM_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(FILESYSTEM)
	@mkdir -p $(FILESYSTEM)/libraries/filesystem.library/abi-$(FILESYSTEM_LIBRARY_ABI)/$(FILESYSTEM_LIBRARY_VERSION)/m68k-68040
	cp Filesystem.kit/manifest $(FILESYSTEM)/manifest
	cp $(FILESYSTEM_LIBRARY) \
		$(FILESYSTEM)/libraries/filesystem.library/abi-$(FILESYSTEM_LIBRARY_ABI)/$(FILESYSTEM_LIBRARY_VERSION)/m68k-68040/filesystem.library
	$(BUNDLE_TOOL) check $(FILESYSTEM)
	@touch $@

$(INTERFACE_STAMP): Interface.kit/manifest $(INTERFACE_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(INTERFACE)
	@mkdir -p $(INTERFACE)/libraries/interface.library/abi-$(INTERFACE_LIBRARY_ABI)/$(INTERFACE_LIBRARY_VERSION)/m68k-68040
	cp Interface.kit/manifest $(INTERFACE)/manifest
	cp $(INTERFACE_LIBRARY) \
		$(INTERFACE)/libraries/interface.library/abi-$(INTERFACE_LIBRARY_ABI)/$(INTERFACE_LIBRARY_VERSION)/m68k-68040/interface.library
	$(BUNDLE_TOOL) check $(INTERFACE)
	@touch $@

$(NETWORK_STAMP): Network.kit/manifest $(NETWORK_LIBRARY) $(NTP_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(NETWORK)
	@mkdir -p $(NETWORK)/libraries/network.library/abi-$(NETWORK_LIBRARY_ABI)/$(NETWORK_LIBRARY_VERSION)/m68k-68040
	@mkdir -p $(NETWORK)/libraries/ntp.library/abi-$(NTP_LIBRARY_ABI)/$(NTP_LIBRARY_VERSION)/m68k-68040
	cp Network.kit/manifest $(NETWORK)/manifest
	cp $(NETWORK_LIBRARY) \
		$(NETWORK)/libraries/network.library/abi-$(NETWORK_LIBRARY_ABI)/$(NETWORK_LIBRARY_VERSION)/m68k-68040/network.library
	cp $(NTP_LIBRARY) \
		$(NETWORK)/libraries/ntp.library/abi-$(NTP_LIBRARY_ABI)/$(NTP_LIBRARY_VERSION)/m68k-68040/ntp.library
	$(BUNDLE_TOOL) check $(NETWORK)
	@touch $@

$(CONFIGURATION_STAMP): Configuration.kit/manifest $(CONFIG_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(CONFIGURATION)
	@mkdir -p $(CONFIGURATION)/libraries/config.library/abi-$(CONFIG_LIBRARY_ABI)/$(CONFIG_LIBRARY_VERSION)/m68k-68040
	cp Configuration.kit/manifest $(CONFIGURATION)/manifest
	cp $(CONFIG_LIBRARY) \
		$(CONFIGURATION)/libraries/config.library/abi-$(CONFIG_LIBRARY_ABI)/$(CONFIG_LIBRARY_VERSION)/m68k-68040/config.library
	$(BUNDLE_TOOL) check $(CONFIGURATION)
	@touch $@

provider-coverage: $(LIBRARIES) $(DEPENDENCY_MANIFESTS) \
		$(BUNDLE_DEPENDENCY_TOOL)
	python3 $(BUNDLE_DEPENDENCY_TOOL) \
		$(foreach library,$(LIBRARIES),--library $(library)) \
		$(foreach manifest,$(DEPENDENCY_MANIFESTS),--manifest $(manifest))

$(KIT_INVENTORY): FORCE $(PROVIDER_MANIFESTS)
	@mkdir -p $(@D)
	@rm -f $@.tmp
	@for kit in $(KIT_NAMES); do echo $$kit >>$@.tmp; done
	@cmp -s $@.tmp $@ 2>/dev/null || mv $@.tmp $@
	@rm -f $@.tmp

test: all
	@touch $(FILESYSTEM)/stale-artifact
	@rm -f $(FILESYSTEM_STAMP)
	@$(MAKE) $(FILESYSTEM_STAMP)
	@test ! -e $(FILESYSTEM)/stale-artifact

sanitize analyze:
	@true

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean provider-coverage FORCE

FORCE:
