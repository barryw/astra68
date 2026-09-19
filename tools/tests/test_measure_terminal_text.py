#!/usr/bin/env python3
"""The physical terminal tool must target the production QMP socket."""

import importlib.util
from pathlib import Path
import sys

import pytest


SOURCE = Path(__file__).resolve().parents[1] / "measure-terminal-text.py"
SPEC = importlib.util.spec_from_file_location("measure_terminal_text", SOURCE)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


def test_production_qmp_default(monkeypatch):
    observed = []

    class FakeQmp:
        def __init__(self, path):
            observed.append(path)

        def execute(self, command, arguments=None):
            return ""

    previous = sys.argv
    monkeypatch.setattr(module, "Qmp", FakeQmp)
    sys.argv = [str(SOURCE), "--dump-trace-ring", "/tmp/ignored.bin"]
    try:
        module.main()
    finally:
        sys.argv = previous

    assert observed == ["/run/astra/qmp.sock"]
    assert "/data/astra/run/qmp.sock" not in observed


def test_key_edges_are_atomic(monkeypatch):
    observed = []
    qmp = object.__new__(module.Qmp)
    qmp.input_events = lambda events: observed.append(events)

    qmp.key("a")

    assert observed == [[
        {"type": "key", "data": {"down": True,
         "key": {"type": "qcode", "data": "a"}}},
        {"type": "key", "data": {"down": False,
         "key": {"type": "qcode", "data": "a"}}},
    ]]
    assert len(observed) != 2


def test_input_waits_for_capacity_and_drain(monkeypatch):
    observed = []
    statuses = iter((30, 28, 30, 28))
    qmp = object.__new__(module.Qmp)
    qmp.word = lambda address: observed.append(("status", address)) or \
        next(statuses)
    qmp.execute = lambda command, arguments=None: observed.append(
        (command, arguments))
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)
    events = [{"type": "key"}, {"type": "key"}]

    qmp.input_events(events)

    assert observed[0:2] == [("status", module.INPUT_STATUS)] * 2
    assert observed[2] == ("input-send-event", {"events": events})
    assert observed[3:] == [("status", module.INPUT_STATUS)] * 2


def test_interactive_command_is_rejected_before_input(monkeypatch):
    previous = sys.argv
    monkeypatch.setattr(module, "Qmp",
                        lambda _path: pytest.fail(
                            "interactive command opened QMP"))
    sys.argv = [str(SOURCE), "posix"]
    try:
        with pytest.raises(SystemExit, match="posix is interactive"):
            module.main()
    finally:
        sys.argv = previous


def test_noninteractive_command_is_allowed(monkeypatch, capsys):
    class FakeQmp:
        def __init__(self, _path):
            pass

    monkeypatch.setattr(module, "Qmp", FakeQmp)
    monkeypatch.setattr(module, "run", lambda _qmp, command, _quiet:
                        (0.001, {name: 0 for name in module.PROPERTIES})
                        if command == "which status" else
                        pytest.fail("wrong command"))
    previous = sys.argv
    sys.argv = [str(SOURCE), "--runs", "1", "--warmups", "0",
                "which", "status"]
    try:
        module.main()
    finally:
        sys.argv = previous

    assert '"runs": 1' in capsys.readouterr().out
