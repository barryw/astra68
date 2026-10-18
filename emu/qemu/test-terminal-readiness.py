#!/usr/bin/env python3
"""Native gates must wait for current launch and command completion."""

import importlib.util
import contextlib
import io
from pathlib import Path
import tempfile
from types import SimpleNamespace
from unittest.mock import patch


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

with tempfile.TemporaryDirectory() as directory:
    image = Path(directory) / "fixture.img"
    image.write_bytes(b"fixture")
    commands.clear()

    def build_result(command, **_kwargs):
        commands.append(command)
        return Result(0 if command[-1] == "build/astra_boot.bin" else 2)

    failure = io.StringIO()
    with patch.object(terminal.astra_image.os, "geteuid", return_value=0), \
            patch.dict(terminal.os.environ, {
                "ASTRA_TEST_QEMU_UID": "1000", "ASTRA_TEST_QEMU_GID": "1000"}), \
            patch.object(terminal.astra_image.pwd, "getpwuid", return_value=
                         SimpleNamespace(pw_name="fixture-builder", pw_gid=1000)), \
            patch.object(terminal.subprocess, "run", side_effect=build_result), \
            patch.object(terminal.sys, "argv", ["test-terminal.py", "qemu", workspace_rom,
                         "--image", str(image), "--prepared-image", "--sbase-only"]), \
            contextlib.redirect_stdout(failure):
        assert terminal.main() == 1
    assert len(commands) == 2
    assert all(command[:5] == ["runuser", "-u", "fixture-builder", "--", "make"]
               for command in commands)
    assert commands[0][5:] == ["-C", terminal.os.path.join(terminal.ROOT, "sw", "boot"),
                               "build/astra_boot.bin"]
    assert commands[1][5:7] == ["-C", terminal.os.path.join(
        terminal.ROOT, "sw", "userspace", "commands")]
    assert "could not build integration test commands" in failure.getvalue()

font_spec = importlib.util.spec_from_file_location(
    "astra_fonts_gate", HERE / "test-fonts-ui.py")
fonts = importlib.util.module_from_spec(font_spec)
font_spec.loader.exec_module(fonts)


class FontsMachine:
    def __init__(self):
        self.clicked = False
        self.polls = 0
        self.qmp = SimpleNamespace(execute=self.display_count, double_click=self.click)

    def display_count(self, *_args):
        return int(self.clicked)

    def click(self, *_args):
        self.clicked = True

    def desktop_icon(self, *_args):
        return 1, 2

    def sequence(self):
        return 7

    def said(self, after):
        assert after == 7
        return [], 7

    def said_with_syslog(self):
        self.polls += 1
        return ["old launch /apps/Fonts.app status=0"], 7

    def syslog_lines(self):
        lines = ["old launch /apps/Fonts.app status=0"] + [
            "old fonts UI " + operation + " complete"
            for operation in ("import", "details", "preview", "confirmation", "removal")]
        if self.clicked:
            self.polls += 1
            if self.polls >= 2:
                lines.append("new launch /apps/Fonts.app status=0")
                lines.extend("new fonts UI " + operation + " complete"
                             for operation in ("import", "details", "preview", "confirmation", "removal"))
        return lines


machine = FontsMachine()
with patch.object(fonts.time, "sleep"):
    fonts.open_fonts(machine)
assert machine.polls == 2, "old launch log satisfied new Fonts launch"

for operation in ("import", "details", "preview", "confirmation", "removal"):
    machine = FontsMachine()
    syslog_before = len(machine.syslog_lines())
    machine.clicked = True
    with patch.object(fonts.time, "sleep"):
        fonts.wait_ui(machine, operation, 7, syslog_before)
    assert machine.polls == 2, "old log satisfied new Fonts " + operation

print("native launch and terminal command readiness test: PASS")
