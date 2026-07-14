.PHONY: architecture harte-smoke target musashi-target rtl-target test unit

PYTHON ?= python3
REPORT_DIR ?= /tmp

target: musashi-target rtl-target

musashi-target:
	$(MAKE) -C ../third_party/musashi conformance-target

rtl-target:
	$(MAKE) -C rtl

unit: target
	PYTHONPATH=.. $(PYTHON) -m pytest tests ../sw/harte/host/tests -q

architecture: target
	PYTHONPATH=.. $(PYTHON) -m conformance.runner cases \
		--report $(REPORT_DIR)/astra68-conformance-matrix.json

harte-smoke: target
	PYTHONPATH=.. $(PYTHON) ../sw/harte/host/harte_run.py \
		--target musashi-68030 --allow-unpinned-corpus \
		--report $(REPORT_DIR)/astra68-harte-musashi-smoke.json \
		../sw/harte/testdata/register-smoke.json
	PYTHONPATH=.. $(PYTHON) ../sw/harte/host/harte_run.py \
		--target rtl-tg68k030-mmu2 --allow-unpinned-corpus \
		--report $(REPORT_DIR)/astra68-harte-rtl-smoke.json \
		../sw/harte/testdata/register-smoke.json

test: unit architecture harte-smoke
