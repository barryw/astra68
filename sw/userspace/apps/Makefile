TERMINAL := ../services/terminal/build/m68k/terminal
GALLERY := ../applications/interface-gallery/build/m68k/interface-gallery
TERMINAL_MANIFEST := Terminal.app/manifest
GALLERY_MANIFEST := InterfaceGallery.app/manifest
ICON_TOOL := ../../../tools/aicon.py
BUNDLE_TOOL := ../../../tools/build/astra-bundle
TERMINAL_BUNDLE := build/Terminal.app
GALLERY_BUNDLE := build/InterfaceGallery.app
TERMINAL_STAMP := build/.Terminal.app.stamp
GALLERY_STAMP := build/.InterfaceGallery.app.stamp

all: $(TERMINAL_STAMP) $(GALLERY_STAMP)

$(TERMINAL):
	$(MAKE) -C ../services/terminal all

$(GALLERY):
	$(MAKE) -C ../applications/interface-gallery all

$(BUNDLE_TOOL): FORCE
	$(MAKE) -C ../../../tools all

$(TERMINAL_STAMP): $(TERMINAL_MANIFEST) $(TERMINAL) $(ICON_TOOL) $(BUNDLE_TOOL)
	rm -rf $(TERMINAL_BUNDLE)
	@mkdir -p $(TERMINAL_BUNDLE)/bin/m68k-68040 $(TERMINAL_BUNDLE)/resources
	cp $(TERMINAL_MANIFEST) $(TERMINAL_BUNDLE)/manifest
	cp $(TERMINAL) $(TERMINAL_BUNDLE)/bin/m68k-68040/Terminal
	python3 $(ICON_TOOL) terminal $(TERMINAL_BUNDLE)/resources/Terminal.aicon
	$(BUNDLE_TOOL) check $(TERMINAL_BUNDLE)
	@touch $@

$(GALLERY_STAMP): $(GALLERY_MANIFEST) $(GALLERY) $(ICON_TOOL) $(BUNDLE_TOOL)
	rm -rf $(GALLERY_BUNDLE)
	@mkdir -p $(GALLERY_BUNDLE)/bin/m68k-68040 $(GALLERY_BUNDLE)/resources
	cp $(GALLERY_MANIFEST) $(GALLERY_BUNDLE)/manifest
	cp $(GALLERY) $(GALLERY_BUNDLE)/bin/m68k-68040/InterfaceGallery
	python3 $(ICON_TOOL) gallery \
		$(GALLERY_BUNDLE)/resources/InterfaceGallery.aicon
	$(BUNDLE_TOOL) check $(GALLERY_BUNDLE)
	@touch $@

test: all

sanitize analyze:
	@true

clean:
	rm -rf build

FORCE:

.PHONY: all test sanitize analyze clean FORCE
