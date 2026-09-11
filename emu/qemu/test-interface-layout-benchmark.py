#!/usr/bin/env python3
"""Parser contract for the target-side Interface Kit reflow benchmark."""

import importlib.util
import io
from contextlib import redirect_stdout
from pathlib import Path


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "test_terminal", HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)


class Machine:
    def __init__(self, lines):
        self.log = lines

    def wait_for_serial(self, marker, deadline):
        return marker == terminal.BOOT_MARKER and deadline > 0

    def wait_for_text(self, needles, deadline):
        if deadline <= 0:
            return None, 0
        if all(any(needle in line for line in self.log) for needle in needles):
            return self.log, 1
        return None, 0


valid = [
    "INTERFACE LAYOUT controls=0000000c iterations=00001000 "
    "elapsed-ns=0000000000001000",
    "INTERFACE LAYOUT controls=00000040 iterations=00000400 "
    "elapsed-ns=0000000000002000",
    "INTERFACE LAYOUT controls=00000100 iterations=00000100 "
    "elapsed-ns=0000000000003000",
    "INTERFACE UNDO n=00002710 rec=0000000000004000 "
    "undo=0000000000005000 redo=0000000000006000 bytes=0009c400",
    "stage 8",
]
assert terminal.interface_layout_benchmark(Machine(valid), 1)
with redirect_stdout(io.StringIO()):
    assert not terminal.interface_layout_benchmark(
        Machine(valid[:2] + valid[-2:]), 1)
slow = valid.copy()
slow[2] = ("INTERFACE LAYOUT controls=00000100 iterations=00000100 "
           "elapsed-ns=0000000027100001")
with redirect_stdout(io.StringIO()):
    assert not terminal.interface_layout_benchmark(Machine(slow), 1)
slow_undo = valid.copy()
slow_undo[-2] = (
    "INTERFACE UNDO n=00002710 rec=0000000007735941 "
    "undo=0000000000005000 redo=0000000000006000 bytes=0009c400")
with redirect_stdout(io.StringIO()):
    assert not terminal.interface_layout_benchmark(Machine(slow_undo), 1)
print("interface layout benchmark parser: PASS")
