#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import tempfile


SCRIPT = Path(__file__).parents[1] / "check_stack_usage.py"
SPEC = importlib.util.spec_from_file_location("check_stack_usage", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


with tempfile.TemporaryDirectory() as directory:
    report = Path(directory) / "kernel.su"
    report.write_text(
        "kernel.c:1:1:safe\t3072\tdynamic,bounded\n"
        "kernel.c:2:1:unsafe\t3073\tdynamic,bounded\n"
    )
    assert MODULE.oversized([report], 3072) == [
        "kernel.c:2:1:unsafe: 3073 bytes (max 3072)"
    ]
    report.write_text("kernel.c:1:1:broken\tnot-a-size\tstatic\n")
    try:
        MODULE.oversized([report], 3072)
    except ValueError:
        pass
    else:
        raise AssertionError("malformed compiler stack data was accepted")

print("stack-usage checker tests passed")
