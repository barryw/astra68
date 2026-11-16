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

# A live snapshot can stop between the records of one printed line.
reader = object.__new__(terminal.TraceReader)
rows = ['seq 11 debug 1000001a/78 act 00000003 FSSEM CLEAN PAGE readonl\\']
reader.trace = lambda: list(rows)
assert reader.said(10) == ([], 10), 'partial line advanced the reader cursor'
assert reader.said(10) == ([], 10), 'repeated snapshot exposed partial output'
rows += ['seq 12 debug 1000001a/78 act 00000003 y-owner zero-tail no-dirty reclaim PASS',
         'seq 13 debug 1000001a/78 act 00000003 next par\\']
assert reader.said(10) == (['FSSEM CLEAN PAGE readonly-owner zero-tail no-dirty reclaim PASS'], 12)
assert reader.said(12) == ([], 12)
rows.append('seq 14 debug 1000001a/78 act 00000003 t')
assert reader.said(12) == (['next part'], 14)
assert reader.said(14) == ([], 14)


class TraceMachine(terminal.Machine):
    def __init__(self, rows, replacement=None):
        self.reader = SimpleNamespace(hostfs_root='fixture')
        self.rows = list(rows)
        self.replacement = replacement
        self.now = 0.0
        self.events = []

    def trace(self):
        return list(self.events)

    def syslog_lines(self):
        return list(self.rows)

    def advance(self, seconds):
        self.now += seconds
        if self.replacement is not None:
            self.rows = list(self.replacement)


old = ('old desktop ready', 'cursor stamp')
for replacement in (None, old + ('new desktop ready',),
                    ('cursor stamp', 'new desktop ready'),
                    ('new desktop ready',)):
    machine = TraceMachine(old, replacement)
    with patch.object(terminal.time, 'monotonic', lambda: machine.now), \
            patch.object(terminal.time, 'sleep', machine.advance), \
            patch.object(terminal, 'read_host_syslog', lambda _root: '\n'.join(machine.rows)):
        observed = machine.wait_for_trace_text('desktop ready', 0.5, (7, old))
    assert observed == (replacement is not None), 'old syslog satisfied new readiness'
    assert machine.now > 0.0, 'readiness was accepted before a new event'

machine = TraceMachine(('old desktop ready',))
machine.events = ['seq 7 info 1/1 act 0 desktop ready']
cursor = machine.trace_cursor()
assert cursor == (7, ('old desktop ready',))
with patch.object(terminal.time, 'monotonic', lambda: machine.now), \
        patch.object(terminal.time, 'sleep', machine.advance):
    assert not machine.wait_for_trace_text('desktop ready', 0.5, cursor)
    machine.events.append('seq 8 info 1/1 act 0 desktop ready')
    assert machine.wait_for_trace_text('desktop ready', 0.5, cursor)

semantic_spec = importlib.util.spec_from_file_location('semantic_gate', HERE / 'test-fs-semantics.py')
semantic = importlib.util.module_from_spec(semantic_spec)
semantic_spec.loader.exec_module(semantic)
for ready in (False, True):
    machine = Machine(['FSSEM-GATE-9-0'], ready)
    machine.settle = lambda: True
    machine.sequence = lambda: 7
    machine.qmp = SimpleNamespace(type_line=lambda text: None)
    machine.said = lambda after=0: (['FSSEM-GATE-9-0'], 9)
    machine.trace_with_syslog = lambda: []
    with contextlib.redirect_stdout(io.StringIO()):
        try:
            answer = semantic.command(machine, 'true', 3.0, 9)
        except SystemExit:
            assert not ready
        else:
            assert ready, 'filesystem command returned before the next shell prompt'
            assert answer == (['FSSEM-GATE-9-0'], 0)

misleading = ('notice remote-desktop[7] log: remote desktop ready',
    'notice supervisor[1] log: launch /services/desktop-helper bytes=12 status=0',
    'notice supervisor[1] log: launch /services/desktop bytes=12 status=9')
machine = TraceMachine(misleading, misleading + (
     'notice supervisor[1] log: launch /services/desktop bytes=12 status=0',))
sampled_at = []
def sample_after_desktop(_path):
    sampled_at.append(machine.now)
    return SimpleNamespace(st_mtime_ns=len(sampled_at))
with patch.object(terminal.time, 'monotonic', lambda: machine.now), \
        patch.object(terminal.time, 'sleep', machine.advance), \
        patch.object(semantic.os, 'stat', sample_after_desktop):
    semantic.wait_for_hostfs_recovery(machine, 'fixture', (0, ()), 0.5)
assert sampled_at[0] > 0.0, 'remote desktop readiness satisfied desktop recovery'

clock = [0.0]
recovered = SimpleNamespace(wait_for_trace_text=lambda pattern, timeout, after:
                            pattern.search('launch /services/desktop bytes=12 status=0') and after == cursor)
with patch.object(semantic.time, 'monotonic', lambda: clock[0]), \
        patch.object(semantic.time, 'sleep', lambda seconds: clock.__setitem__(0, clock[0] + seconds)), \
        patch.object(semantic.os, 'stat', side_effect=[SimpleNamespace(st_mtime_ns=101),
                    SimpleNamespace(st_mtime_ns=101), SimpleNamespace(st_mtime_ns=99)]) as sampled:
    semantic.wait_for_hostfs_recovery(recovered, 'fixture', cursor, 1.0)
assert sampled.call_count == 3 and clock[0] > 0.0, 'old sample satisfied recovery'


