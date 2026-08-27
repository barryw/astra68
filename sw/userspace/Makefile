SUBDIRS := runtime metrics alloc storage vfs events input shell terminal graphics interface messaging network config \
	streams posix ntp commands supervisor services kits apps
SYSTEM_CATALOG := build/m68k/astra_events.cat
SUPERVISOR_ELF := supervisor/build/m68k/astra_supervisor.elf
TERMINAL_ELF := services/terminal/build/m68k/terminal.elf
CATALOG_TOOL := ../../tools/system_event_catalog.py

all:
	@for dir in $(SUBDIRS); do $(MAKE) -C $$dir $@ || exit $$?; done
	$(MAKE) system-catalog

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

.PHONY: all system-catalog test sanitize analyze clean
