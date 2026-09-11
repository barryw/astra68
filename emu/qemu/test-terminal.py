#!/usr/bin/env python3
"""Drive the Astra zsh session from outside and judge what it answered.

Everything below the terminal has a gate; the terminal itself had none, and
the gap cost a session. A refused input syscall made the shell yield forever:
no fault, no message, nothing on the serial stream -- the boot log ends at
`stage 8` whether the terminal works or not, so every existing check passed
while nothing responded to a key.

This types into the machine over QMP and reads back what the shell printed.

**Where the text comes from changed.** This gate used to read cells out of
VEGA's POST text window, because the terminal owned that plane and wrote it
with ASTRA_SYSCALL_CONSOLE_WRITE. The terminal is a window client now: it
draws glyphs into a surface the display service composites, nothing writes the
character plane any more, and a screen made of pixels says nothing to anything
but an eye. So the terminal model echoes each completed line into the kernel
trace ring -- `astra_terminal_set_echo`, installed by the session host -- and this
reads the ring. It is the same text, from the same place a panic report and
the debugger already read, and it works on a machine with no screen attached.

What is asserted:

  * the input queue drains. A count left in the Vesta FIFO means nobody is
    consuming keys, which is what a silently refused ASTRA_SYSCALL_INPUT_READ_TRY
    looks like from here.
  * a file written through the shell can be listed and read back.
  * every command in SCRIPT answers with what it is supposed to answer.

The machine boots the desktop, and the terminal is opened the way a person
opens it: a double click on its icon. That is not decoration -- it is the only
way a terminal starts now, so a gate that skipped it would be testing a
configuration the product does not have.

The image is copied first, so a run neither depends on nor disturbs the state
of the one it was given.
"""

import argparse
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import queue
import time

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import astra_image
from qemu_runtime import DEFAULT_MEMORY, qemu_environment

# RAM the gate boots with. A module global so --memory can prove that the
# machine works at a size other than the one it was written against.
MEMORY = DEFAULT_MEMORY
import trace_decode

# Vesta input block; the low five bits are the queued count.
INPUT_STATUS = 0xFFF0070C
INPUT_COUNT_MASK = 0x1F
RTC_STATUS = 0xFFF00420
RTC_NS_LO = 0xFFF00424
RTC_NS_HI = 0xFFF00428
RTC_VALID = 1 << 0

# The kernel trace ring, at the fixed address the loader retains it at.
RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000

BOOT_MARKER = "stage 8"
INTERFACE_LAYOUT = re.compile(
    r"INTERFACE LAYOUT controls=([0-9a-f]{8}) "
    r"iterations=([0-9a-f]{8}) elapsed-ns=([0-9a-f]{16})")
INTERFACE_LAYOUT_CASES = {12: 4096, 64: 1024, 256: 256}
INTERFACE_UNDO = re.compile(
    r"INTERFACE UNDO n=([0-9a-f]{8}) "
    r"rec=([0-9a-f]{16}) undo=([0-9a-f]{16}) "
    r"redo=([0-9a-f]{16}) bytes=([0-9a-f]{8})")
INTERFACE_UNDO_OPERATIONS = 10000
# The physical 69.874 MHz MC68040 baseline is 7.546 us/group for record,
# 3.958 for undo, and 3.292 for redo.  The shared 12.5 us ceiling preserves
# 39% headroom over the slowest phase while rejecting a material regression.
INTERFACE_UNDO_MAX_NS_PER_OPERATION = 12_500
# The physical 70 MHz MC68040 baseline is 6.90--7.55 us/control across the
# three cases.  Ten microseconds preserves at least 32% scheduling headroom
# while rejecting a material regression before layout becomes a visible part
# of a 60 Hz frame.
INTERFACE_LAYOUT_MAX_NS_PER_CONTROL = 10_000

# The desktop's Terminal icon. A double click here is what starts a terminal;
# there is no manifest entry that starts one directly, because a window client
# with no window server to ask is a program that exits before it draws.
TERMINAL_ICON = (70, 90)

# The shipped plain MOTD is zsh's startup fallback. Seeing it proves the
# terminal launched zsh and zsh read its system startup file.
BANNER = "Astra 68"
POSIX_COMMAND = 'posix -R +42 --cmd "set number" -- WORK:notes.txt'
DURABILITY_COMMAND = (
    "posix --durability WORK:durability-cut.txt durable-data-68040")

ZSH_SCRIPT = [
    # The result is beyond 32 bits, proving the cross-configured arithmetic
    # type and its decimal formatting on the actual MC68040 target.
    ('print $((4294967296+7))', "4294967303"),
    ('print -r -- ZSH-PATH-$PATH', "ZSH-PATH-/commands"),
    ('print -r -- ZSH-HOME-$HOME', "ZSH-HOME-HOME:"),
    # A bare external command must resolve through Astra's /commands PATH;
    # Status is checked in the same zsh command that launched the program.
    ('status 23; print -r -- ZSH-STATUS-$?',
     "ZSH-STATUS-23"),
    ('print $((6*7)) | cat', "42"),
    ('/commands/echo 42 | cat', "42"),
    ('print -r -- ZSH-SUB-$(print ok)', "ZSH-SUB-ok"),
    ('print -r -- ZSH-EXEC-$(/commands/echo 42)',
     "ZSH-EXEC-42"),
    # External commands use the same inherited descriptor table as ported
    # programs; neither a pipe nor a redirect may fall back to startup streams.
    ('print -r -- ZSH-PIPE-$(/commands/echo 42 | cat)',
     "ZSH-PIPE-42"),
    ('print -r -- ZSH-WHICH-$(/commands/which status | cat)',
     "ZSH-WHICH-/commands/status"),
    ('print $((6*7)) > WORK:zsh.out; cat WORK:zsh.out; rm WORK:zsh.out',
     "42"),
    ('/commands/echo 42 > WORK:zsh-command.out; '
     'print -r -- ZSH-FILE-$(cat WORK:zsh-command.out); '
     'rm WORK:zsh-command.out', "ZSH-FILE-42"),
    ('/commands/date > WORK:zsh-date.out; '
     '[[ -s WORK:zsh-date.out ]] && print ZSH-DATE-FILE; '
     'rm WORK:zsh-date.out', "ZSH-DATE-FILE"),
    ('/commands/ls -z 2> WORK:zsh-error.out; '
     '[[ -s WORK:zsh-error.out ]] && print ZSH-STDERR-FILE; '
     'rm WORK:zsh-error.out', "ZSH-STDERR-FILE"),
]

