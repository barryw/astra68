ifndef RUNTIME
$(error define RUNTIME before including dynamic-program.mk)
endif
ifndef TARGET
$(error define TARGET before including dynamic-program.mk)
endif
ifndef IMAGE
$(error define IMAGE before including dynamic-program.mk)
endif
ifndef LIBRARIES
$(error define LIBRARIES before including dynamic-program.mk)
endif

# Canonical native user-program link. Consumers declare their objects, direct
# shared-library ABI set, and any genuinely program-private archive inputs.
# Everything else -- interpreter, hardening, owner ordering, dependency audit,
# and stripping -- is identical for applications and services.
ASTRA_PROGRAM_OBJECTS ?= $(strip $(OBJECT) $(OBJECTS))
ASTRA_PROGRAM_PRIVATE_INPUTS ?=
ASTRA_PROGRAM_PRIVATE_OWNER_TARGETS ?=
ASTRA_PROGRAM_LINK_PREREQUISITES ?=
# Astra allocates each process's complete static TLS block before entry and
# does not provide the ELF general-dynamic __tls_get_addr ABI.
TARGET_FLAGS += -ftls-model=initial-exec
# Definitions consumed by the canonical linker script must precede that
# script. GNU ld evaluates the default assignments while it reads the script;
# appending an override afterward leaves the already-laid-out sections at the
# default addresses.
ASTRA_PROGRAM_LINK_SCRIPT_FLAGS ?=
ASTRA_PROGRAM_STRIP_FLAGS ?= --strip-all --remove-section=.astra_events
ASTRA_DYNAMIC_EXECUTABLE_CHECK ?= \
	$(ASTRA_REPOSITORY_ROOT)/tools/check_dynamic_executable.py
ASTRA_PROGRAM_CRT0 ?= $(RUNTIME)/build/m68k/crt0-dynamic.o
ASTRA_PROGRAM_LIBRARY_SONAMES := $(notdir $(LIBRARIES))
ASTRA_PROGRAM_LINK_FLAGS := -nostdlib -pie -Wl,-Bdynamic \
	-Wl,--no-as-needed -Wl,--no-undefined -Wl,-z,now -Wl,-z,relro \
	-Wl,-z,max-page-size=0x1000 -Wl,--build-id=none -Wl,--gc-sections \
	$(ASTRA_PROGRAM_LINK_SCRIPT_FLAGS) $(ASTRA_DYNAMIC_LINK_SCRIPT_FLAGS) \
	$(ASTRA_SHARED_LIBRARY_SEARCH_FLAGS)
ASTRA_PROGRAM_OWNER_TARGETS := $(call astra_library_owner_targets,\
	$(call astra_library_keys,$(LIBRARIES))) \
	$(ASTRA_PROGRAM_PRIVATE_OWNER_TARGETS)

include $(ASTRA_USERSPACE_ROOT)/program.mk

$(TARGET): $(ASTRA_PROGRAM_OBJECTS) $(ASTRA_PROGRAM_CRT0) \
		$(ASTRA_PROGRAM_PRIVATE_INPUTS) $(LIBRARIES) \
		$(ASTRA_PROGRAM_LINK_PREREQUISITES) \
		$(ASTRA_DYNAMIC_USER_LD_INPUTS) $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) \
		Makefile $(ASTRA_USERSPACE_ROOT)/dynamic-program.mk \
		$(ASTRA_USERSPACE_ROOT)/program.mk \
		$(ASTRA_USERSPACE_ROOT)/libraries.mk
	@mkdir -p $(@D)
	$(CC) $(TARGET_FLAGS) $(ASTRA_PROGRAM_LINK_FLAGS) \
		-o $@ $(ASTRA_PROGRAM_CRT0) \
		$(ASTRA_PROGRAM_OBJECTS) $(ASTRA_PROGRAM_PRIVATE_INPUTS) \
		$(foreach library,$(LIBRARIES),-Wl,-l:$(notdir $(library)))
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach soname,$(ASTRA_PROGRAM_LIBRARY_SONAMES),\
			--needed $(soname)) $@

$(IMAGE): $(TARGET)
	$(OBJCOPY) $(ASTRA_PROGRAM_STRIP_FLAGS) $< $@
	python3 $(ASTRA_DYNAMIC_EXECUTABLE_CHECK) --readelf $(READELF) \
		$(foreach soname,$(ASTRA_PROGRAM_LIBRARY_SONAMES),\
			--needed $(soname)) $@

size: $(IMAGE)
	$(SIZE) $(IMAGE)

.PHONY: size
