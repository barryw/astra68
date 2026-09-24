SUBDIRS := compiler runtime metrics alloc storage vfs events input terminal graphics interface network config audio loader \
	streams posix terminfo ntp lua cxx commands supervisor services applications kits apps

# `all` and `test` both walk the same recursive producers.  A caller may ask
# for both goals with -j, but letting those recipes overlap makes two submakes
# configure or replace the same vendor/library products concurrently.  Keep
# this orchestration layer serial; recursive makes still inherit the jobserver
# and parallelize each producer's independent object graph.
.NOTPARALLEL:

include libraries.mk
NDK := ../../ndk
SYSTEM_CATALOG := build/m68k/astra_events.cat
SUPERVISOR_ELF := supervisor/build/m68k/astra_supervisor.elf
TERMINAL_ELF := services/terminal/build/m68k/terminal.elf
CATALOG_TOOL := ../../tools/system_event_catalog.py
SHARED_LIBRARY_OWNERSHIP_TOOL := ../../tools/check_shared_symbol_ownership.py
SHARED_LIBRARIES := $(ASTRA_SHARED_LIBRARIES)

all:
	$(MAKE) prune-shared-library-artifacts
	$(MAKE) -C $(NDK) all
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done
	$(MAKE) system-catalog
	$(MAKE) shared-library-ownership

# The process interpreter is intentionally absent: it is the bootstrap that
# loads these DSOs and therefore owns a closed, static runtime/VFS subset.
# Every ordinary shared library must have exactly one implementation owner for
# every symbol, including definitions hidden by its version script.
shared-library-ownership:
	$(MAKE) prune-shared-library-artifacts
	python3 $(SHARED_LIBRARY_OWNERSHIP_TOOL) --readelf m68k-astra-readelf \
		--all-libraries $(SHARED_LIBRARIES)

# Generated directories contain only the products selected by libraries.mk.
# ABI/SONAME changes therefore cannot leave an old file available for a Kit or
# release builder to select accidentally.
prune-shared-library-artifacts:
	@set -eu; \
	for directory in $(ASTRA_SHARED_LIBRARY_DIRS); do \
		test ! -d "$$directory" || \
			find "$$directory" -maxdepth 1 -type f -name '*.library*' -print; \
	done | while IFS= read -r product; do \
		case " $(SHARED_LIBRARIES) " in \
			*" $$product "*) ;; \
			*) echo "remove stale shared-library product: $$product"; \
				rm -f -- "$$product" ;; \
		esac; \
	done

sanitize analyze:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done

clean:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done
	rm -rf build

$(SUPERVISOR_ELF):
	$(MAKE) -C supervisor all

$(TERMINAL_ELF): $(SUPERVISOR_ELF)
	$(MAKE) -C services/terminal all

system-catalog: $(SYSTEM_CATALOG)

$(SYSTEM_CATALOG): $(SUPERVISOR_ELF) $(TERMINAL_ELF) $(CATALOG_TOOL)
	@mkdir -p $(@D)
	python3 $(CATALOG_TOOL) merge $@ $(SUPERVISOR_ELF) $(TERMINAL_ELF)

test:
	python3 ../../tools/tests/test_system_event_catalog.py
	python3 tests/test_boundaries.py
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir test || exit $$?; done

.PHONY: all prune-shared-library-artifacts shared-library-ownership \
	system-catalog test sanitize analyze clean