SCRIPT = [
    ("mkdir proto; print ASTRA-MKDIR", "ASTRA-MKDIR"),
    ("print -r -- 'via the protocol' > hello.txt", "hello.txt"),
    ("ls", "proto/"),
    ("cd proto; pwd", "/CWD/proto"),
    ("mkdir inner; print ASTRA-INNER", "ASTRA-INNER"),
    ("ls", "inner/"),
    ("print -r -- hi > scratch.txt; cat scratch.txt", "hi"),
    ("rm scratch.txt; print ASTRA-RM-$?", "ASTRA-RM-0"),
    ("cd ..; pwd", "/CWD"),
    ("cat hello.txt", "via the protocol"),
    ("print no > EVENTS:no", "permission denied"),
    ("ls EVENTS:", "activity/"),
    ("ls EVENTS:boot/current", "earliest"),
    ("events", "shell ready"),
    ("status 7; print ASTRA-STATUS-$?", "ASTRA-STATUS-7"),
    ("status; print ASTRA-STATUS-$?", "ASTRA-STATUS-0"),
    ("/commands/status 3; print ASTRA-STATUS-$?", "ASTRA-STATUS-3"),
    ("print no > COMMANDS:status", "permission denied"),
    ("ls COMMANDS:", ("devices  [0]", "devices  [1]", "which  [1]")),
    ("rm COMMANDS:doesnotexist", "not found"),
    ("nosuchthing", "command not found"),
    ("print ASTRA-STATUS-$?", "ASTRA-STATUS-127"),
    ("which status", "/commands/status"),
    ("which devices", "/commands/devices"),
    ("devices status", "/commands/status [1]"),
    ("ps", ("ROM:supervisor", "SERVICES:desktop", "APPS:Terminal.app",
            " ps", " zsh")),
    ("metrics", ("host.channel.commands ", "hostfs.vfs.requests ",
                 "host.fs.open.calls ")),
    ("events --boot -1", "namespace bound"),
    (POSIX_COMMAND, "POSIX RAW PASS"),
    ("print ASTRA-STATUS-$?", "ASTRA-STATUS-0"),
    (DURABILITY_COMMAND,
     ("ASTRA DURABILITY SYNCED", "ASTRA DURABILITY PASS")),
    ("posix --durability-check WORK:durability-cut.txt durable-data-68040",
     "ASTRA DURABILITY EXACT"),
    ("lua -v", "Lua 5.5.1"),
    ("lua -e \"print(6*7)\"", "42"),
    ("lua -e \"print(1/2)\"", "0.5"),
    ("print -r -- 'print(6*7)' > luafile.lua; lua luafile.lua", "42"),
    ("rm luafile.lua; print ASTRA-LUA-RM-$?", "ASTRA-LUA-RM-0"),
    ("FOO=bar lua -e \"print(os.getenv('FOO'))\"", "bar"),
    ("lua -e \"local a,b,c=os.execute('status 23');print(a,b,c)\"",
     "exit"),
    ("lua -e 'print(os.date(\"!%Y\"))'", "2026"),
    ("lua -e \"local f=assert(io.open('luatest','w'));"
     "f:write('ok');f:close();assert(os.rename('luatest','luanew'));"
     "print(assert(io.open('luanew')):read('*a'));os.remove('luanew')\"",
     "ok"),
    *ZSH_SCRIPT,
    ("mkdir 'two words'; ls", "two words/"),
    ("which status > out.txt", "out.txt"),
    ("cat out.txt", "/commands/status"),
    ("which devices >> out.txt", "out.txt"),
    ("cat out.txt", ("/commands/status",
                     "/commands/devices")),
    ("pwd > pwd.txt; cat pwd.txt", "/CWD"),
    ("ls >", "parse error"),
    ("GREETING=hello; print -r -- $GREETING", "hello"),
    ("unset GREETING; print -r -- -$GREETING-", "--"),
    ("PHRASE='two words'; print -r -- -$PHRASE-", "-two words-"),
    ("print -r -- $PHRASE > phrase.txt; cat phrase.txt", "two words"),
    ("A=1 /commands/echo scoped; print -r -- ${A-unset}", "unset"),
]

VIM_LUA_FILE = "vim-created.lua"