for status in (0, 1, 88, 89):
    events = []
    machine = object.__new__(terminal.Machine)
    machine.process = SimpleNamespace(poll=lambda: status,
        wait=lambda timeout: events.append(('wait', timeout)) or status)
    machine.qmp = SimpleNamespace(execute=lambda *_args: events.append('quit'),
        close=lambda: events.append('close'))
    machine.launcher = ('provider',)
    machine._pump_thread = SimpleNamespace(join=lambda timeout: events.append(('join', timeout)))
    machine.recent_serial = lambda: events.append('serial')
    machine.runtime_directory = 'finished-runtime'
    with patch.object(terminal.shutil, 'rmtree', lambda path: events.append(('remove', path))):
        machine.close()
    assert events == [('wait', 120), 'close', ('join', 1), 'serial',
                      ('remove', 'finished-runtime')], 'finished provider received a QMP command'


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


class InputQmp(terminal.Qmp):
    def __init__(self, replies):
        self.requests = []
        self.file = SimpleNamespace(
            write=lambda frame: self.requests.append(terminal.json.loads(frame)),
            readline=lambda: terminal.json.dumps(replies.pop(0)))

    def word(self, address):
        assert address == terminal.INPUT_STATUS
        return 0


paused = {'error': {'class': 'GenericError', 'desc': 'VM not running'}}
events = [{'type': 'btn', 'data': {'button': 'left', 'down': True}}]
clock = [0.0]
machine = InputQmp([paused, {'return': {}}])
with patch.object(terminal.time, 'monotonic', lambda: clock[0]), \
        patch.object(terminal.time, 'sleep', lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
    machine.input_events(events, deadline=1.0)
assert machine.requests == [{'execute': 'input-send-event', 'arguments': {'events': events}}] * 2
assert clock[0] > 0.0, 'paused input was not deferred'

machine = InputQmp([{'error': {'class': 'GenericError', 'desc': 'Input handler not found'}}])
try:
    machine.input_events(events)
    raise AssertionError('unknown QMP failure was ignored')
except RuntimeError as error:
    assert 'Input handler not found' in str(error)
assert len(machine.requests) == 1, 'failed input was replayed'

clock = [0.0]
machine = InputQmp([paused] * 4)
with patch.object(terminal.time, 'monotonic', lambda: clock[0]), \
        patch.object(terminal.time, 'sleep', lambda seconds: clock.__setitem__(0, clock[0] + seconds)):
    try:
        machine.input_events(events, deadline=.002)
        raise AssertionError('stopped VM ignored input deadline')
    except RuntimeError as error:
        assert 'VM did not resume' in str(error)
assert clock[0] <= .002 and len(machine.requests) == 3


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

    def application_icon(self, *_args):
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

class LossMachine:
    def __init__(self, result):
        self.result = result
        self.acted = False
        self.qmp = SimpleNamespace(type_line=lambda _line: None)

    def sequence(self):
        return 7

    def trace_cursor(self):
        return 7, ()

    def wait_for_text(self, *_args):
        return ['FSSEM LOSS armed'], 8

    def wait_for_trace_text(self, text, _timeout, after):
        assert after == (7, ())
        return text == 'FSSEM LOSS armed' or (self.acted and text == self.result)

    def settle(self):
        pass

    def said(self, *_args):
        return [], 7

    def trace_with_syslog(self):
        return ['seq 8 debug 1/1 ' + self.result]


for result, passed in (('FSSEM LOSS PASS', True), ('FSSEM LOSS FAIL', False),
                       ('FSSEM LOSS none', False)):
    machine = LossMachine(result)
    clock = [0.0]
    with patch.object(semantic.time, 'monotonic', lambda: clock[0]), \
            patch.object(semantic.time, 'sleep', lambda seconds: clock.__setitem__(0, clock[0] + seconds)), \
            contextlib.redirect_stdout(io.StringIO()):
        try:
            semantic.loss(machine, 'dead', lambda: setattr(machine, 'acted', True))
            observed = True
        except SystemExit:
            observed = False
    assert observed == passed, 'loss result depended on surviving Terminal output'
    assert clock[0] < 180, 'native loss outcome was ignored until timeout'

shutdown_policy = semantic.load('shutdown_observation', 'test-service-policy.py')
for exit_at, result, broken_trace in ((1.0, 88, None), (8.0, 88, None),
                                     (8.0, 88, EOFError), (8.0, 88, ValueError),
                                     (None, 88, None), (1.0, 0, None)):
    clock = [0.0]
    snapshots = []
    typed = []
    def wait_shutdown(timeout):
        if exit_at is not None and clock[0] + timeout >= exit_at:
            clock[0] = max(clock[0], exit_at)
            return result
        clock[0] += timeout
        raise terminal.subprocess.TimeoutExpired('fixture-qemu', timeout)
    def shutdown_trace():
        snapshots.append(clock[0])
        if broken_trace:
            raise broken_trace('QEMU already exited; provider cleanup remains')
        return ['shutdown snapshot at %.1f' % clock[0]]
    machine = SimpleNamespace(process=SimpleNamespace(wait=wait_shutdown),
        qmp=SimpleNamespace(type_line=typed.append), trace_with_syslog=shutdown_trace)
    with patch.object(shutdown_policy.time, 'monotonic', lambda: clock[0]):
        try:
            shutdown_policy.clean_shutdown(machine, 30.0)
        except RuntimeError as error:
            assert exit_at is None or result != 88, str(error)
            if exit_at is None:
                assert 'shutdown snapshot at 5.0' in str(error)
                assert 'shutdown snapshot at 30.0' in str(error)
        else:
            assert exit_at is not None and result == 88
    assert typed == ['shutdown']
    assert clock[0] <= 30.0, 'shutdown diagnostics extended the deadline'
    if exit_at is None or exit_at > 5.0:
        assert snapshots and snapshots[0] == 5.0, 'shutdown evidence was collected after early state was lost'

print("native launch and terminal command readiness test: PASS")
