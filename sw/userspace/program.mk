ifndef ASTRA_PROGRAM_OWNER_DIRS
$(error ASTRA_PROGRAM_OWNER_DIRS must name every library owner)
endif

ASTRA_PROGRAM_TARGETS ?= $(IMAGE) size
ASTRA_PROGRAM_PRODUCTS := $(strip $(OBJECT) $(OBJECTS) $(TARGET) $(IMAGE))

# A clean program has no product for m68k-cross.mk's existing-product scan to
# discover. Declare the owner-standard products here so the identity stamp is
# created before the first compile, not by the following freshness check.
$(ASTRA_PROGRAM_PRODUCTS): $(ASTRA_TOOLCHAIN_STAMP)

all:
	$(MAKE) libraries
	$(MAKE) ASTRA_PROGRAM_OWNERS_READY=1 program

libraries:
	@for directory in $(ASTRA_PROGRAM_OWNER_DIRS); do \
		$(MAKE) -C $$directory all || exit $$?; \
	done

program: $(ASTRA_PROGRAM_TARGETS)

ASTRA_PROGRAM_DIRECT_GOALS := $(filter $(ASTRA_PROGRAM_TARGETS),$(MAKECMDGOALS))
ifeq ($(ASTRA_PROGRAM_OWNERS_READY),)
ifneq ($(ASTRA_PROGRAM_DIRECT_GOALS),)
$(ASTRA_PROGRAM_DIRECT_GOALS): | __astra_program_direct

__astra_program_direct:
	$(MAKE) libraries
	$(MAKE) ASTRA_PROGRAM_OWNERS_READY=1 $(ASTRA_PROGRAM_DIRECT_GOALS)

.PHONY: __astra_program_direct
endif
endif

.PHONY: all libraries program