# What a person waits for: Enter, to the answer on the screen.
#
# It used to time `status`, whose whole output was the shell's "exited 0" line.
# The shell does not print that any more -- a status is `$?` -- so a command
# with no output has nothing to wait for, and the baseline was reset here on
# commands that answer. The numbers below are therefore not comparable with
# the pre-2026-08-19 compatibility aliases.
PERFORMANCE_SCRIPT = [
    ("hello", "hello from picolibc"),
    ("echo one", "one"),
    ("which status", "/commands/status"),
    ("which devices", "/commands/devices"),
    ("devices status", "/commands/status [1]"),
]
PERFORMANCE_BUDGET_SECONDS = {
    "hello": 8.0,
    "echo one": 8.0,
    "which status": 8.0,
    "which devices": 7.0,
    "devices status": 7.0,
}

QCODE = {" ": "spc", "\n": "ret", "/": "slash", ".": "dot", "-": "minus",
         "=": "equal", ",": "comma", ";": "semicolon", "'": "apostrophe",
         "[": "bracket_left", "]": "bracket_right",
         "`": "grave_accent", "\\": "backslash"}
# Keys that need a modifier held. A capital is handled in type_text as the
# shift chord over its lowercase key; these are the punctuation that only
# exists shifted, and `+` is here because `date +FORMAT` needs it.
SHIFTED = {":": "semicolon", "+": "equal", "%": "5", "_": "minus",
           "?": "slash", "\"": "apostrophe", ">": "dot", "<": "comma",
           "$": "4", "(": "9", ")": "0", "*": "8", "!": "1",
           "@": "2", "#": "3", "^": "6", "&": "7",
           "{": "bracket_left", "}": "bracket_right",
           "~": "grave_accent", "|": "backslash"}

# A user record the decoder rendered. The body is what the shell wrote; a
# record whose text did not fit one record ends in a backslash and continues
# in the next, which is why this gate never matches against a single record.
RECORD = re.compile(
    r"^seq\s+(\d+)\s+\S+\s+[0-9a-f]{8}/\d+(?:\s+act\s+[0-9a-f]+)?\s(.*)$")
READY_RECORD = re.compile(
    r"^seq\s+(\d+)\s+info\s+[0-9a-f]{8}/\d+"
    r"(?:\s+act\s+[0-9a-f]+)?\s+shell ready\s+"
    r"\(console_session\.c:\d+\)$")
TRACE_RECORD = re.compile(r"^seq\s+(\d+)\s+")


def ready_sequence(line):
    match = READY_RECORD.match(line)
    return int(match.group(1)) if match is not None else None


class Qmp:
    def __init__(self, path, deadline=20.0, process=None):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        end = time.monotonic() + deadline
        while True:
            try:
                self.socket.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if process is not None and process.poll() is not None:
                    self.socket.close()
                    raise RuntimeError(
                        "QEMU exited with status %d before QMP was available" %
                        process.returncode)
                if time.monotonic() >= end:
                    self.socket.close()
                    raise
                time.sleep(0.05)
        self.file = self.socket.makefile("rwb", buffering=0)
        while True:
            greeting = json.loads(self.file.readline())
            if "event" in greeting:
                continue
            if "QMP" not in greeting:
                raise RuntimeError("invalid QMP greeting: %s" % greeting)
            break
        self.execute("qmp_capabilities")

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write(json.dumps(request).encode("ascii") + b"\n")
        while True:
            reply = json.loads(self.file.readline())
            if "event" in reply:
                continue
            if "error" in reply:
                raise RuntimeError("QMP %s failed: %s" % (command,
                                                          reply["error"]))
            return reply.get("return")

    def close(self):
        self.file.close()
        self.socket.close()

    def monitor(self, line):
        return self.execute("human-monitor-command", {"command-line": line})

    def word(self, address):
        for token in self.monitor("xp /1xw 0x%x" % address).split():
            if token.startswith("0x") and len(token) == 10:
                return int(token, 16)
        raise RuntimeError("no word read back from 0x%x" % address)

    def input_events(self, events):
        queued = self.word(INPUT_STATUS) & INPUT_COUNT_MASK
        while queued > INPUT_COUNT_MASK - len(events):
            time.sleep(0.001)
            queued = self.word(INPUT_STATUS) & INPUT_COUNT_MASK
        self.execute("input-send-event", {"events": events})
        while (self.word(INPUT_STATUS) & INPUT_COUNT_MASK) > queued:
            time.sleep(0.001)

    def send(self, down, qcode):
        self.input_events([
            {"type": "key",
             "data": {"down": down,
                      "key": {"type": "qcode", "data": qcode}}}])

    def key(self, qcode):
        # Deliver both edges atomically so a slow guest cannot auto-repeat a
        # key while the host is waiting to send its release.
        self.input_events([
            {"type": "key", "data": {
                "down": down, "key": {"type": "qcode", "data": qcode}}}
            for down in (True, False)])

    def chord(self, modifier, qcode):
        # The keymap translates a press against the modifiers held at that
        # moment, so the modifier stays down across both edges of the key.
        self.send(True, modifier)
        self.key(qcode)
        self.send(False, modifier)

    def type_line(self, text):
        self.type_text(text)
        self.key("ret")

    def type_text(self, text):
        for character in text:
            if character in QCODE:
                self.key(QCODE[character])
            elif character in SHIFTED:
                self.chord("shift", SHIFTED[character])
            elif character.isupper() and character.isalpha():
                # QMP's qcodes are the unshifted key names, so a capital is
                # the shift chord rather than a code of its own.
                self.chord("shift", character.lower())
            elif character.isalnum():
                self.key(character)
            else:
                raise RuntimeError("no qcode for %r" % character)

    def point(self, x, y):
        self.input_events([
            {"type": "abs", "data": {"axis": "x", "value": x}},
            {"type": "abs", "data": {"axis": "y", "value": y}}])

    def button(self, down):
        self.input_events([
            {"type": "btn", "data": {"button": "left", "down": down}}])

    def double_click(self, x, y):
        self.point(x, y)
        for _ in range(2):
            self.button(True)
            self.button(False)


