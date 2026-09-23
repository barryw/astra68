#!/usr/bin/env python3
"""The physical terminal tool must target the production QMP socket."""

import importlib.util
from pathlib import Path
import struct
import sys

import pytest


SOURCE = Path(__file__).resolve().parents[1] / "measure-terminal-text.py"
SPEC = importlib.util.spec_from_file_location("measure_terminal_text", SOURCE)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


def test_production_qmp_default(monkeypatch):
    observed = []

    class FakeQmp:
        def __init__(self, path, _deadline):
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


def test_quit_requests_clean_qemu_exit_and_reports_failure(monkeypatch):
    observed = []

    class FakeQmp:
        def __init__(self, _path, _deadline):
            pass

        def execute(self, command, arguments=None):
            observed.append(command)
            if arguments is not None:
                pytest.fail("quit must not send arguments")

    previous = sys.argv
    monkeypatch.setattr(module, "Qmp", FakeQmp)
    sys.argv = [str(SOURCE), "--quit"]
    try:
        module.main()
    finally:
        sys.argv = previous
    assert observed == ["quit"]

    def fail(_self, _command, _arguments=None):
        raise RuntimeError("QMP unavailable")

    monkeypatch.setattr(FakeQmp, "execute", fail)
    sys.argv = [str(SOURCE), "--quit"]
    try:
        with pytest.raises(RuntimeError, match="QMP unavailable"):
            module.main()
    finally:
        sys.argv = previous


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


def test_input_waits_for_idle_and_drain(monkeypatch):
    observed = []
    statuses = iter((2, 0, 2, 0))
    qmp = object.__new__(module.Qmp)
    qmp.deadline = 1.0
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
        def __init__(self, _path, _deadline):
            pass

    monkeypatch.setattr(module, "Qmp", FakeQmp)
    monkeypatch.setattr(module, "run", lambda _qmp, command, _quiet, _deadline:
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


def test_ready_sequence_matches_only_requested_user_event():
    capacity = 2
    blob = bytearray(module.TRACE_HEADER_SIZE +
                     capacity * module.TRACE_RECORD_SIZE)
    struct.pack_into(">IHHI", blob, 0, module.TRACE_MAGIC, 1,
                     module.TRACE_RECORD_SIZE, capacity)
    struct.pack_into(">I8xH", blob, module.TRACE_HEADER_SIZE,
                     41, module.TRACE_USER_EVENT)
    struct.pack_into(">I", blob, module.TRACE_HEADER_SIZE + 20, 0x12345678)
    second = module.TRACE_HEADER_SIZE + module.TRACE_RECORD_SIZE
    struct.pack_into(">I8xH", blob, second, 42, module.TRACE_USER_EVENT)
    struct.pack_into(">I", blob, second + 20, 0x87654321)

    assert module.ready_sequence(blob, 0x12345678) == 41
    assert module.ready_sequence(blob, 0x11111111) == 0


def test_wait_ready_accepts_a_new_event(monkeypatch):
    positions = iter(((41, 1, 2), (42, 0, 2)))
    monkeypatch.setattr(module, "trace_position",
                        lambda _qmp: next(positions))
    monkeypatch.setattr(module.time, "monotonic", lambda: 0.0)
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)
    qmp = object.__new__(module.Qmp)
    qmp.words = lambda _address, count: \
        [41, 0, 0, module.TRACE_USER_EVENT << 16, 0, 7, 0, 0] \
        if count == 8 else pytest.fail("wrong record size")

    assert module.wait_ready(
        qmp, "/tmp/trace", 7, next(positions), 1.0) == 0


def test_wait_ready_rejects_a_stale_event(monkeypatch):
    monkeypatch.setattr(module, "trace_position",
                        lambda _qmp: (41, 1, 2))
    times = iter((0.0, 2.0))
    monkeypatch.setattr(module.time, "monotonic", lambda: next(times))
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)

    with pytest.raises(RuntimeError, match="ready event timed out"):
        module.wait_ready(object(), "/tmp/trace", 7, (41, 1, 2), 1.0)


def test_terminal_open_has_independent_ready_deadlines(monkeypatch):
    deadlines = []

    class FakeQmp:
        def double_click(self, x, y):
            assert (x, y) == (70, 90)

    monkeypatch.setattr(module, "wait_ready",
                        lambda _qmp, _path, _message, _prior, deadline:
                        deadlines.append(deadline))
    monkeypatch.setattr(module, "wait_for_desktop",
                        lambda _qmp, _deadline: None)
    monkeypatch.setattr(module, "settled",
                        lambda _qmp, _quiet, _deadline: ({}, 0.0))
    monkeypatch.setattr(module, "trace_position",
                        lambda _qmp: (41, 1, 2))
    times = iter((10.0, 20.0))
    monkeypatch.setattr(module.time, "monotonic", lambda: next(times))

    module.open_terminal(FakeQmp(), 0.1, "/tmp/trace", 7, 8, 5.0)

    assert deadlines == [25.0]


def test_partial_profile_configuration_is_rejected_before_qmp(monkeypatch):
    monkeypatch.setattr(module, "Qmp",
                        lambda _path: pytest.fail("partial profile opened QMP"))
    previous = sys.argv
    sys.argv = [str(SOURCE), "--profile-control", "/tmp/profile.sock"]
    try:
        with pytest.raises(SystemExit, match="profiling requires"):
            module.main()
    finally:
        sys.argv = previous


