include ../../../tools/astra-bundle.mk

TERMINAL := ../services/terminal/build/m68k/terminal
GALLERY := ../applications/interface-gallery/build/m68k/interface-gallery
SHIPPED := Terminal InterfaceGallery

all: build/.apps

$(TERMINAL): FORCE
	$(MAKE) -C ../services/terminal all

$(GALLERY): FORCE
	$(MAKE) -C ../applications/interface-gallery all

$(eval $(call astra_app_bundle,build,Terminal,$(TERMINAL),Terminal.app/manifest,terminal,))
$(eval $(call astra_app_bundle,build,InterfaceGallery,$(GALLERY),InterfaceGallery.app/manifest,gallery,))
$(eval $(call astra_app_inventory,build,$(SHIPPED)))

test: all
	@for app in $(TERMINAL) $(GALLERY); do \
		m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_INTERFACE_5.2 || exit 1; \
		m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_SYSTEM_2.2 || exit 1; \
		if m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_INTERFACE_5.1; then exit 1; fi; \
	done

sanitize analyze:
	@true

clean:
	rm -rf build

FORCE:

.PHONY: all test sanitize analyze clean FORCE
