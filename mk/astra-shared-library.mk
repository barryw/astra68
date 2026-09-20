# One link policy for every Astra .library ELF.  Per-library Makefiles provide
# the SONAME and version script; this file owns format, binding, RELRO, and page
# layout so those security and loader contracts cannot drift.
ASTRA_SHARED_MK_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ASTRA_SHARED_LIBRARY_LD := $(ASTRA_SHARED_MK_DIR)astra-shared-library.ld
# A link recipe is part of the artifact's identity. Depending on both the
# owner Makefile and this common policy forces a relink when a dependency is
# added, removed, reordered, or hardened, even when every object is unchanged.
ASTRA_SHARED_OWNER_MAKEFILE := $(firstword $(MAKEFILE_LIST))
ASTRA_SHARED_POLICY_MAKEFILE := $(lastword $(MAKEFILE_LIST))
ASTRA_SHARED_LINK_INPUTS := $(ASTRA_SHARED_LIBRARY_LD) \
	$(ASTRA_SHARED_OWNER_MAKEFILE) $(ASTRA_SHARED_POLICY_MAKEFILE)
ifneq ($(ASTRA_SHARED_NO_CRT),1)
ASTRA_SHARED_CRT_BEGIN := $(shell $(CROSS)gcc -print-file-name=crtbeginS.o)
ASTRA_SHARED_CRT_END := $(shell $(CROSS)gcc -print-file-name=crtendS.o)
ASTRA_SHARED_LINK_INPUTS += $(ASTRA_SHARED_CRT_BEGIN) $(ASTRA_SHARED_CRT_END)
endif
# Compile products as well as the final DSO must become stale when their owner
# flags or this shared policy changes.
override .EXTRA_PREREQS += $(ASTRA_SHARED_OWNER_MAKEFILE) \
	$(ASTRA_SHARED_POLICY_MAKEFILE)
$(ASTRA_EXISTING_PRODUCTS): .EXTRA_PREREQS += \
	$(ASTRA_SHARED_OWNER_MAKEFILE) $(ASTRA_SHARED_POLICY_MAKEFILE)
ASTRA_SHARED_PIC_FLAGS := -fPIC -ftls-model=initial-exec
ASTRA_SHARED_LINK_FLAGS := -nostdlib -shared \
	-Wl,--no-undefined -Wl,-Bsymbolic -Wl,--build-id=none \
	-Wl,--gc-sections -Wl,--hash-style=sysv -Wl,-z,now -Wl,-z,relro \
	-Wl,-z,max-page-size=0x1000 -T $(ASTRA_SHARED_LIBRARY_LD) \
	$(ASTRA_SHARED_CRT_BEGIN)
ASTRA_SHARED_LINK_END := $(ASTRA_SHARED_CRT_END)

# Record the exact bytes used to derive an ABI map.  The content-addressed
# toolchain prerequisite detects installs whose mtimes move backwards; normal
# Make dependencies decide when this recipe runs.  Publishing a fresh stamp
# after that change prevents every later Make process from seeing it as stale.
define ASTRA_UPDATE_CONTENT_STAMP
	@mkdir -p $(@D)
	@rm -f $@.tmp
	@for input in $(1); do cksum "$$input"; done >$@.tmp
	@mv $@.tmp $@
endef

# A content-preserving generator may deliberately leave an unchanged output's
# mtime alone.  Mark successful outputs as current with their content stamp so
# Make does not regenerate the same ABI map forever, while a missing output
# still rebuilds normally.
define ASTRA_MARK_CONTENT_OUTPUTS
	@touch -r $(1) $(2)
endef
