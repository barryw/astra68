include ../../../tools/astra-bundle.mk

TERMINAL := ../services/terminal/build/m68k/terminal
GALLERY := ../applications/interface-gallery/build/m68k/interface-gallery
ATLAS := ../applications/atlas/build/m68k/atlas
ATLAS_RESOURCES := build/atlas-resources
SHIPPED := Terminal InterfaceGallery Atlas

all: build/.apps

$(TERMINAL): FORCE
	$(MAKE) -C ../services/terminal all

$(GALLERY): FORCE
	$(MAKE) -C ../applications/interface-gallery all

$(ATLAS): FORCE
	$(MAKE) -C ../applications/atlas all

$(ATLAS_RESOURCES)/%.aicon: ../../../tools/aicon.py
	@mkdir -p $(@D)
	python3 $< $* $@

$(eval $(call astra_app_bundle,build,Terminal,$(TERMINAL),Terminal.app/manifest,terminal,))
$(eval $(call astra_app_bundle,build,InterfaceGallery,$(GALLERY),InterfaceGallery.app/manifest,gallery,))
$(eval $(call astra_app_bundle,build,Atlas,$(ATLAS),Atlas.app/manifest,atlas,$(addprefix $(ATLAS_RESOURCES)/,drawer.aicon document.aicon volume.aicon)))
$(eval $(call astra_app_inventory,build,$(SHIPPED)))

test: all
	@for app in $(TERMINAL) $(GALLERY) $(ATLAS); do \
		m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_INTERFACE_5.3 || exit 1; \
		m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_SYSTEM_3.0 || exit 1; \
		if m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_INTERFACE_5.1; then exit 1; fi; \
	done

sanitize analyze:
	@true

clean:
	rm -rf build

FORCE:

.PHONY: all test sanitize analyze clean FORCE
