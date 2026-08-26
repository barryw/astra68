ifndef ASTRA_PROGRAM_OWNER_DIRS
$(error ASTRA_PROGRAM_OWNER_DIRS must name every library owner)
endif

ASTRA_PROGRAM_TARGETS ?= $(IMAGE) size

all:
	$(MAKE) libraries
	$(MAKE) program

libraries:
	@for directory in $(ASTRA_PROGRAM_OWNER_DIRS); do \
		$(MAKE) -C $$directory all || exit $$?; \
	done

program: $(ASTRA_PROGRAM_TARGETS)

.PHONY: all libraries program
