TERMINAL := ../services/terminal/build/m68k/terminal
MANIFEST := Terminal.app/manifest
ICON_TOOL := ../../../tools/aicon.py
BUNDLE_TOOL := ../../../tools/build/astra-bundle
BUNDLE := build/Terminal.app
STAMP := build/.Terminal.app.stamp

all: $(STAMP)

$(TERMINAL):
	$(MAKE) -C ../services/terminal all

$(BUNDLE_TOOL):
	$(MAKE) -C ../../../tools all

$(STAMP): $(MANIFEST) $(TERMINAL) $(ICON_TOOL) $(BUNDLE_TOOL)
	@mkdir -p $(BUNDLE)/bin/m68k-68030 $(BUNDLE)/resources
	cp $(MANIFEST) $(BUNDLE)/manifest
	cp $(TERMINAL) $(BUNDLE)/bin/m68k-68030/Terminal
	python3 $(ICON_TOOL) $(BUNDLE)/resources/Terminal.aicon
	$(BUNDLE_TOOL) check $(BUNDLE)
	@touch $@

test: all

sanitize analyze:
	@true

clean:
	rm -rf build

.PHONY: all test sanitize analyze clean
