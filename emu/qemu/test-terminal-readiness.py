#!/usr/bin/env python3
"""The terminal gate must wait for command completion, not output silence."""

import importlib.util
from pathlib import Path


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)


class Machine:
    def __init__(self, answer, ready):
        self.answer = answer
        self.ready = ready
        self.calls = []

    def wait_for_text(self, text, deadline, after=0, exact=False):
        self.calls.append((text, deadline, after, exact))
        return (self.answer, 9) if self.answer is not None else (None, 9)

    def wait_for_ready(self, deadline, after=0):
        self.calls.append(("ready", deadline, after, False))
        return self.ready


machine = Machine(["42"], ["shell ready"])
assert terminal.wait_for_command(machine, "42", 3.0, 7, exact=True) == ["42"]
assert machine.calls == [("42", 3.0, 7, True),
                         ("ready", 3.0, 7, False)]

machine = Machine(["42"], None)
assert terminal.wait_for_command(machine, "42", 3.0, 7) is None

assert terminal.ready_sequence(
    "seq 19 info 1000001a/78 act 00000003 shell ready   "
    "(console_shell.c:1414)") == 19
assert terminal.ready_sequence(
    "seq 20 debug 1000001a/78 act 00000003 seq 4 info shell ready   "
    "(console_shell.c:1414)") is None

print("terminal command readiness test: PASS")