class Machine:
    def __init__(self, qemu, rom, image, socket_directory, extra_args=(),
                 hostfs_root=None):
        # Resolve every source-relative input before starting QEMU.  A missing
        # catalog must not leave a live emulator behind.
        self.names = trace_decode.kernel_event_names(
            os.path.join(ROOT, "sw/kernel/trace.h"))
        self.catalog = trace_decode.load_catalogs([
            os.path.join(ROOT, "sw/userspace", "supervisor", "build",
                         "m68k", "astra_supervisor.elf"),
            os.path.join(ROOT, "sw/userspace", "services", "terminal",
                         "build", "m68k", "terminal.elf")])
        self.runtime_directory = tempfile.mkdtemp(prefix="astra-qmp-")
        self.qmp_path = os.path.join(self.runtime_directory, "qmp.sock")
        self.ring_path = os.path.join(socket_directory, "ring.bin")
        command = ([qemu, "-M", "astra68", "-m", MEMORY, "-bios", rom,
                    "-display", "none", "-monitor", "none", "-serial", "stdio",
                    "-no-reboot",
                    "-qmp", "unix:%s,server=on,wait=off" % self.qmp_path] +
                   list(extra_args) +
                   ["-drive", "if=none,format=raw,file=%s" % image])
        hostfs_root = hostfs_root or os.environ.get(
            "ASTRA_HOSTFS_ROOT", os.path.join(socket_directory, "hostfs"))
        environment = qemu_environment(qemu, hostfs_root=hostfs_root)
        self.process = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1, env=environment)
        self.serial = queue.Queue()
        self.log = []
        self._pump_thread = threading.Thread(target=self._pump, daemon=True)
        self._pump_thread.start()
        try:
            self.qmp = Qmp(self.qmp_path, process=self.process)
            if (self.qmp.word(RTC_STATUS) & RTC_VALID) == 0:
                raise RuntimeError(
                    "Astra machine started without the host wall clock")
            low = self.qmp.word(RTC_NS_LO)
            high = self.qmp.word(RTC_NS_HI)
            if abs(((high << 32) | low) - time.time_ns()) > 600_000_000_000:
                raise RuntimeError(
                    "Astra machine started with the wrong host wall clock")
        except Exception:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
            shutil.rmtree(self.runtime_directory)
            raise
    def _pump(self):
        for line in self.process.stdout:
            self.serial.put(line.rstrip("\n"))
        self.serial.put(None)

    def wait_for_serial(self, marker, deadline):
        end = time.monotonic() + deadline
        while time.monotonic() < end:
            try:
                line = self.serial.get(timeout=0.2)
            except queue.Empty:
                continue
            if line is None:
                return False
            self.log.append(line)
            if marker in line:
                return True
        return False

    def recent_serial(self, limit=20):
        while True:
            try:
                line = self.serial.get_nowait()
            except queue.Empty:
                break
            if line is not None:
                self.log.append(line)
        return self.log[-limit:]

    def word(self, address):
        return self.qmp.word(address)

    def trace(self):
        reply = self.qmp.monitor('pmemsave 0x%08x %d "%s"'
                                 % (RING_ADDRESS, RING_SIZE, self.ring_path))
        if reply and reply.strip():
            raise RuntimeError("ring unavailable: %s" % reply.strip())
        with open(self.ring_path, "rb") as handle:
            _, rendered = trace_decode.decode(handle.read(), self.catalog,
                                               self.names)
        return rendered

    def said(self, after=0):
        """Every line the machine has printed since sequence `after`.

        Continuations are rejoined here rather than left to the caller: a
        record holds twenty bytes, so most of what this gate looks for --
        `/commands/status [1]`, `namespace bound` -- straddles two of them, and
        a check against single records would fail on the length of its own
        needle rather than on anything the machine did.
        """
        lines = []
        pending = ""
        highest = after
        for line in self.trace():
            match = RECORD.match(line)
            if match is None:
                continue
            sequence = int(match.group(1))
            body = match.group(2)
            continued = body.endswith("\\")
            if continued:
                body = body[:-1]
            if sequence > after:
                pending += body
                highest = max(highest, sequence)
            if not continued and pending:
                lines.append(pending)
                pending = ""
        if pending:
            lines.append(pending)
        return lines, highest

    def sequence(self):
        return self.said()[1]

    def trace_sequence(self):
        highest = 0
        for line in self.trace():
            match = TRACE_RECORD.match(line)
            if match is not None:
                highest = max(highest, int(match.group(1)))
        return highest

    def wait_for_trace_text(self, text, deadline, after=0):
        end = time.monotonic() + deadline
        while True:
            for line in self.trace():
                match = TRACE_RECORD.match(line)
                if (match is not None and int(match.group(1)) > after and
                        text in line):
                    return True
            if time.monotonic() >= end:
                return False
            time.sleep(0.25)

    def recent_faults(self):
        return [line for line in self.trace() if "fault" in line.lower()][-8:]

    def wait_for_text(self, text, deadline, after=0, exact=False):
        """Waits until every needle has been printed since `after`.

        `after` is what keeps a command from being judged by an earlier one's
        output: the ring is cumulative, and `exited 0` is true of something on
        almost every boot.
        """
        needles = (text,) if isinstance(text, str) else tuple(text)
        end = time.monotonic() + deadline
        while True:
            lines, highest = self.said(after)
            if all(any(line == needle if exact else needle in line
                       for line in lines)
                   for needle in needles):
                return lines, highest
            if time.monotonic() >= end:
                return None, highest
            time.sleep(0.25)

    def wait_for_ready(self, deadline, after=0):
        end = time.monotonic() + deadline
        while True:
            if any((sequence := ready_sequence(line)) is not None and
                   sequence > after for line in self.trace()):
                return True
            if time.monotonic() >= end:
                return False
            time.sleep(0.25)

    def settle(self, deadline=8.0, quiet=0.4):
        """Waits until the machine has stopped printing.

        Not politeness: the shell drops what is typed at it before it has
        printed its next prompt, and the prompt carries no newline, so there
        is no record that says "ready". What there is instead is silence --
        the last of the previous answer having been written and nothing
        following it. Typing into the tail of a redraw loses the first
        characters of the line and the shell then answers a question nobody
        asked, which is a failure that reads like a bug in the command.
        """
        end = time.monotonic() + deadline
        last = self.sequence()
        while time.monotonic() < end:
            time.sleep(quiet)
            current = self.sequence()
            if current == last:
                return True
            last = current
        return False

    def close(self):
        try:
            self.qmp.execute("quit")
        except (BrokenPipeError, EOFError, OSError, RuntimeError):
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
        self.qmp.close()
        self._pump_thread.join(timeout=1)
        self.recent_serial()
        shutil.rmtree(self.runtime_directory)


