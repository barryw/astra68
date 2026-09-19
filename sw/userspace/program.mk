ifndef ASTRA_PROGRAM_OWNER_TARGETS
$(error ASTRA_PROGRAM_OWNER_TARGETS must name each owner as directory:target)
endif

ASTRA_PROGRAM_TARGETS ?= $(IMAGE) size
ASTRA_PROGRAM_PREPARE_TARGETS ?=
ASTRA_PROGRAM_PRODUCTS := $(strip $(OBJECT) $(OBJECTS) $(TARGET) $(IMAGE))

# A clean program has no product for m68k-cross.mk's existing-product scan to
# discover. Declare the owner-standard products here so the identity stamp is
# created before the first compile, not by the following freshness check.
$(ASTRA_PROGRAM_PRODUCTS): $(ASTRA_TOOLCHAIN_STAMP)

define ASTRA_BUILD_PROGRAM_OWNERS
for owner in $(ASTRA_PROGRAM_OWNER_TARGETS); do \
		directory=$${owner%%:*}; \
		target=$${owner#*:}; \
		test "$$directory" != "$$owner" && test -n "$$target" || { \
			printf 'invalid Astra program owner target: %s\n' "$$owner" >&2; \
			exit 2; \
		}; \
		$(MAKE) -C "$$directory" "$$target" || exit $$?; \
	done
endef

all:
	+@$(ASTRA_BUILD_PROGRAM_OWNERS)
	$(MAKE) prepare
	$(MAKE) ASTRA_PROGRAM_OWNERS_READY=1 program

libraries:
	+@$(ASTRA_BUILD_PROGRAM_OWNERS)

prepare: $(ASTRA_PROGRAM_PREPARE_TARGETS)

program: $(ASTRA_PROGRAM_TARGETS)

ASTRA_PROGRAM_DIRECT_GOALS := $(filter $(ASTRA_PROGRAM_TARGETS),$(MAKECMDGOALS))
ifeq ($(ASTRA_PROGRAM_OWNERS_READY),)
ifneq ($(ASTRA_PROGRAM_DIRECT_GOALS),)

# GNU Make expands a direct target's prerequisite graph in parallel.  A normal
# target prerequisite is therefore too late to rebuild an archive: another
# branch can inspect the old archive before its owner finishes.  Remake this
# included gate first so Make restarts, then evaluates the program graph from a
# fresh view of every owner product.
ASTRA_PROGRAM_OWNER_GATE := build/.program-owners-ready.mk
include $(ASTRA_PROGRAM_OWNER_GATE)

ifeq ($(MAKE_RESTARTS),)
$(ASTRA_PROGRAM_OWNER_GATE): __astra_program_owner_gate_force
	+@$(ASTRA_BUILD_PROGRAM_OWNERS)
	$(MAKE) prepare
	@mkdir -p $(@D)
	@touch $@

.PHONY: __astra_program_owner_gate_force
endif
endif
endif

.PHONY: all libraries prepare program
