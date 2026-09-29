# Application bundles, built one way for every application.
#
#   include $(ASTRA_ROOT)/tools/astra-bundle.mk
#   $(eval $(call astra_app_bundle,BUILD,NAME,BINARY,MANIFEST,ICON,RESOURCES))
#   $(eval $(call astra_app_inventory,BUILD,NAMES))
#
# BUILD      output directory; the bundle is BUILD/NAME.app
# NAME       bundle name without ".app"; also the executable's file name in
#            bin/m68k-68040/ (the manifest's `executable` must agree, which
#            `astra-bundle check` verifies)
# BINARY     the linked m68k executable
# MANIFEST   the bundle manifest (source controlled)
# ICON       a built-in icon kind (terminal, gallery) or an image file that
#            tools/aicon.py converts; the manifest's `icon` names
#            resources/NAME.aicon
# RESOURCES  files copied flat into resources/ (may be empty)
#
# The rule is a stamp, BUILD/.NAME.app.stamp; depend on it to build the
# bundle. astra_app_inventory writes BUILD/.apps, the bundles that directory
# ships in the system image, one name per line: the image installs exactly
# those, and probes built with astra_app_bundle but left out stay out.

ASTRA_BUNDLE_TOOLS_DIR := $(patsubst %/,%,$(dir $(lastword $(MAKEFILE_LIST))))
ASTRA_BUNDLE_TOOL := $(ASTRA_BUNDLE_TOOLS_DIR)/build/astra-bundle
ASTRA_ICON_TOOL := $(ASTRA_BUNDLE_TOOLS_DIR)/aicon.py

$(ASTRA_BUNDLE_TOOL): FORCE_ASTRA_BUNDLE_TOOL
	$(MAKE) -C $(ASTRA_BUNDLE_TOOLS_DIR) all

FORCE_ASTRA_BUNDLE_TOOL:
.PHONY: FORCE_ASTRA_BUNDLE_TOOL

define astra_app_bundle
$(1)/.$(2).app.stamp: $(3) $(4) $(ASTRA_ICON_TOOL) $(ASTRA_BUNDLE_TOOL) \
		$(filter-out terminal gallery,$(5)) $(6)
	rm -rf $(1)/$(2).app
	@mkdir -p $(1)/$(2).app/bin/m68k-68040 $(1)/$(2).app/resources
	cp $(4) $(1)/$(2).app/manifest
	cp $(3) $(1)/$(2).app/bin/m68k-68040/$(2)
	python3 $(ASTRA_ICON_TOOL) $(5) $(1)/$(2).app/resources/$(2).aicon
	$(if $(strip $(6)),cp $(6) $(1)/$(2).app/resources/)
	$(ASTRA_BUNDLE_TOOL) check $(1)/$(2).app
	@touch $$@
endef

define astra_app_inventory
$(1)/.apps: $(foreach name,$(2),$(1)/.$(name).app.stamp) \
		FORCE_ASTRA_BUNDLE_TOOL
	@mkdir -p $(1)
	@printf '%s\n' $(foreach name,$(2),$(name).app) > $$@.new
	@if cmp -s $$@.new $$@; then rm -f $$@.new; else mv $$@.new $$@; fi
endef
