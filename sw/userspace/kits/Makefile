GRAPHICS_LIBRARY := ../graphics/build/m68k/libraries/graphics.library
FONT_LIBRARY := ../graphics/build/m68k/libraries/font.library
FILESYSTEM_LIBRARY := ../vfs/build/m68k/libraries/filesystem.library
INTERFACE_LIBRARY := ../interface/build/m68k/libraries/interface.library
INPUT_LIBRARY := ../interface/build/m68k/libraries/input.library
EVENTS_LIBRARY := ../events/build/m68k/libraries/events.library
MESSAGING_LIBRARY := ../messaging/build/m68k/libraries/messaging.library
BUNDLE_TOOL := ../../../tools/build/astra-bundle
GRAPHICS := build/Graphics.kit
FILESYSTEM := build/Filesystem.kit
INTERFACE := build/Interface.kit
EVENTS := build/Events.kit
MESSAGING := build/Messaging.kit
GRAPHICS_STAMP := build/.Graphics.kit.stamp
FILESYSTEM_STAMP := build/.Filesystem.kit.stamp
INTERFACE_STAMP := build/.Interface.kit.stamp
EVENTS_STAMP := build/.Events.kit.stamp
MESSAGING_STAMP := build/.Messaging.kit.stamp

all: $(GRAPHICS_STAMP) $(FILESYSTEM_STAMP) $(INTERFACE_STAMP) \
	$(EVENTS_STAMP) $(MESSAGING_STAMP)

$(GRAPHICS_LIBRARY) $(FONT_LIBRARY):
	$(MAKE) -C ../graphics all

$(FILESYSTEM_LIBRARY):
	$(MAKE) -C ../vfs all

$(INTERFACE_LIBRARY) $(INPUT_LIBRARY):
	$(MAKE) -C ../interface all

$(EVENTS_LIBRARY):
	$(MAKE) -C ../events all

$(MESSAGING_LIBRARY):
	$(MAKE) -C ../messaging all

$(BUNDLE_TOOL):
	$(MAKE) -C ../../../tools all

$(GRAPHICS_STAMP): Graphics.kit/manifest $(GRAPHICS_LIBRARY) \
		$(FONT_LIBRARY) $(BUNDLE_TOOL)
	@mkdir -p $(GRAPHICS)/libraries/graphics.library/abi-1/1.1.0/m68k-68030
	@mkdir -p $(GRAPHICS)/libraries/font.library/abi-1/1.0.0/m68k-68030
	cp Graphics.kit/manifest $(GRAPHICS)/manifest
	cp $(GRAPHICS_LIBRARY) \
		$(GRAPHICS)/libraries/graphics.library/abi-1/1.1.0/m68k-68030/graphics.library
	cp $(FONT_LIBRARY) \
		$(GRAPHICS)/libraries/font.library/abi-1/1.0.0/m68k-68030/font.library
	$(BUNDLE_TOOL) check $(GRAPHICS)
	@touch $@

$(FILESYSTEM_STAMP): Filesystem.kit/manifest $(FILESYSTEM_LIBRARY) \
		$(BUNDLE_TOOL)
	@mkdir -p $(FILESYSTEM)/libraries/filesystem.library/abi-1/1.0.0/m68k-68030
	cp Filesystem.kit/manifest $(FILESYSTEM)/manifest
	cp $(FILESYSTEM_LIBRARY) \
		$(FILESYSTEM)/libraries/filesystem.library/abi-1/1.0.0/m68k-68030/filesystem.library
	$(BUNDLE_TOOL) check $(FILESYSTEM)
	@touch $@

$(INTERFACE_STAMP): Interface.kit/manifest $(INTERFACE_LIBRARY) \
		$(INPUT_LIBRARY) $(BUNDLE_TOOL)
	@mkdir -p $(INTERFACE)/libraries/interface.library/abi-1/1.1.0/m68k-68030
	@mkdir -p $(INTERFACE)/libraries/input.library/abi-1/1.0.0/m68k-68030
	cp Interface.kit/manifest $(INTERFACE)/manifest
	cp $(INTERFACE_LIBRARY) \
		$(INTERFACE)/libraries/interface.library/abi-1/1.1.0/m68k-68030/interface.library
	cp $(INPUT_LIBRARY) \
		$(INTERFACE)/libraries/input.library/abi-1/1.0.0/m68k-68030/input.library
	$(BUNDLE_TOOL) check $(INTERFACE)
	@touch $@

$(EVENTS_STAMP): Events.kit/manifest $(EVENTS_LIBRARY) $(BUNDLE_TOOL)
	@mkdir -p $(EVENTS)/libraries/events.library/abi-1/1.0.0/m68k-68030
	cp Events.kit/manifest $(EVENTS)/manifest
	cp $(EVENTS_LIBRARY) \
		$(EVENTS)/libraries/events.library/abi-1/1.0.0/m68k-68030/events.library
	$(BUNDLE_TOOL) check $(EVENTS)
	@touch $@

$(MESSAGING_STAMP): Messaging.kit/manifest $(MESSAGING_LIBRARY) $(BUNDLE_TOOL)
	@mkdir -p $(MESSAGING)/libraries/messaging.library/abi-1/1.0.0/m68k-68030
	cp Messaging.kit/manifest $(MESSAGING)/manifest
	cp $(MESSAGING_LIBRARY) \
		$(MESSAGING)/libraries/messaging.library/abi-1/1.0.0/m68k-68030/messaging.library
	$(BUNDLE_TOOL) check $(MESSAGING)
	@touch $@

test: all

sanitize analyze:
	@true

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean
