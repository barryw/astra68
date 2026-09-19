ifndef RUNTIME
$(error define RUNTIME before including astra-link.mk)
endif

# One source of truth for every user-image linker-script dependency. Listing
# the shared fragments here ensures a contract edit always forces a relink.
ASTRA_USER_LD_COMMON := $(RUNTIME)/astra_user_contract.ld \
	$(RUNTIME)/astra_user_sections.ld
ASTRA_DYNAMIC_USER_LD := $(RUNTIME)/astra_user.ld
ASTRA_STATIC_USER_LD := $(RUNTIME)/astra_static_user.ld
ASTRA_DYNAMIC_USER_LD_INPUTS := $(ASTRA_DYNAMIC_USER_LD) \
	$(ASTRA_USER_LD_COMMON)
ASTRA_STATIC_USER_LD_INPUTS := $(ASTRA_STATIC_USER_LD) \
	$(ASTRA_USER_LD_COMMON)

# INCLUDE directives search the linker's script path. Keep that path beside
# the selected wrapper so both source-tree and packaged-NDK links are stable.
ASTRA_DYNAMIC_LINK_SCRIPT_FLAGS := -Wl,-L$(RUNTIME) \
	-T $(ASTRA_DYNAMIC_USER_LD)
ASTRA_STATIC_LINK_SCRIPT_FLAGS := -Wl,-L$(RUNTIME) \
	-T $(ASTRA_STATIC_USER_LD)