def test_unprofiled_ready_measurement_is_allowed(monkeypatch, capsys):
    class FakeQmp:
        def __init__(self, _path, _deadline):
            pass

    monkeypatch.setattr(module, "Qmp", FakeQmp)
    monkeypatch.setattr(
        module, "run_ready",
        lambda _qmp, command, _quiet, control, _label, _trace, ready,
        _deadline: (0.123, {name: 0 for name in module.PROPERTIES})
        if command == "status" and control is None and ready == 7 else
        pytest.fail("wrong ready measurement"))
    previous = sys.argv
    sys.argv = [str(SOURCE), "--runs", "1", "--warmups", "0",
                "--ready-message", "7", "--trace-ring", "/tmp/trace",
                "status"]
    try:
        module.main()
    finally:
        sys.argv = previous

    assert '"milliseconds": 123.0' in capsys.readouterr().out


def test_latency_ceiling_accepts_fast_and_rejects_slow():
    module.enforce_latency([{"milliseconds": 499.999}], 500.0)
    with pytest.raises(SystemExit, match="501.000 ms exceeds 500.000 ms"):
        module.enforce_latency([{"milliseconds": 100.0},
                                {"milliseconds": 501.0}], 500.0)


def test_ready_latency_uses_guest_timestamp_not_observation_delay(monkeypatch):
    class FakeQmp:
        def type_without_enter(self, command):
            assert command == "status"

        def cpu_hz(self):
            return 10

        def cycles(self):
            return 100

        def key(self, qcode):
            assert qcode == "ret"

    counters = {name: 0 for name in module.PROPERTIES}
    monkeypatch.setattr(module, "settled",
                        lambda _qmp, _quiet, _deadline: (counters, 0.0))
    monkeypatch.setattr(module, "trace_position",
                        lambda _qmp: (7, 0, 8))
    monkeypatch.setattr(module, "wait_ready",
                        lambda *_arguments: 150)
    times = iter((0.0, 100.0, 200.0))
    monkeypatch.setattr(module.time, "monotonic", lambda: next(times))

    elapsed, result = module.run_ready(
        FakeQmp(), "status", 0.1, None, "test", "/tmp/trace", 1, 1.0)

    assert elapsed == 5.0
    assert result["guest-cycles"] == 50
    assert result["observation-milliseconds"] == 100000.0


def test_cpu_frequency_rejects_zero():
    qmp = object.__new__(module.Qmp)
    qmp.word = lambda _address: 0
    with pytest.raises(RuntimeError, match="frequency is zero"):
        qmp.cpu_hz()


def test_display_drained_requires_every_submission_to_complete():
    assert module.display_drained({"astra-display-submissions": 7,
                                   "astra-display-completions": 7})
    assert not module.display_drained({"astra-display-submissions": 8,
                                       "astra-display-completions": 7})


def test_desktop_wait_accepts_rendered_text(monkeypatch):
    qmp = object.__new__(module.Qmp)
    values = iter((0, 1))
    qmp.property = lambda _name: next(values)
    monkeypatch.setattr(module.time, "monotonic", lambda: 0.0)
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)

    module.wait_for_desktop(qmp, 1.0)


def test_desktop_wait_rejects_missing_render(monkeypatch):
    qmp = object.__new__(module.Qmp)
    qmp.property = lambda _name: 0
    times = iter((0.0, 2.0))
    monkeypatch.setattr(module.time, "monotonic", lambda: next(times))
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)

    with pytest.raises(RuntimeError, match="desktop did not render"):
        module.wait_for_desktop(qmp, 1.0)


def test_run_accepts_output_before_deadline(monkeypatch):
    qmp = object.__new__(module.Qmp)
    qmp.type_without_enter = lambda _command: None
    qmp.key = lambda _key: None
    values = iter((0, 1))
    qmp.property = lambda _name: next(values)
    counters = {name: 0 for name in module.PROPERTIES}
    counters["astra-display-glyph-commands"] = 0
    monkeypatch.setattr(module, "settled",
                        lambda _qmp, _quiet, _deadline: (counters, 0.5))
    monkeypatch.setattr(module.time, "monotonic", lambda: 0.0)
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)

    elapsed, _ = module.run(qmp, "status", 0.1, 1.0)

    assert elapsed == 0.4


def test_run_rejects_command_without_output(monkeypatch):
    qmp = object.__new__(module.Qmp)
    qmp.type_without_enter = lambda _command: None
    qmp.key = lambda _key: None
    qmp.property = lambda _name: 0
    counters = {name: 0 for name in module.PROPERTIES}
    times = iter((0.0, 0.0, 0.0, 2.0))
    monkeypatch.setattr(module, "settled",
                        lambda _qmp, _quiet, _deadline: (counters, 0.0))
    monkeypatch.setattr(module.time, "monotonic", lambda: next(times))

    with pytest.raises(RuntimeError, match="no display output"):
        module.run(qmp, "silent", 0.1, 1.0)


def test_input_queue_rejects_idle_timeout(monkeypatch):
    qmp = object.__new__(module.Qmp)
    qmp.deadline = 1.0
    qmp.word = lambda _address: module.INPUT_COUNT_MASK
    times = iter((0.0, 0.0, 2.0))
    monkeypatch.setattr(module.time, "monotonic", lambda: next(times))
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)

    with pytest.raises(RuntimeError, match="idle timed out"):
        qmp.input_events([{"type": "key"}])


def test_input_queue_rejects_drain_timeout(monkeypatch):
    qmp = object.__new__(module.Qmp)
    qmp.deadline = 1.0
    statuses = iter((0, 1, 1))
    qmp.word = lambda _address: next(statuses)
    qmp.execute = lambda _command, _arguments=None: None
    times = iter((0.0, 0.0, 2.0))
    monkeypatch.setattr(module.time, "monotonic", lambda: next(times))
    monkeypatch.setattr(module.time, "sleep", lambda _seconds: None)

    with pytest.raises(RuntimeError, match="drain timed out"):
        qmp.input_events([{"type": "key"}])
