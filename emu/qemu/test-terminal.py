#!/usr/bin/env python3
"""Drive the Astra shell from outside and judge what it answered.

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
trace ring -- `astra_terminal_set_echo`, installed by the shell -- and this
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

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import astra_image

# RAM the gate boots with. A module global so --memory can prove that the
# machine works at a size other than the one it was written against.
MEMORY = "128M"
import trace_decode

# Vesta input block; the low byte of the status word is the queued count.
INPUT_STATUS = 0xFFF0070C
INPUT_COUNT_MASK = 0xFF
RTC_STATUS = 0xFFF00420
RTC_VALID = 1 << 0

# The kernel trace ring, at the fixed address the loader retains it at.
RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000

BOOT_MARKER = "stage 8"

# The desktop's Terminal icon. A double click here is what starts a terminal;
# there is no manifest entry that starts one directly, because a window client
# with no window server to ask is a program that exits before it draws.
TERMINAL_ICON = (70, 90)

# The prompt the shell settles on. It is `shell.assign + ":" + directory + "> "`
# from console_shell.c's `prompt()`. It reaches the ring attached to whatever
# was typed after it, because a prompt carries no newline of its own -- so this
# is a substring to look for, never a whole line to match.
PROMPT = "WORK:>"

# One line of the shell's banner, which is the first thing a live terminal
# says. Waiting for it is waiting for the window to exist and the shell inside
# it to have run.
BANNER = "COMMANDS"
POSIX_COMMAND = 'posix -R +42 --cmd "set number" -- WORK:notes.txt'

SCRIPT = [
    # `mkdir` is silent on success. The diagnostic event is the completion
    # barrier; `ls` below proves the directory was actually created.
    ("mkdir proto", "finished with status 0"),
    ("write hello.txt via the protocol", "hello.txt"),
    ("ls", "proto/"),
    # `cd` and then bare names, which is the whole reason a launched program
    # is granted CWD:. The machine has no root and no current directory --
    # a path is ASSIGN:path -- so `mkdir inner` typed here names nothing at
    # all unless the shell has told the child where the prompt is standing.
    # While these were builtins the shell qualified for them and the question
    # never came up; the first thing that would have broken on the day they
    # became programs is exactly this.
    #
    # `cd` says nothing when it works, so its own line is only the line coming
    # back. `pwd` and the four after it are what prove it moved.
    ("cd proto", "cd proto"),
    ("pwd", "WORK:proto"),
    ("mkdir inner", "mkdir inner"),
    ("ls", "inner/"),
    ("write scratch.txt hi", "scratch.txt"),
    ("cat scratch.txt", "hi"),
    ("rm scratch.txt", "rm scratch.txt"),
    ("echo $?", "0"),
    # And back, by the same door: `cd` with no name is the assign's root, and
    # `proto/` is only in the root -- so this is the check that it returned
    # rather than the check that it is somewhere.
    ("cd", "cd"),
    ("ls", "proto/"),
    ("cat hello.txt", "via the protocol"),
    # The terminal is a least-authority process: WORK: is writable, while the
    # event tree it can inspect is read-only. The refusal comes from the assign
    # rather than from the service behind it.
    ("write events:no no", "access denied"),
    # EVENTS: is a tree, and that is the whole claim: no bespoke client, no
    # query language, just a path. `ls` walks it and `cat` reads it.
    ("ls events:", "activity/"),
    ("ls events:boot/current", "earliest"),
    # One command is one story, read back on the machine that recorded it. The
    # first line typed at this prompt begins activity 1 -- the kernel's counter
    # starts there every boot -- and that line was `mkdir proto`, so this is
    # the shell's own account of the first thing this test did.
    ("cat events:activity/00000001", "command accepted"),
    # And the refusal, which is the one that pays for the design: its story
    # holds both the line the shell accepted and the refusal it answered
    # with, and neither passed an activity to the other. The name is the
    # activity in hex, activities are numbered from one in the order the shell
    # accepted them, and `write events:no no` is the fifteenth line typed --
    # so 0x0f. Inserting a line above it moves this, which is what the
    # assertions below the script turn into a named failure. Every `echo $?`
    # is a line too: asking for the status costs an activity.
    ("cat events:activity/0000000f", "command refused"),
    # The command: the same store, the last screen of it, and two dimensions at
    # once -- which is the one thing a path could not say until subsystem/ grew
    # levels under it.
    # The bare command shows the last screen of the store, so what it is
    # asserted on has to be recent: this is the shell's own record of having
    # launched a program, which is a notice and therefore absent from the
    # level-filtered listing below. `namespace bound` used to be the needle
    # here and is not in this window on a seven-process boot -- it is the
    # first thing the machine recorded, and the last line of this script is
    # where a whole boot gets read back.
    ("events", "launching from place"),
    ("events --subsystem shell --level warning", "command refused"),
    # A program. `status` is a file in COMMANDS: that the gate installed, not
    # anything the ROM carries: this is the first line in this script whose
    # answer required loading an ELF off the volume and running it.
    #
    # It prints nothing, on purpose -- what is on the screen is the shell
    # reporting what the child exited with, so the number proves the argument
    # vector arrived and the status came back through the wait.
    ("status 7", "status 7"),
    ("echo $?", "7"),
    # No argument is zero, which is what a program that did what it was asked
    # says. It also proves argc reaches the child correctly rather than the
    # child reading whatever was after the vector.
    ("status", "status"),
    # COMMANDS: is bound and searched, so a bare name resolves there without
    # the person naming it -- and naming it explicitly is the same file.
    ("commands:status 3", "commands:status 3"),
    ("echo $?", "3"),
    # Commands can be loaded but not rewritten by the terminal process.
    ("write commands:status no", "access denied"),
    # Both members remain visible: `devices` is the fixture's own
    # shadow copy and `which` is only on the shipped member, so a listing
    # holding them both is a union that did not collapse while crossing into
    # the terminal process.
    #
    # The tag is part of the union contract: duplicates are intentionally
    # visible, and the member says which binding supplied each row.
    ("ls commands:", ("devices  [0]", "devices  [1]", "which  [1]")),
    # And the milder failure the same conflation caused in `rm`: a name on
    # no member at all must be reported "not found", not "access denied" --
    # a member refusing on rights alone has said nothing about whether the
    # name is there, and mistaking that refusal for the answer is exactly
    # the bug the two lines above also guard against.
    ("rm commands:doesnotexist", "not found"),
    # And the one that used to be a hang waiting to happen: a name that is not
    # a builtin and is not a file. Two places are looked in, both top level
    # only, and then it says so.
    ("nosuchthing", "not a command"),
    # 127 is what every shell answers for a name it could not find, and
    # the only way to see it is `$?` -- the shell prints no status now.
    ("echo $?", "127"),
    # The namespace printed back, and the whole of the order a lookup uses.
    # Before this existed the search order was a comment in one function.
    ("assign", ("local/commands", "LIBS: [0] r  /libs")),
    # A child resolving through a union it was granted. `which` holds COMMANDS:
    # as two grants with two roots and loops them with the same Kit function
    # the shell uses -- so this line is the roots-in-grants fix and the union
    # crossing a process boundary at once. `status` is only on the shipped
    # member, so member 1 is the honest answer.
    ("which status", "/commands/status [1]"),
    # And the shadowing. The gate installed a `devices` into the writable
    # member, so the person's own copy is what a lookup finds -- member 0,
    # ahead of the shipped one.
    ("which devices", "/local/commands/devices [0]"),
    # Which is also what runs: the shadowing copy is `which`'s image under
    # another name, so it answers the way `which` does rather than the way the
    # shipped `devices` does.
    ("devices status", "/commands/status [1]"),
    # PROC: is the whole running machine, not only boot services. It includes
    # init, the desktop, this dynamically launched Terminal, and ps itself.
    ("ps", ("ROM:supervisor", "SERVICES:desktop", "APPS:Terminal.app",
            "COMMANDS:ps")),
    # This used to have to be last, because the check that followed SCRIPT
    # reread the rendered screen and needed this line's output to still be on
    # it -- thirty rows of terminal, and five commands' worth of listings were
    # enough to scroll it away before the check ran. The ring is cumulative
    # and each command is judged against the records its own Enter produced,
    # so nothing here depends on its position any more.
    ("events --boot -1", "namespace bound"),
    # The POSIX file and directory layer, checked against itself: open, read,
    # write, lseek, stat, fstat, opendir, readdir, mkdir, unlink, chdir,
    # getcwd, and a heap. It reports by exit status rather than by text, so a
    # zero here is thirty-odd separate checks and a non-zero one names the step
    # -- see the enum at the top of `commands/posix/posix.c`.
    (POSIX_COMMAND, "POSIX RAW PASS"),
    ("echo $?", "0"),
    # Stock Lua, including the shared POSIX paths it depends on rather than a
    # command-private compatibility layer.
    ("lua -v", "Lua 5.5.1"),
    ("lua -e \"print(6*7)\"", "42"),
    # The expected fraction is absent from the echoed command, so a crash
    # cannot pass by merely rendering what was typed.
    ("lua -e \"print(1/2)\"", "0.5"),
    ("write luafile.lua \"print(6*7)\"", "luafile.lua"),
    ("lua luafile.lua", "42"),
    ("rm luafile.lua", "rm luafile.lua"),
    ("FOO=bar lua -e \"print(os.getenv('FOO'))\"", "bar"),
    ("lua -e \"local a,b,c=os.execute('status 23');print(a,b,c)\"",
     "exit"),
    ("lua -e \"print(os.date('!%Y'))\"", "2026"),
    ("lua -e \"local f=assert(io.open('luatest','w'));"
     "f:write('ok');f:close();assert(os.rename('luatest','luanew'));"
     "print(assert(io.open('luanew')):read('*a'));os.remove('luanew')\"",
     "ok"),
    # Quoting, proved by a name that could not exist without it. Two words
    # arriving as two arguments makes `two/`, and one word makes `two words/`
    # -- so the listing is the whole assertion and no message has to be
    # believed.
    ("mkdir \"two words\"", "two words"),
    ("ls", "two words/"),
    # Redirection, and the only honest way to check it: the answer must *not*
    # be on the screen after the first line -- the shell echoes what the
    # terminal renders into the ring, and a redirected child renders nothing --
    # and must be in the file on the second.
    ("which status > out.txt", "out.txt"),
    ("cat out.txt", "/commands/status [1]"),
    # `>>` keeps what is there, so both answers are in one file. A truncating
    # append would leave only the second, which is why the needle is both.
    ("which devices >> out.txt", "out.txt"),
    ("cat out.txt", ("/commands/status [1]",
                     "/local/commands/devices [0]")),
    # The two refusals. A builtin has no stream to move, and a redirect with
    # no name is not a command -- both said rather than done quietly.
    ("pwd > out.txt", "cannot be redirected"),
    ("ls >", "syntax"),
    # Names. A line that is only `NAME=VALUE` sets one in this shell; `$NAME`
    # is how it comes back, and `set` is the whole list.
    ("GREETING=hello", "GREETING=hello"),
    ("echo $GREETING", "hello"),
    ("set", "GREETING=hello"),
    # `?` is there and marked as never crossing a launch, which is the one
    # thing about it a person has to know.
    ("set", "this shell only"),
    ("set GREETING", "set GREETING"),
    # An unset name expands to nothing rather than to its own spelling, so the
    # dash is the whole of the output and proves the expansion ran.
    ("echo $GREETING-", "-"),
    # A value with a space in it survives, which is quoting and expansion in
    # one line -- and `echo` is a program, so it also proves the vector.
    ("PHRASE=\"two words\"", "PHRASE="),
    ("echo -$PHRASE-", "-two words-"),
    # And a redirect on a program whose output is a value.
    ("echo $PHRASE > phrase.txt", "phrase.txt"),
    ("cat phrase.txt", "two words"),
    # A builtin still refuses an environment, and says so rather than dropping
    # the name.
    ("A=1 pwd", "cannot be redirected or given an environment"),
]

VIM_LUA_FILE = "vim-created.lua"

# What a person waits for: Enter, to the answer on the screen.
#
# It used to time `status`, whose whole output was the shell's "exited 0" line.
# The shell does not print that any more -- a status is `$?` -- so a command
# with no output has nothing to wait for, and the baseline was reset here on
# commands that answer. The numbers below are therefore not comparable with
# the ones in HANDOVER-launch-latency.md before 2026-08-19.
PERFORMANCE_SCRIPT = [
    ("hello", "hello from picolibc"),
    ("echo one", "one"),
    ("which status", "/commands/status [1]"),
    ("which devices", "/local/commands/devices [0]"),
    ("devices status", "/commands/status [1]"),
]
PERFORMANCE_BUDGET_SECONDS = {
    "hello": 8.0,
    "echo one": 8.0,
    "which status": 8.0,
    "which devices": 7.0,
    "devices status": 7.0,
}

# The two `cat events:activity/N` lines name an activity by number, and the
# number is a position in the list above. Nothing else ties them together, so a
# line inserted before either one would send the gate looking at somebody
# else's story and report the wrong command as unrecorded.
assert SCRIPT[0][0] == "mkdir proto", (
    "SCRIPT's first line is activity 1, which "
    "(\"cat events:activity/00000001\", \"command accepted\") reads back")
assert SCRIPT[14][0] == "write events:no no", (
    "SCRIPT's fifteenth line is activity 0x0f, which "
    "(\"cat events:activity/0000000f\", \"command refused\") reads back")

QCODE = {" ": "spc", "\n": "ret", "/": "slash", ".": "dot", "-": "minus",
         "=": "equal", ",": "comma", ";": "semicolon", "'": "apostrophe"}
# Keys that need a modifier held. A capital is handled in type_text as the
# shift chord over its lowercase key; these are the punctuation that only
# exists shifted, and `+` is here because `date +FORMAT` needs it.
SHIFTED = {":": "semicolon", "+": "equal", "%": "5", "_": "minus",
           "?": "slash", "\"": "apostrophe", ">": "dot", "<": "comma",
           "$": "4", "(": "9", ")": "0", "*": "8", "!": "1"}

# A user record the decoder rendered. The body is what the shell wrote; a
# record whose text did not fit one record ends in a backslash and continues
# in the next, which is why this gate never matches against a single record.
RECORD = re.compile(
    r"^seq\s+(\d+)\s+\S+\s+[0-9a-f]{8}/\d+(?:\s+act\s+[0-9a-f]+)?\s(.*)$")


class Qmp:
    def __init__(self, path, deadline=20.0):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        end = time.monotonic() + deadline
        while True:
            try:
                self.socket.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= end:
                    raise
                time.sleep(0.05)
        self.file = self.socket.makefile("rwb", buffering=0)
        greeting = json.loads(self.file.readline())
        if "QMP" not in greeting:
            raise RuntimeError("invalid QMP greeting: %s" % greeting)
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

    def monitor(self, line):
        return self.execute("human-monitor-command", {"command-line": line})

    def send(self, down, qcode):
        self.execute("input-send-event", {"events": [
            {"type": "key",
             "data": {"down": down,
                      "key": {"type": "qcode", "data": qcode}}}]})
        time.sleep(0.02)

    def key(self, qcode):
        # Press and release: the shell acts on the press, but a key left down
        # would hold the modifier state the next press is translated against.
        for down in (True, False):
            self.send(down, qcode)

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
        self.execute("input-send-event", {"events": [
            {"type": "abs", "data": {"axis": "x", "value": x}},
            {"type": "abs", "data": {"axis": "y", "value": y}}]})
        time.sleep(0.05)

    def button(self, down):
        self.execute("input-send-event", {"events": [
            {"type": "btn", "data": {"button": "left", "down": down}}]})
        time.sleep(0.05)

    def double_click(self, x, y):
        self.point(x, y)
        for _ in range(2):
            self.button(True)
            self.button(False)


class Machine:
    def __init__(self, qemu, rom, image, socket_directory):
        self.qmp_path = os.path.join(socket_directory, "terminal-qmp.sock")
        self.ring_path = os.path.join(socket_directory, "ring.bin")
        command = [qemu, "-M", "astra68", "-m", MEMORY, "-bios", rom,
                   "-display", "none", "-monitor", "none", "-serial", "stdio",
                   "-no-reboot",
                   "-qmp", "unix:%s,server=on,wait=off" % self.qmp_path,
                   "-drive", "if=none,format=raw,file=%s" % image]
        environment = os.environ.copy()
        private_lib = os.path.realpath(
            os.path.join(os.path.dirname(qemu), "..", "lib"))
        if os.path.isdir(private_lib):
            existing = environment.get("LD_LIBRARY_PATH")
            environment["LD_LIBRARY_PATH"] = (
                private_lib if not existing else "%s:%s" % (private_lib,
                                                            existing))
        self.process = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1, env=environment)
        self.serial = queue.Queue()
        self.log = []
        threading.Thread(target=self._pump, daemon=True).start()
        self.qmp = Qmp(self.qmp_path)
        self.names = trace_decode.kernel_event_names(
            os.path.join(ROOT, "sw/kernel/trace.h"))
        self.catalog = trace_decode.load_catalogs([
            os.path.join(ROOT, "sw/userspace", "supervisor", "build",
                         "m68k", "astra_supervisor.elf"),
            os.path.join(ROOT, "sw/userspace", "services", "terminal",
                         "build", "m68k", "terminal.elf")])

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

    def recent_serial(self):
        while True:
            try:
                line = self.serial.get_nowait()
            except queue.Empty:
                break
            if line is not None:
                self.log.append(line)
        return self.log[-20:]

    def word(self, address):
        for token in self.qmp.monitor("xp /1xw 0x%x" % address).split():
            if token.startswith("0x") and len(token) == 10:
                return int(token, 16)
        raise RuntimeError("no word read back from 0x%x" % address)

    def said(self, after=0):
        """Every line the machine has printed since sequence `after`.

        Continuations are rejoined here rather than left to the caller: a
        record holds twenty bytes, so most of what this gate looks for --
        `/commands/status [1]`, `namespace bound` -- straddles two of them, and
        a check against single records would fail on the length of its own
        needle rather than on anything the machine did.
        """
        reply = self.qmp.monitor('pmemsave 0x%08x %d "%s"'
                                 % (RING_ADDRESS, RING_SIZE, self.ring_path))
        if reply and reply.strip():
            raise RuntimeError("ring unavailable: %s" % reply.strip())
        with open(self.ring_path, "rb") as handle:
            _, rendered = trace_decode.decode(handle.read(), self.catalog,
                                               self.names)
        lines = []
        pending = ""
        highest = after
        for line in rendered:
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

    def recent_faults(self):
        reply = self.qmp.monitor('pmemsave 0x%08x %d "%s"'
                                 % (RING_ADDRESS, RING_SIZE, self.ring_path))
        if reply and reply.strip():
            raise RuntimeError("ring unavailable: %s" % reply.strip())
        with open(self.ring_path, "rb") as handle:
            _, rendered = trace_decode.decode(handle.read(), self.catalog,
                                               self.names)
        return [line for line in rendered if "fault" in line.lower()][-8:]

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
    machine.qmp.double_click(*TERMINAL_ICON)
    lines, _ = machine.wait_for_text(BANNER, command_deadline)
    if lines is None:
        print("FAIL: the terminal never drew its banner after a double click")
        return False
    return machine.settle()


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
        machine.settle()
        if any(VIM_LUA_FILE in line for line in machine.said(before)[0]):
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
    if machine.wait_for_text("print(6*7)", command_deadline, before,
                             exact=True)[0] is None:
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
    if machine.wait_for_text("finished with status 0", command_deadline,
                             before)[0] is None:
        print("FAIL: Vim did not exit cleanly after writing the Lua program")
        return False

    machine.qmp.point(1100, 200)
    machine.qmp.button(True)
    machine.qmp.button(False)
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("lua " + VIM_LUA_FILE)
    if machine.wait_for_text("42", command_deadline, before,
                             exact=True)[0] is None:
        print("FAIL: Lua did not execute the program created in Vim")
        for line in machine.said(before)[0][-80:]:
            print("    |%s|" % line)
        return False
    if verbose:
        print("ok: Vim created %s and Lua returned 42" % VIM_LUA_FILE)

    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("rm " + VIM_LUA_FILE)
    if machine.wait_for_text("rm " + VIM_LUA_FILE, command_deadline,
                             before)[0] is None:
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
        machine.qmp.type_line("mkdir priorboot")
        if machine.wait_for_text("finished with status 0", command_deadline,
                                 before)[0] is None:
            print("FAIL: the boot before the run answered no command")
            return False
        machine.settle()
        return True
    finally:
        machine.close()


def run(qemu, rom, image, catalog, boot_deadline, command_deadline, verbose,
        report_timings, prepared_image, performance_only, vim_gate,
        network_only, vim_only):
    timings = []
    with tempfile.TemporaryDirectory(prefix="astra-terminal-") as temporary:
        scratch = os.path.join(temporary, "card.img")
        shutil.copyfile(image, scratch)
        # Into the copy, so the image this gate was pointed at is untouched.
        if not prepared_image:
            astra_image.install(scratch, catalog)
        needs_warm_store = not (performance_only or network_only or vim_only)
        if needs_warm_store and not warm_the_store(
                qemu, rom, scratch, temporary, boot_deadline,
                command_deadline):
            return 1
        run_dir = os.path.join(temporary, "run")
        os.mkdir(run_dir)
        machine = Machine(qemu, rom, scratch, run_dir)
        try:
            if not open_terminal(machine, boot_deadline, command_deadline):
                return 1
            script = ([] if vim_only else
                      [(POSIX_COMMAND, "POSIX RAW PASS")] if network_only else
                      PERFORMANCE_SCRIPT if performance_only else SCRIPT)
            for line, expected in script:
                machine.settle()
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
                said, _ = machine.wait_for_text(expected, command_deadline,
                                                before,
                                                exact=line == "echo $?")
                elapsed = time.monotonic() - started
                if said is None:
                    print("FAIL: %r never answered with %r" % (line, expected))
                    for text in machine.said(before)[0][-80:]:
                        print("    |%s|" % text)
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
            machine.settle()
            before = machine.sequence()
            machine.qmp.type_line("echo $?")
            if machine.wait_for_text("0", command_deadline, before,
                                     exact=True)[0] is None:
                print("FAIL: the truncating redirect never finished")
                return 1
            machine.settle()
            before = machine.sequence()
            machine.qmp.type_line("cat out.txt")
            said, _ = machine.wait_for_text("/local/commands/devices [0]",
                                            command_deadline, before)
            if said is None:
                print("FAIL: the truncated file did not hold the new answer")
                return 1
            if any("/commands/status [1]" in line for line in said):
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
            machine.settle()
            before = machine.sequence()
            machine.qmp.type_line("assign")
            if machine.wait_for_text(PROMPT, command_deadline,
                                     before)[0] is None:
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
            for text in machine.recent_serial():
                print("    %s" % text)
            return 1
        finally:
            machine.close()


def main():
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
    parser.add_argument("--memory", default="128M",
                        help="RAM to boot with, e.g. 256M")
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
    arguments = parser.parse_args()
    global MEMORY
    MEMORY = arguments.memory
    return run(arguments.qemu, arguments.rom, arguments.image,
               arguments.catalog, arguments.boot_deadline,
               arguments.command_deadline, arguments.verbose,
               arguments.report_timings, arguments.prepared_image,
               arguments.performance_only, arguments.vim_gate,
               arguments.network_only, arguments.vim_only)


if __name__ == "__main__":
    sys.exit(main())