def open_terminal(machine, boot_deadline, command_deadline):
    """Boots to the desktop and opens a terminal the way a person does."""
    if not machine.wait_for_serial(BOOT_MARKER, boot_deadline):
        print("FAIL: never reached the desktop; last serial lines:")
        for line in machine.log[-8:]:
            print("    %s" % line)
        return False
    # The desktop has to have painted before a click lands on an icon, and
    # what says it painted is the launch report for the app it is running.
    time.sleep(2.0)
    before = machine.sequence()
    machine.qmp.double_click(*TERMINAL_ICON)
    lines, _ = machine.wait_for_text(BANNER, command_deadline, before)
    if lines is None:
        print("FAIL: the terminal never drew its banner after a double click")
        for line in machine.said()[0][-40:]:
            print("    |%s|" % line)
        for line in machine.recent_faults():
            print("    %s" % line)
        print("last serial lines:")
        for line in machine.recent_serial():
            print("    %s" % line)
        return False
    if not machine.wait_for_ready(command_deadline, before):
        print("FAIL: the terminal drew but never became ready")
        for line in machine.said(before)[0][-40:]:
            print("    |%s|" % line)
        return False
    return True


def interface_layout_benchmark(machine, boot_deadline):
    if not machine.wait_for_serial(BOOT_MARKER, boot_deadline):
        print("FAIL: interface layout benchmark never reached the desktop")
        return False
    needles = tuple("INTERFACE LAYOUT controls=%08x" % count
                    for count in INTERFACE_LAYOUT_CASES) + (
                        "INTERFACE UNDO n=%08x" %
                        INTERFACE_UNDO_OPERATIONS,)
    lines, _ = machine.wait_for_text(needles, boot_deadline)
    if lines is None:
        print("FAIL: interface layout benchmark did not finish")
        return False
    results = {}
    for line in lines:
        match = INTERFACE_LAYOUT.search(line)
        if match is None:
            continue
        count, iterations, elapsed = (int(value, 16)
                                      for value in match.groups())
        if count in results or INTERFACE_LAYOUT_CASES.get(count) != iterations:
            print("FAIL: malformed or duplicate layout result: %s" % line)
            return False
        results[count] = elapsed
    if set(results) != set(INTERFACE_LAYOUT_CASES) or any(
            elapsed == 0 for elapsed in results.values()):
        print("FAIL: incomplete interface layout benchmark: %r" % results)
        return False
    if any(elapsed > INTERFACE_LAYOUT_MAX_NS_PER_CONTROL *
           INTERFACE_LAYOUT_CASES[count] * count
           for count, elapsed in results.items()):
        print("FAIL: interface layout exceeded %u ns/control: %r" %
              (INTERFACE_LAYOUT_MAX_NS_PER_CONTROL, results))
        return False
    for count in sorted(results):
        iterations = INTERFACE_LAYOUT_CASES[count]
        elapsed = results[count]
        print("%3u controls  %5u reflows  %9.1f ns/reflow  %7.1f ns/control" %
              (count, iterations, elapsed / iterations,
               elapsed / (iterations * count)))
    undo_results = []
    for line in lines:
        match = INTERFACE_UNDO.search(line)
        if match is not None:
            undo_results.append(tuple(int(value, 16)
                                      for value in match.groups()))
    if len(undo_results) != 1:
        print("FAIL: missing or duplicate undo benchmark: %r" % undo_results)
        for line in lines:
            if "INTERFACE UNDO" in line:
                print("    %s" % line)
        return False
    operations, record_ns, undo_ns, redo_ns, history_bytes = undo_results[0]
    if operations != INTERFACE_UNDO_OPERATIONS or min(
            record_ns, undo_ns, redo_ns, history_bytes) == 0:
        print("FAIL: malformed undo benchmark: %r" % (undo_results[0],))
        return False
    if max(record_ns, undo_ns, redo_ns) > (
            INTERFACE_UNDO_MAX_NS_PER_OPERATION * operations):
        print("FAIL: undo exceeded %u ns/operation: %r" %
              (INTERFACE_UNDO_MAX_NS_PER_OPERATION, undo_results[0]))
        return False
    print("%5u undo groups  record %7.1f ns/op  undo %7.1f ns/op  "
          "redo %7.1f ns/op  %u bytes" %
          (operations, record_ns / operations, undo_ns / operations,
           redo_ns / operations, history_bytes))
    print("ASTRA INTERFACE UNDO PASS")
    print("ASTRA INTERFACE LAYOUT PASS")
    return True


