#!/usr/bin/env python3
"""Unit checks for remote-desktop service state extraction."""

import importlib.util
from pathlib import Path


SOURCE = Path(__file__).with_name("test-remote-desktop-service.py")
SPEC = importlib.util.spec_from_file_location("remote_desktop_service", SOURCE)
service = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(service)


class FakeQmp:
    def __init__(self, machine, status):
        self.machine = machine
        self.status = status

    def type_line(self, text):
        marker = text.split("print -r -- ", 1)[1].removesuffix("$?")
        self.machine.lines = (["kill: no such process"] if self.status else [])
        self.machine.lines.append(marker + str(self.status))


class FakeMachine:
    def __init__(self, status):
        self.lines = []
        self.qmp = FakeQmp(self, status)

    def settle(self):
        pass

    def sequence(self):
        return 0

    def said(self, _after=0):
        return self.lines, len(self.lines)


assert service.service_pid(["state: running", "pid: 34 generation: 7"]) == 34
assert service.restarted_service_pid(
    ["state: running", "pid: 35 generation: 8"], 34) == 35
assert service.stable_service_pid(
    ["state: running", "pid: 35 generation: 8"], 35) == 35
lines, status = service.command_status(FakeMachine(0), "kill -9 35", 1.0, 1)
assert status == 0 and lines == ["RD-COMMAND-1-0"]
lines, status = service.command_status(FakeMachine(1), "kill -9 34", 1.0, 2)
assert status == 1 and "no such process" in lines[0]

try:
    service.restarted_service_pid(
        ["state: running", "pid: 34 generation: 8"], 34)
except RuntimeError as error:
    assert "stale PID 34" in str(error)
else:
    raise AssertionError("stale restart PID was accepted")

try:
    service.stable_service_pid(
        ["state: running", "pid: 36 generation: 8"], 35)
except RuntimeError as error:
    assert "changed from 35 to 36" in str(error)
else:
    raise AssertionError("unexpected service PID change was accepted")

try:
    service.service_pid(["state: running"])
except RuntimeError as error:
    assert "one PID" in str(error)
else:
    raise AssertionError("missing service PID was accepted")

print("remote desktop service state: PASS")
