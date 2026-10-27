include ../../../tools/astra-bundle.mk

TERMINAL := ../services/terminal/build/m68k/terminal
GALLERY := ../applications/interface-gallery/build/m68k/interface-gallery
ATLAS := ../applications/atlas/build/m68k/atlas
FONTS := ../applications/fonts/build/m68k/fonts
DISK_SETUP := ../applications/disk-setup/build/m68k/disk-setup
INSTALLER := ../applications/installer/build/m68k/installer
OPEN_WITH := ../applications/open-with/build/m68k/open-with
ATLAS_RESOURCES := build/atlas-resources
SHIPPED := Terminal InterfaceGallery Atlas Fonts DiskSetup Installer OpenWith

all: build/.apps

$(TERMINAL): FORCE
	$(MAKE) -C ../services/terminal all

$(GALLERY): FORCE
	$(MAKE) -C ../applications/interface-gallery all

$(ATLAS): FORCE
	$(MAKE) -C ../applications/atlas all

$(FONTS): FORCE
	$(MAKE) -C ../applications/fonts all

$(DISK_SETUP): FORCE
	$(MAKE) -C ../applications/disk-setup all

$(INSTALLER): FORCE
	$(MAKE) -C ../applications/installer all

$(OPEN_WITH): FORCE
	$(MAKE) -C ../applications/open-with all

$(ATLAS_RESOURCES)/%.aicon: ../../../tools/aicon.py
	@mkdir -p $(@D)
	python3 $< $* $@

$(eval $(call astra_app_bundle,build,Terminal,$(TERMINAL),Terminal.app/manifest,terminal,))
$(eval $(call astra_app_bundle,build,InterfaceGallery,$(GALLERY),InterfaceGallery.app/manifest,gallery,))
$(eval $(call astra_app_bundle,build,Atlas,$(ATLAS),Atlas.app/manifest,atlas,$(addprefix $(ATLAS_RESOURCES)/,drawer.aicon document.aicon volume.aicon installer.aicon)))
$(eval $(call astra_app_bundle,build,Fonts,$(FONTS),Fonts.app/manifest,fonts,))
$(eval $(call astra_app_bundle,build,DiskSetup,$(DISK_SETUP),DiskSetup.app/manifest,volume,))
$(eval $(call astra_app_bundle,build,Installer,$(INSTALLER),Installer.app/manifest,installer,))
$(eval $(call astra_app_bundle,build,OpenWith,$(OPEN_WITH),OpenWith.app/manifest,document,))
$(eval $(call astra_app_inventory,build,$(SHIPPED)))

test: all
	@for app in $(TERMINAL) $(GALLERY) $(ATLAS) $(FONTS) $(DISK_SETUP) $(INSTALLER) $(OPEN_WITH); do \
		m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_INTERFACE_7.0 || exit 1; \
		m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_SYSTEM_4.0 || exit 1; \
		if m68k-astra-readelf --version-info "$$app" | grep -q ASTRA_INTERFACE_5.; then exit 1; fi; \
	done
	@m68k-astra-readelf --version-info "$(GALLERY)" | grep -q ASTRA_INTERFACE_7.0

sanitize analyze:
	@true

clean:
	rm -rf build

FORCE:

.PHONY: all test sanitize analyze clean FORCE