def wait_for_command(machine, expected, deadline, before, exact=False):
    """Return only after both the answer and the following prompt exist."""
    said, _ = machine.wait_for_text(expected, deadline, before, exact=exact)
    if said is None:
        return None
    return said if machine.wait_for_ready(deadline, before) else None


def zsh_interactive(machine, command_deadline, verbose=False):
    """Exercise the default zsh's own line editor on the terminal TTY."""
    # Use a second terminal to prove the first terminal is already running
    # zsh, rather than a session host which merely launches one on demand.
    before = machine.sequence()
    machine.qmp.double_click(*TERMINAL_ICON)
    if machine.wait_for_text(BANNER, command_deadline, before)[0] is None:
        print("FAIL: no observer terminal opened for interactive zsh")
        return False
    if not machine.wait_for_ready(command_deadline, before):
        print("FAIL: observer zsh never became ready")
        return False
    before = machine.sequence()
    machine.qmp.type_line("ps; print ASTRA-PS-DONE")
    observed = wait_for_command(machine, "ASTRA-PS-DONE", command_deadline,
                                before, exact=True)
    if observed is None or sum(" zsh" in line for line in observed) < 2:
        print("FAIL: both terminal windows did not own zsh sessions")
        for text in machine.said(before)[0][-80:]:
            print("    |%s|" % text)
        return False

    # Move the observer away, return focus to zsh, and edit a typo through
    # ZLE. The resulting line can only be produced if raw input and erase
    # handling are owned by zsh rather than the Terminal session host.
    machine.qmp.point(500, 105)
    machine.qmp.button(True)
    machine.qmp.point(700, 155)
    machine.qmp.button(False)
    machine.qmp.point(200, 140)
    machine.qmp.button(True)
    machine.qmp.button(False)
    before = machine.sequence()
    machine.qmp.type_text("print -r -- ASTRA-INTERACTIVE-ZSX")
    machine.qmp.key("backspace")
    machine.qmp.key("backspace")
    machine.qmp.type_text("SH")
    machine.qmp.key("ret")
    if machine.wait_for_text("ASTRA-INTERACTIVE-ZSH", command_deadline,
                             before, exact=True)[0] is None:
        print("FAIL: zsh ZLE did not edit and execute the interactive line")
        return False

    before = machine.sequence()
    machine.qmp.key("up")
    machine.qmp.key("ret")
    if machine.wait_for_text("ASTRA-INTERACTIVE-ZSH", command_deadline,
                             before, exact=True)[0] is None:
        print("FAIL: zsh ZLE history did not replay the interactive line")
        return False

    finished_after = machine.trace_sequence()
    machine.qmp.type_line("exit")
    if not machine.wait_for_trace_text("finished with status 0",
                                       command_deadline, finished_after):
        print("FAIL: interactive zsh did not exit cleanly")
        return False
    if verbose:
        print("ok: default zsh ZLE, history, and clean terminal exit")
    return True


def vim_creates_and_runs_lua(machine, command_deadline, verbose=False):
    """Create a Lua program through Vim's full-screen terminal interface."""
    machine.settle()
    vim_before = machine.sequence()
    machine.qmp.type_line(
        "vim -Nu NONE -n -c \"call setline(1,'')\" -c write -- WORK:" +
        VIM_LUA_FILE)

    # Vim owns the first terminal now. Open a second one to observe the ready
    # file written by Vim's post-startup command, then move it aside and return
    # focus to the editor. This is a readiness handshake, not a guessed sleep.
    before = machine.sequence()
    machine.qmp.double_click(*TERMINAL_ICON)
    if machine.wait_for_text(BANNER, command_deadline, before)[0] is None:
        print("FAIL: no observer terminal opened while Vim was running")
        return False
    end = time.monotonic() + command_deadline
    while time.monotonic() < end:
        machine.settle()
        startup_lines = machine.said(vim_before)[0]
        if any("vim: crashed" in line for line in startup_lines):
            print("FAIL: Vim crashed during startup")
            for line in startup_lines:
                print("    %s" % line)
            for line in machine.recent_faults():
                print("    %s" % line)
            return False
        before = machine.sequence()
        machine.qmp.type_line("ls")
        said = wait_for_command(machine, VIM_LUA_FILE, command_deadline,
                                before)
        if said is not None:
            break
    else:
        print("FAIL: Vim never completed startup")
        for line in machine.said(vim_before)[0]:
            print("    %s" % line)
        before = machine.sequence()
        machine.qmp.type_line("ps")
        machine.settle()
        for line in machine.said(before)[0]:
            print("    %s" % line)
        return False

    machine.qmp.point(500, 105)
    machine.qmp.button(True)
    machine.qmp.point(700, 155)
    machine.qmp.button(False)
    interaction_before = machine.sequence()
    machine.qmp.point(200, 140)
    machine.qmp.button(True)
    machine.qmp.button(False)
    machine.settle()

    machine.qmp.key("esc")
    machine.qmp.key("i")
    machine.qmp.type_text("print(6*7)")
    machine.qmp.key("esc")
    machine.qmp.type_line(":write")

    # Read the file from the observer before allowing Vim to exit. This proves
    # that the keys reached the editor and that Vim, rather than the shell or
    # the test harness, wrote the program.
    machine.qmp.point(1100, 200)
    machine.qmp.button(True)
    machine.qmp.button(False)
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("cat " + VIM_LUA_FILE)
    if wait_for_command(machine, "print(6*7)", command_deadline, before,
                        exact=True) is None:
        print("FAIL: Vim did not write the Lua program")
        for line in machine.said(interaction_before)[0][-80:]:
            print("    |%s|" % line)
        return False

    machine.qmp.point(200, 140)
    machine.qmp.button(True)
    machine.qmp.button(False)
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line(":q")
    if not machine.wait_for_ready(command_deadline, before):
        print("FAIL: Vim did not exit cleanly after writing the Lua program")
        return False

    machine.qmp.point(1100, 200)
    machine.qmp.button(True)
    machine.qmp.button(False)
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("lua " + VIM_LUA_FILE)
    if wait_for_command(machine, "42", command_deadline, before,
                        exact=True) is None:
        print("FAIL: Lua did not execute the program created in Vim")
        for line in machine.said(before)[0][-80:]:
            print("    |%s|" % line)
        return False
    if verbose:
        print("ok: Vim created %s and Lua returned 42" % VIM_LUA_FILE)

    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("rm " + VIM_LUA_FILE + "; print ASTRA-VIM-RM-$?")
    if wait_for_command(machine, "ASTRA-VIM-RM-0", command_deadline,
                        before) is None:
        print("FAIL: the Vim-created Lua fixture was not removed")
        return False
    return True


