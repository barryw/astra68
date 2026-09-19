#!/usr/bin/env python3
"""The terminal gate must wait for command completion, not output silence."""

import importlib.util
import contextlib
import io
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
    "(console_session.c:1414)") == 19
assert terminal.ready_sequence(
    "seq 20 debug 1000001a/78 act 00000003 seq 4 info shell ready   "
    "(console_session.c:1414)") is None


class Keyboard(terminal.Qmp):
    def __init__(self):
        self.keys = []

    def key(self, qcode):
        self.keys.append(qcode)

    def chord(self, modifier, qcode):
        self.keys.append((modifier, qcode))


keyboard = Keyboard()
keyboard.type_text("[]`{}~@#^&")
assert keyboard.keys == [
    "bracket_left", "bracket_right", "grave_accent",
    ("shift", "bracket_left"), ("shift", "bracket_right"),
    ("shift", "grave_accent"), ("shift", "2"), ("shift", "3"),
    ("shift", "6"), ("shift", "7")]


class Result:
    def __init__(self, returncode):
        self.returncode = returncode


commands = []
original_run = terminal.subprocess.run
terminal.subprocess.run = lambda command, **_kwargs: (
    commands.append(command) or Result(0))
workspace_rom = terminal.os.path.join(
    terminal.ROOT, "sw", "boot", "build", "astra_boot.bin")
assert terminal.refresh_workspace_rom(workspace_rom)
assert commands == [["make", "-C",
                     terminal.os.path.join(terminal.ROOT, "sw", "boot"),
                     "build/astra_boot.bin"]]

terminal.subprocess.run = lambda *_args, **_kwargs: Result(2)
failure = io.StringIO()
with contextlib.redirect_stdout(failure):
    assert not terminal.refresh_workspace_rom(workspace_rom)
assert "could not refresh the workspace boot ROM" in failure.getvalue()
terminal.subprocess.run = original_run

print("terminal command readiness test: PASS")
