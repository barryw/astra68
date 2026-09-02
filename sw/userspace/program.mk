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
	$(MAKE) program

libraries:
	@for directory in $(ASTRA_PROGRAM_OWNER_DIRS); do \
		$(MAKE) -C $$directory all || exit $$?; \
	done

program: $(ASTRA_PROGRAM_TARGETS)

.PHONY: all libraries program