def warm_the_store(qemu, rom, image, temporary, boot_deadline,
                   command_deadline):
    """Boots once and does something, so there is a boot to read back.

    The last line of SCRIPT asks the machine for the boot before this one,
    which is the event store surviving a restart -- and that is only
    observable from the boot after one. So this is not a warmup in the sense
    of caches: without it the assertion has nothing to be true of, and the
    command correctly answers that no previous boot is stored.
    """
    warmup_dir = os.path.join(temporary, "warmup")
    os.mkdir(warmup_dir)
    machine = Machine(qemu, rom, image, warmup_dir)
    try:
        if not open_terminal(machine, boot_deadline, command_deadline):
            print("FAIL: the boot before the run never reached a terminal")
            return False
        before = machine.sequence()
        machine.qmp.type_line("mkdir priorboot; print ASTRA-WARM-$?")
        if wait_for_command(machine, "ASTRA-WARM-0",
                            command_deadline, before) is None:
            print("FAIL: the boot before the run answered no command")
            return False
        machine.settle()
        return True
    finally:
        machine.close()


def run(qemu, rom, image, catalog, boot_deadline, command_deadline, verbose,
        report_timings, prepared_image, performance_only, vim_gate,
        network_only, vim_only, cxx_only, zsh_only, interface_layout_only):
    timings = []
    with tempfile.TemporaryDirectory(prefix="astra-terminal-") as temporary:
        scratch = os.path.join(temporary, "card.img")
        shutil.copyfile(image, scratch)
        # Into the copy, so the image this gate was pointed at is untouched.
        if not prepared_image:
            astra_image.install(scratch, catalog)
        needs_warm_store = not (performance_only or network_only or vim_only or
                                cxx_only or zsh_only or interface_layout_only)
        if needs_warm_store and not warm_the_store(
                qemu, rom, scratch, temporary, boot_deadline,
                command_deadline):
            return 1
        run_dir = os.path.join(temporary, "run")
        os.mkdir(run_dir)
        machine = Machine(qemu, rom, scratch, run_dir)
        try:
            if interface_layout_only:
                return 0 if interface_layout_benchmark(
                    machine, boot_deadline) else 1
            if not open_terminal(machine, boot_deadline, command_deadline):
                return 1
            script = (ZSH_SCRIPT if zsh_only else
                      [('cxx', 'ASTRA C++ PASS')] if cxx_only else
                      [] if vim_only else
                      [(POSIX_COMMAND, "POSIX RAW PASS")] if network_only else
                      PERFORMANCE_SCRIPT if performance_only else SCRIPT)
            for line, expected in script:
                before = machine.sequence()
                started = time.monotonic()
                machine.qmp.type_line(line)
                if line == POSIX_COMMAND:
                    ready, _ = machine.wait_for_text(
                        "POSIX RAW READY", command_deadline, before)
                    if ready is None:
                        print("FAIL: posix never entered raw terminal mode")
                        return 1
                    machine.qmp.key("up")
                said = wait_for_command(machine, expected, command_deadline,
                                        before,
                                        exact=zsh_only or line == "echo $?")
                elapsed = time.monotonic() - started
                if said is None:
                    print("FAIL: %r never answered with %r" % (line, expected))
                    for text in machine.said(before)[0][-80:]:
                        print("    |%s|" % text)
                    print("recent trace records:")
                    for text in machine.trace()[-40:]:
                        print("    %s" % text)
                    print("last serial lines:")
                    for text in machine.recent_serial():
                        print("    %s" % text)
                    return 1
                timings.append((line, elapsed))
                if verbose:
                    print("ok: %-38s %6.2fs  %s" % (line, elapsed, expected))
            if (vim_gate or vim_only) and not vim_creates_and_runs_lua(
                    machine, command_deadline, verbose):
                return 1
            if vim_only:
                print("ASTRA VIM LUA PASS")
                return 0
            if zsh_only:
                if not zsh_interactive(machine, command_deadline, verbose):
                    return 1
                print("ASTRA ZSH PASS")
                return 0
            if performance_only:
                failed = False
                for line, elapsed in timings:
                    budget = PERFORMANCE_BUDGET_SECONDS[line]
                    print("%-24s %6.2fs  budget %5.2fs%s"
                          % (line, elapsed, budget,
                             "" if elapsed <= budget else "   OVER"))
                    failed = failed or elapsed > budget
                if failed:
                    print("FAIL: a command exceeded its budget")
                    return 1
                print("ASTRA TERMINAL PERFORMANCE PASS")
                return 0
            if network_only:
                if report_timings:
                    for line, elapsed in timings:
                        print("%-38s %6.2fs" % (line, elapsed))
                print("ASTRA NETWORK PASS")
                return 0

            # `>` truncates, and that is a claim about what is *not* in the
            # file -- which no needle in SCRIPT can make, because every needle
            # there has to be found. The file holds two answers by now; one
            # `>` and it must hold only the newer one.
            machine.settle()
            before = machine.sequence()
            machine.qmp.type_line("which devices > out.txt")
            # The status, because a redirected command prints nothing at all
            # and the shell no longer narrates one.
            if not machine.wait_for_ready(command_deadline, before):
                print("FAIL: the truncating redirect never completed")
                return 1
            before = machine.sequence()
            machine.qmp.type_line("echo $?")
            if wait_for_command(machine, "0", command_deadline, before,
                                exact=True) is None:
                print("FAIL: the truncating redirect never finished")
                return 1
            before = machine.sequence()
            machine.qmp.type_line("cat out.txt")
            said = wait_for_command(machine, "/commands/devices",
                                    command_deadline, before)
            if said is None:
                print("FAIL: the truncated file did not hold the new answer")
                return 1
            if any("/commands/status" in line for line in said):
                print("FAIL: `>` kept what was in the file; it must truncate")
                for text in said[-20:]:
                    print("    |%s|" % text)
                return 1

            # Nobody consuming keys is what a silently refused input read
            # looks like from out here, and it is invisible on the serial
            # stream: the queue simply fills and the shell yields forever.
            queued = machine.word(INPUT_STATUS) & INPUT_COUNT_MASK
            if queued != 0:
                print("FAIL: %d input events left unconsumed" % queued)
                return 1

            # The prompt has to still be there at the end. A shell that
            # answered every line and then died would pass every check above.
            before = machine.sequence()
            machine.qmp.type_line("print ASTRA-STILL-PROMPTING")
            if wait_for_command(machine, "ASTRA-STILL-PROMPTING",
                                command_deadline, before, exact=True) is None:
                print("FAIL: the shell stopped prompting")
                return 1

            if report_timings:
                print("command latency, Enter to answer:")
                for line, elapsed in timings:
                    print("  %-38s %6.2fs" % (line, elapsed))
            print("ASTRA TERMINAL PASS %d commands" % len(timings))
            return 0
        except (BrokenPipeError, ConnectionError, EOFError, OSError) as error:
            status = machine.process.poll()
            print("FAIL: QEMU exited during terminal gate: %s (status %s)" %
                  (error, "running" if status is None else status))
            print("last serial lines:")
            for text in machine.recent_serial(200):
                print("    %s" % text)
            return 1
        finally:
            machine.close()


