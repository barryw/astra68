GRAPHICS_LIBRARY := ../graphics/build/m68k/libraries/graphics.library
FONT_LIBRARY := ../graphics/build/m68k/libraries/font.library
FILESYSTEM_LIBRARY := ../vfs/build/m68k/libraries/filesystem.library
INTERFACE_LIBRARY := ../interface/build/m68k/libraries/interface.library
INPUT_LIBRARY := ../interface/build/m68k/libraries/input.library
EVENTS_LIBRARY := ../events/build/m68k/libraries/events.library
MESSAGING_LIBRARY := ../messaging/build/m68k/libraries/messaging.library
NETWORK_LIBRARY := ../network/build/m68k/libraries/network.library
CONFIG_LIBRARY := ../config/build/m68k/libraries/config.library
BUNDLE_TOOL := ../../../tools/build/astra-bundle
GRAPHICS := build/Graphics.kit
FILESYSTEM := build/Filesystem.kit
INTERFACE := build/Interface.kit
EVENTS := build/Events.kit
MESSAGING := build/Messaging.kit
NETWORK := build/Network.kit
CONFIGURATION := build/Configuration.kit
GRAPHICS_STAMP := build/.Graphics.kit.stamp
FILESYSTEM_STAMP := build/.Filesystem.kit.stamp
INTERFACE_STAMP := build/.Interface.kit.stamp
EVENTS_STAMP := build/.Events.kit.stamp
MESSAGING_STAMP := build/.Messaging.kit.stamp
NETWORK_STAMP := build/.Network.kit.stamp
CONFIGURATION_STAMP := build/.Configuration.kit.stamp
LIBRARIES := $(GRAPHICS_LIBRARY) $(FONT_LIBRARY) $(FILESYSTEM_LIBRARY) \
	$(INTERFACE_LIBRARY) $(INPUT_LIBRARY) $(EVENTS_LIBRARY) \
	$(MESSAGING_LIBRARY) $(NETWORK_LIBRARY) $(CONFIG_LIBRARY)
OWNER_DIRS := ../graphics ../vfs ../interface ../events ../messaging \
	../network ../config

all: $(GRAPHICS_STAMP) $(FILESYSTEM_STAMP) $(INTERFACE_STAMP) \
	$(EVENTS_STAMP) $(MESSAGING_STAMP) $(NETWORK_STAMP) \
	$(CONFIGURATION_STAMP)

owners:
	@for directory in $(OWNER_DIRS); do \
		$(MAKE) -C $$directory all || exit $$?; \
	done
	$(MAKE) -C ../../../tools all

$(LIBRARIES) $(BUNDLE_TOOL): | owners
	@test -s $@

$(GRAPHICS_STAMP): Graphics.kit/manifest $(GRAPHICS_LIBRARY) \
		$(FONT_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(GRAPHICS)
	@mkdir -p $(GRAPHICS)/libraries/graphics.library/abi-1/1.3.0/m68k-68040
	@mkdir -p $(GRAPHICS)/libraries/font.library/abi-1/1.0.0/m68k-68040
	cp Graphics.kit/manifest $(GRAPHICS)/manifest
	cp $(GRAPHICS_LIBRARY) \
		$(GRAPHICS)/libraries/graphics.library/abi-1/1.3.0/m68k-68040/graphics.library
	cp $(FONT_LIBRARY) \
		$(GRAPHICS)/libraries/font.library/abi-1/1.0.0/m68k-68040/font.library
	$(BUNDLE_TOOL) check $(GRAPHICS)
	@touch $@

$(FILESYSTEM_STAMP): Filesystem.kit/manifest $(FILESYSTEM_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(FILESYSTEM)
	@mkdir -p $(FILESYSTEM)/libraries/filesystem.library/abi-2/2.0.0/m68k-68040
	cp Filesystem.kit/manifest $(FILESYSTEM)/manifest
	cp $(FILESYSTEM_LIBRARY) \
		$(FILESYSTEM)/libraries/filesystem.library/abi-2/2.0.0/m68k-68040/filesystem.library
	$(BUNDLE_TOOL) check $(FILESYSTEM)
	@touch $@

$(INTERFACE_STAMP): Interface.kit/manifest $(INTERFACE_LIBRARY) \
		$(INPUT_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(INTERFACE)
	@mkdir -p $(INTERFACE)/libraries/interface.library/abi-1/1.1.0/m68k-68040
	@mkdir -p $(INTERFACE)/libraries/input.library/abi-1/1.0.0/m68k-68040
	cp Interface.kit/manifest $(INTERFACE)/manifest
	cp $(INTERFACE_LIBRARY) \
		$(INTERFACE)/libraries/interface.library/abi-1/1.1.0/m68k-68040/interface.library
	cp $(INPUT_LIBRARY) \
		$(INTERFACE)/libraries/input.library/abi-1/1.0.0/m68k-68040/input.library
	$(BUNDLE_TOOL) check $(INTERFACE)
	@touch $@

$(EVENTS_STAMP): Events.kit/manifest $(EVENTS_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(EVENTS)
	@mkdir -p $(EVENTS)/libraries/events.library/abi-1/1.0.0/m68k-68040
	cp Events.kit/manifest $(EVENTS)/manifest
	cp $(EVENTS_LIBRARY) \
		$(EVENTS)/libraries/events.library/abi-1/1.0.0/m68k-68040/events.library
	$(BUNDLE_TOOL) check $(EVENTS)
	@touch $@

$(MESSAGING_STAMP): Messaging.kit/manifest $(MESSAGING_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(MESSAGING)
	@mkdir -p $(MESSAGING)/libraries/messaging.library/abi-1/1.0.0/m68k-68040
	cp Messaging.kit/manifest $(MESSAGING)/manifest
	cp $(MESSAGING_LIBRARY) \
		$(MESSAGING)/libraries/messaging.library/abi-1/1.0.0/m68k-68040/messaging.library
	$(BUNDLE_TOOL) check $(MESSAGING)
	@touch $@

$(NETWORK_STAMP): Network.kit/manifest $(NETWORK_LIBRARY) $(BUNDLE_TOOL)
	rm -rf $(NETWORK)
	@mkdir -p $(NETWORK)/libraries/network.library/abi-1/1.0.0/m68k-68040
	cp Network.kit/manifest $(NETWORK)/manifest
	cp $(NETWORK_LIBRARY) \
		$(NETWORK)/libraries/network.library/abi-1/1.0.0/m68k-68040/network.library
	$(BUNDLE_TOOL) check $(NETWORK)
	@touch $@

$(CONFIGURATION_STAMP): Configuration.kit/manifest $(CONFIG_LIBRARY) \
		$(BUNDLE_TOOL)
	rm -rf $(CONFIGURATION)
	@mkdir -p $(CONFIGURATION)/libraries/config.library/abi-1/1.0.0/m68k-68040
	cp Configuration.kit/manifest $(CONFIGURATION)/manifest
	cp $(CONFIG_LIBRARY) \
		$(CONFIGURATION)/libraries/config.library/abi-1/1.0.0/m68k-68040/config.library
	$(BUNDLE_TOOL) check $(CONFIGURATION)
	@touch $@

test: all
	@touch $(FILESYSTEM)/stale-artifact
	@rm -f $(FILESYSTEM_STAMP)
	@$(MAKE) $(FILESYSTEM_STAMP)
	@test ! -e $(FILESYSTEM)/stale-artifact

sanitize analyze:
	@true

clean:
	rm -rf build

.PHONY: all owners test sanitize analyze clean