def main():
    global MEMORY

    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="qemu-system-m68k carrying astra68")
    parser.add_argument("rom", help="astra_boot.bin")
    parser.add_argument("--image", required=True,
                        help="card image with an ext4 volume; copied, not written")
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG,
        help="the .astra_events bytes to place on the volume as SYS:" +
             astra_image.CATALOG_NAME)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    parser.add_argument("--command-deadline", type=float, default=60.0)
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--memory", default=MEMORY,
                        help="RAM to boot with, e.g. 512M")
    parser.add_argument("--report-timings", action="store_true",
                        help="print Enter-to-answer command latency")
    parser.add_argument("--prepared-image", action="store_true",
                        help="use an image that already contains the fixture")
    parser.add_argument("--performance-only", action="store_true",
                        help="run the command-latency budget gate")
    parser.add_argument("--vim-gate", action="store_true",
                        help="also run the interactive Vim-to-Lua gate")
    parser.add_argument("--vim-only", action="store_true",
                        help="run only the interactive Vim-to-Lua gate")
    parser.add_argument("--network-only", action="store_true",
                        help="run only the POSIX networking integration gate")
    parser.add_argument("--cxx-only", action="store_true",
                        help="run only the C++ runtime integration gate")
    parser.add_argument("--zsh-only", action="store_true",
                        help="run only the upstream zsh integration gate")
    parser.add_argument("--interface-layout-only", action="store_true",
                        help="run only the target interface reflow benchmark")
    arguments = parser.parse_args()
    MEMORY = arguments.memory
    return run(arguments.qemu, arguments.rom, arguments.image,
               arguments.catalog, arguments.boot_deadline,
               arguments.command_deadline, arguments.verbose,
               arguments.report_timings, arguments.prepared_image,
               arguments.performance_only, arguments.vim_gate,
               arguments.network_only, arguments.vim_only,
               arguments.cxx_only, arguments.zsh_only,
               arguments.interface_layout_only)


if __name__ == "__main__":
    sys.exit(main())
