#!/usr/bin/env python3
"""Drive the Astra terminal from outside and judge what it put on the screen.

Everything below the terminal has a gate; the terminal itself had none, and
the gap cost a session. A refused input syscall made the shell yield forever:
no fault, no message, nothing on the serial stream -- the boot log ends at
`stage 8` whether the terminal works or not, so every existing check passed
while nothing responded to a key.

This types into the machine over QMP and reads the character plane back out of
VEGA's POST text window, which is the only place the terminal's output is
observable from outside. It asserts two things the serial stream cannot show:

  * the input queue drains. A count left in the Vesta FIFO means nobody is
    consuming keys, which is what a silently refused ASTRA_SYSCALL_INPUT_READ_TRY
    looks like from here.
  * a file written through the shell can be listed and read back.

The image is copied first, so a run neither depends on nor disturbs the state
of the one it was given.
"""

import argparse
import json
import os
import queue
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import astra_image

# VEGA_POST_TEXT_BASE and its geometry, from sw/include/vega.h. The terminal
# writes cells here through ASTRA_SYSCALL_CONSOLE_WRITE.
PLANE = 0xFFF22000
PLANE_COLUMNS = 90
PLANE_ROWS = 30
PLANE_BYTES = 4096
CURSOR_OFFSET = PLANE_BYTES - 8
CURSOR_MAGIC = b"ACUR"
CURSOR_VISIBLE = 1

# Vesta input block; the low byte of the status word is the queued count.
INPUT_STATUS = 0xFFF0070C
INPUT_COUNT_MASK = 0xFF

BOOT_MARKER = "stage 8"

# The empty prompt this fixture's shell settles on between commands. It is
# `shell.assign + ":" + shell.directory + "> "` from console_shell.c's
# `prompt()`, and this script never types `cd` or switches assigns. screen()
# strips the trailing separator, so the visible empty row is exactly this.
PROMPT = "WORK:>"
PROMPT_CURSOR_COLUMN = len(PROMPT) + 1

# Typed in order. Each line is followed by Enter, then the screen is polled
# until `expect` appears, so the check waits on the machine rather than on a
# guessed settle time -- the emulated CPU is slow enough that a fixed sleep is
# either flaky or wasteful.
SCRIPT = [
    ("mkdir proto", "proto"),
    ("write hello.txt via the protocol", "hello.txt"),
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
    # And the refusal, which is the one that pays for the design: the fifth
    # line typed was `write events:no no`, and its story holds both the
    # line the shell accepted and the refusal it answered with -- neither of
    # which passed an activity to the other.
    ("cat events:activity/00000005", "command refused"),
    # The command: the same store, the last screen of it, and two dimensions at
    # once -- which is the one thing a path could not say until subsystem/ grew
    # levels under it.
    ("events", "namespace bound"),
    ("events --subsystem shell --level warning", "command refused"),
    # A program. `status` is a file in COMMANDS: that the gate installed, not
    # anything the ROM carries: this is the first line in this script whose
    # answer required loading an ELF off the volume and running it.
    #
    # It prints nothing, on purpose -- what is on the screen is the shell
    # reporting what the child exited with, so the number proves the argument
    # vector arrived and the status came back through the wait.
    ("status 7", "exited 7"),
    # No argument is zero, which is what a program that did what it was asked
    # says. It also proves argc reaches the child correctly rather than the
    # child reading whatever was after the vector.
    ("status", "exited 0"),
    # COMMANDS: is bound and searched, so a bare name resolves there without
    # the person naming it -- and naming it explicitly is the same file.
    ("commands:status 3", "exited 3"),
    # Commands can be loaded but not rewritten by the terminal process.
    ("write commands:status no", "access denied"),
    # Both read-only members remain visible and ordered: the fixture's shadow
    # is member 0, while a shipped command found only in member 1 proves the
    # union did not collapse while crossing into the terminal process.
    ("ls commands:", ("devices  [0]", "status  [1]")),
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
    # The namespace printed back, and the whole of the order a lookup uses.
    # Before this existed the search order was a comment in one function.
    ("assign", "local/commands"),
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
    # Last on purpose. `run()` reads the *current* screen after SCRIPT finishes
    # to check that this line's own output and the shell's report of its exit
    # are still on it and in order (see the child-order check below) -- the
    # terminal is 30 rows and five new commands' worth of listings sit between
    # here and where this line used to be, which was enough to scroll that
    # output off before the check ever read it. Run last, its output is what
    # the check finds.
    ("events --boot -1", "namespace bound"),
]

PERFORMANCE_SCRIPT = [
    ("status 7", "exited 7"),
    ("status", "exited 0"),
    ("commands:status 3", "exited 3"),
    ("which status", "/commands/status [1]"),
    ("which devices", "/local/commands/devices [0]"),
    ("devices status", "/commands/status [1]"),
]
PERFORMANCE_BUDGET_SECONDS = {
    "status 7": 6.0,
    "status": 4.5,
    "commands:status 3": 4.5,
    "which status": 8.0,
    "which devices": 7.0,
    "devices status": 7.0,
}

# The exit-order check near the end of run() rereads the *current* screen
# after SCRIPT finishes rather than capturing output as it goes, so this
# line's output -- and the shell's report of its exit, which the check also
# needs -- is only guaranteed to still be among the 30 rendered rows while
# this is the last thing SCRIPT types. Anything appended after it pushes
# both off the top before that check runs, which is the exact scroll-
# dependent flake that put this line here in the first place (see the
# comment above it). This assertion turns a future mistake like that into an
# immediate, named failure instead of a mysterious one at the bottom of a
# run.
assert SCRIPT[-1] == ("events --boot -1", "namespace bound"), (
    "SCRIPT's last entry must stay (\"events --boot -1\", \"namespace "
    "bound\") -- the exit-order check in run() rereads the rendered screen "
    "right after SCRIPT finishes and needs this command's own output and "
    "the shell's exit report for it to both still be on screen, which is "
    "only true while this line is typed last")

QCODE = {" ": "spc", "\n": "ret", "/": "slash", ".": "dot", "-": "minus"}
# Keys that need a modifier held. Assign names are case-insensitive, so a
# colon is the only shifted character the script needs.
SHIFTED = {":": "semicolon"}


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
            elif character.isalnum():
                self.key(character)
            else:
                raise RuntimeError("no qcode for %r" % character)


# The kernel trace ring, at the fixed address the loader retains it at. It is
# RAM and survives whatever killed the thing under test, which is the point:
# when a gate step fails the interesting evidence is usually what the kernel
# was doing just before, and re-running to get it changes the timing that
# produced it.
RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000


def dump_ring(machine, records=40):
    """Prints the tail of the kernel trace ring, best effort."""
    try:
        path = os.path.join(tempfile.mkdtemp(prefix="astra-ring-"), "ring.bin")
        reply = machine.qmp.monitor('pmemsave 0x%08x %d "%s"'
                                    % (RING_ADDRESS, RING_SIZE, path))
        if reply and reply.strip():
            print("    (ring unavailable: %s)" % reply.strip())
            return
        sys.path.insert(0, os.path.join(ROOT, "tools"))
        import trace_decode
        names = trace_decode.kernel_event_names(
            os.path.join(ROOT, "sw/kernel/trace.h"))
        with open(path, "rb") as handle:
            _, lines = trace_decode.decode(handle.read(), {}, names)
    except Exception as error:                       # noqa: BLE001
        print("    (ring unavailable: %s)" % error)
        return
    quarantines = [line for line in lines if "quarantine" in line]
    print("    --- kernel ring, last %d of %d records ---"
          % (min(records, len(lines)), len(lines)))
    for line in lines[-records:]:
        print("    %s" % line)
    if quarantines:
        print("    --- every quarantine in this boot ---")
        for line in quarantines:
            print("    %s" % line)
    else:
        print("    --- no quarantine records in this boot ---")


class Machine:
    def __init__(self, qemu, rom, image, socket_directory,
                 keyboard_evdev=None, pointer_evdev=None, release_io=False):
        self.qmp_path = os.path.join(socket_directory, "terminal-qmp.sock")
        self.text_path = os.path.join(socket_directory, "post-text.bin")
        command = [qemu, "-M", "astra68", "-m", "128M", "-bios", rom]
        command.extend((["-nographic", "-monitor", "none", "-serial", "none"]
                        if release_io else
                        ["-display", "none", "-monitor", "none",
                         "-serial", "stdio"]))
        command.extend(["-no-reboot",
                   "-qmp", "unix:%s,server=on,wait=off" % self.qmp_path,
                   "-drive", "if=none,format=raw,file=%s" % image])
        for identifier, device in (("astra-keyboard", keyboard_evdev),
                                   ("astra-pointer", pointer_evdev)):
            if device:
                command.extend([
                    "-object", "input-linux,id=%s,evdev=%s,repeat=off" %
                    (identifier, device)])
        environment = os.environ.copy()
        environment["ASTRA_TEXT_PLANE_PATH"] = self.text_path
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

    def word(self, address):
        for token in self.qmp.monitor("xp /1xw 0x%x" % address).split():
            if token.startswith("0x") and len(token) == 10:
                return int(token, 16)
        raise RuntimeError("no word read back from 0x%x" % address)

    def screen(self):
        """The character plane as a list of rows."""
        dump = self.qmp.monitor("xp /%dxb 0x%x" % (PLANE_COLUMNS * PLANE_ROWS,
                                                   PLANE))
        cells = [int(token, 16) for token in dump.split()
                 if token.startswith("0x") and len(token) == 4]
        if len(cells) != PLANE_COLUMNS * PLANE_ROWS:
            raise RuntimeError("short plane read: %d cells" % len(cells))
        rows = []
        for row in range(PLANE_ROWS):
            line = cells[row * PLANE_COLUMNS:(row + 1) * PLANE_COLUMNS]
            rows.append("".join(chr(value) if 32 <= value < 127 else " "
                                for value in line).rstrip())
        return rows

    def shared_screen(self):
        """The file-backed plane as rows, read twice to avoid a torn paint."""
        for _ in range(4):
            with open(self.text_path, "rb") as handle:
                first = handle.read(PLANE_COLUMNS * PLANE_ROWS)
                handle.seek(0)
                second = handle.read(PLANE_COLUMNS * PLANE_ROWS)
            if first == second:
                break
        if len(second) != PLANE_COLUMNS * PLANE_ROWS:
            raise RuntimeError("short shared plane read: %d cells" %
                               len(second))
        return [
            "".join(chr(value) if 32 <= value < 127 else " "
                    for value in second[row * PLANE_COLUMNS:
                                        (row + 1) * PLANE_COLUMNS]).rstrip()
            for row in range(PLANE_ROWS)
        ]

    def shared_cursor(self):
        """The stable renderer cursor from the end of the shared page."""
        for _ in range(4):
            with open(self.text_path, "rb") as handle:
                handle.seek(CURSOR_OFFSET)
                first = handle.read(8)
                handle.seek(CURSOR_OFFSET)
                second = handle.read(8)
            if first == second and len(second) == 8 and not second[7] & 1:
                if second[:4] != CURSOR_MAGIC or not second[6] & CURSOR_VISIBLE:
                    return None
                row, column = second[4], second[5]
                if row >= PLANE_ROWS or column > PLANE_COLUMNS:
                    raise RuntimeError("invalid shared cursor: %d,%d" %
                                       (row, column))
                cell = min(row * PLANE_COLUMNS + column,
                           PLANE_COLUMNS * PLANE_ROWS - 1)
                return divmod(cell, PLANE_COLUMNS)
        raise RuntimeError("torn shared cursor")

    def wait_for_screen(self, text, deadline):
        """Waits until `text` is on the plane. `text` may also be a sequence
        of strings, in which case this waits until every one of them is on
        the plane -- not necessarily the same row -- which is how one `ls`
        listing can carry more than one assertion instead of a second
        command being typed just to check the rest of the first one's
        output."""
        texts = (text,) if isinstance(text, str) else tuple(text)
        end = time.monotonic() + deadline
        while True:
            rows = self.screen()
            if all(any(needle in row for row in rows) for needle in texts):
                return rows
            if time.monotonic() >= end:
                return None
            time.sleep(0.25)

    def wait_for_prompt(self, deadline):
        """Waits for the shell to be genuinely done with the command it was
        last given, rather than for some prompt to merely be visible.

        A row *containing* the prompt is not a safe signal: the row a command
        was typed into still starts with the prompt text once it is history
        on screen, and it sits there for as long as anything typed after it
        does not scroll it away. A substring check would call that "ready"
        the instant it was typed, which is the same race this exists to
        close -- the harness would go on typing while the shell was still
        loading an image for the command that row belongs to.
        What is unique to a prompt nobody has answered yet is that its row is
        *exactly* the prompt and nothing else: every prompt on screen that
        already has a command's text following it got that text appended the
        moment somebody (this script) started typing into it, so only the one
        the shell just printed, fresh, is bare. That is what this polls for.
        """
        end = time.monotonic() + deadline
        while True:
            if any(row == PROMPT for row in self.screen()):
                return True
            if time.monotonic() >= end:
                return False
            time.sleep(0.05)

    def wait_for_cursor(self, expected, deadline):
        end = time.monotonic() + deadline
        while True:
            if self.shared_cursor() == expected:
                return True
            if time.monotonic() >= end:
                return False
            time.sleep(0.05)

    def close(self):
        try:
            self.qmp.execute("quit")
        except (BrokenPipeError, EOFError, OSError, RuntimeError):
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()


def recycle_sessions(machine, deadline):
    machine.qmp.type_line("clear")
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        if [row for row in machine.screen() if row] == [PROMPT]:
            break
        time.sleep(0.05)
    else:
        return 0
    for iteration in range(6):
        rows = machine.screen()
        answers = sum("/commands/status [1]" in row for row in rows)
        exits = sum("which: exited 0" in row for row in rows)
        machine.qmp.type_line("which status")
        end = time.monotonic() + deadline
        while time.monotonic() < end:
            rows = machine.screen()
            nonempty = [row for row in rows if row]
            if (sum("/commands/status [1]" in row for row in rows) >
                    answers and
                    sum("which: exited 0" in row for row in rows) > exits and
                    nonempty and nonempty[-1] == PROMPT):
                break
            time.sleep(0.05)
        else:
            return iteration + 1
    return -1


def run(qemu, rom, image, catalog, boot_deadline, command_deadline, verbose,
        report_timings, prepared_image, performance_only, keyboard_evdev,
        pointer_evdev, startup_soak, release_io, session_only):
    command_timings = []
    with tempfile.TemporaryDirectory(prefix="astra-terminal-") as temporary:
        scratch = os.path.join(temporary, "card.img")
        shutil.copyfile(image, scratch)
        # Into the copy, so the image this gate was pointed at is untouched.
        if not prepared_image:
            astra_image.install(scratch, catalog)
        if not performance_only:
            warmup_dir = os.path.join(temporary, "warmup")
            os.mkdir(warmup_dir)
            warmup = Machine(qemu, rom, scratch, warmup_dir)
            try:
                if not warmup.wait_for_serial(BOOT_MARKER, boot_deadline):
                    print("FAIL: durability warmup never reached the terminal")
                    return 1
                if warmup.wait_for_screen("Astra 68",
                                          command_deadline) is None:
                    print("FAIL: durability warmup drew no terminal banner")
                    return 1
                for line in ("mkdir priorboot", "events --all"):
                    warmup.qmp.type_line(line)
                    if not warmup.wait_for_prompt(command_deadline):
                        print("FAIL: durability warmup command %r produced no "
                              "prompt" % line)
                        return 1
            finally:
                warmup.close()

        run_dir = os.path.join(temporary, "run")
        os.mkdir(run_dir)
        machine = Machine(qemu, rom, scratch, run_dir, keyboard_evdev,
                          pointer_evdev, release_io)
        try:
            if not machine.wait_for_serial(BOOT_MARKER, boot_deadline):
                print("FAIL: never reached the terminal; last serial lines:")
                for line in machine.log[-5:]:
                    print("    %s" % line)
                return 1
            # The banner, not the prompt: the first paint shares the character
            # plane with the kernel's own boot console and comes out mangled
            # from "commands:" down. It is cosmetic, it predates this check,
            # and any redraw corrects it -- so the gate waits on the one line
            # that is reliably there and judges the prompt after a command,
            # where it renders correctly.
            if machine.wait_for_screen("Astra 68", command_deadline) is None:
                print("FAIL: the terminal drew no banner")
                return 1
            if machine.shared_screen() != machine.screen():
                print("FAIL: the shared text plane differs from guest memory")
                return 1

            if startup_soak:
                end = time.monotonic() + startup_soak
                while time.monotonic() < end:
                    if machine.process.poll() is not None:
                        print("FAIL: QEMU exited during startup soak")
                        return 1
                    rows = machine.screen()
                    if any("SYSTEM HALTED" in row for row in rows):
                        print("FAIL: guest halted during startup soak")
                        for row in rows:
                            if row:
                                print("    |%s|" % row)
                        return 1
                    time.sleep(0.1)
                print("ASTRA TERMINAL STARTUP SOAK PASS")
                return 0

            # The underline follows the editor insertion point, including
            # movement that changes no cell bytes, and sits after the prompt's
            # one intentional separator when the line is empty.
            machine.qmp.key("a")
            rows = machine.wait_for_screen(PROMPT + " a", command_deadline)
            if rows is None:
                print("FAIL: cursor probe character was not drawn")
                return 1
            cursor_row = rows.index(PROMPT + " a")
            for key, column in (("left", PROMPT_CURSOR_COLUMN),
                                ("right", PROMPT_CURSOR_COLUMN + 1)):
                machine.qmp.key(key)
                if not machine.wait_for_cursor((cursor_row, column),
                                               command_deadline):
                    print("FAIL: cursor did not follow editor key %r" % key)
                    return 1
            machine.qmp.key("backspace")
            if (not machine.wait_for_prompt(command_deadline) or
                    not machine.wait_for_cursor(
                        (cursor_row, PROMPT_CURSOR_COLUMN),
                        command_deadline)):
                print("FAIL: cursor did not return after the prompt separator")
                return 1

            if session_only:
                failed = recycle_sessions(machine, command_deadline)
                if failed != -1:
                    print("FAIL: session-only iteration %d" % failed)
                    for row in machine.screen():
                        if row:
                            print("    |%s|" % row)
                    return 1
                print("ASTRA TERMINAL SESSION RECYCLE PASS")
                return 0

            steps = PERFORMANCE_SCRIPT if performance_only else SCRIPT
            for line, expected in steps:
                machine.qmp.type_text(line)
                if line == "status 7":
                    typed = PROMPT + " " + line
                    rows = machine.wait_for_screen(typed, command_deadline)
                    if rows is None:
                        print("FAIL: acceptance probe line was not drawn")
                        return 1
                    command_row = max(index for index, row in enumerate(rows)
                                      if row == typed)
                    started = time.monotonic()
                    machine.qmp.key("ret")
                    accepted_row = min(command_row + 1, PLANE_ROWS - 1)
                    if not machine.wait_for_cursor((accepted_row, 0),
                                                   0.25) and not \
                            machine.wait_for_prompt(command_deadline):
                        print("FAIL: Enter was not painted before dispatch")
                        return 1
                else:
                    started = time.monotonic()
                    machine.qmp.key("ret")
                # `expected` is None for a step with nothing of its own to
                # wait for -- see the "write commands:brandnew text" entry in
                # SCRIPT for why one exists -- in which case back pressure
                # below is the whole of this step's check.
                if expected is not None:
                    rows = machine.wait_for_screen(expected, command_deadline)
                    if rows is None:
                        print("FAIL: %r produced no %r within %.0fs"
                              % (line, expected, command_deadline))
                        for row in machine.screen():
                            if row:
                                print("    |%s|" % row)
                        dump_ring(machine)
                        return 1
                    if verbose:
                        print("ok: %r -> %r" % (line, expected))
                # Back pressure. `expected` appearing is not the same as the
                # shell being ready for what gets typed next: for a command
                # that launches a program, `expected` is often the program's
                # own output, printed while the shell is still waiting on it
                # -- reading a program image off the volume does not pump
                # input, so keys arriving before the shell is truly back at
                # its prompt queue up behind it instead of being consumed as
                # they arrive, which is the window a stress run reproduces as
                # a corrupted next line and a step that times out with no
                # explanation. Waiting for the bare prompt closes that window
                # by construction: it cannot appear until the shell has come
                # all the way back.
                if not machine.wait_for_prompt(command_deadline):
                    print("FAIL: %r never brought the shell back to %r "
                          "within %.0fs" % (line, PROMPT, command_deadline))
                    for row in machine.screen():
                        if row:
                            print("    |%s|" % row)
                    dump_ring(machine)
                    return 1
                elapsed = time.monotonic() - started
                command_timings.append((line, elapsed))
                if report_timings:
                    print("timing: %.3fs %s" % (elapsed, line))
                if (performance_only and
                        elapsed > PERFORMANCE_BUDGET_SECONDS[line]):
                    print("FAIL: %r took %.3fs, budget %.3fs" %
                          (line, elapsed,
                           PERFORMANCE_BUDGET_SECONDS[line]))
                    return 1
                rows = machine.screen()
                prompt_rows = [index for index, row in enumerate(rows)
                               if row == PROMPT]
                expected_cursor = ((prompt_rows[-1], PROMPT_CURSOR_COLUMN)
                                   if prompt_rows else None)
                if not machine.wait_for_cursor(expected_cursor,
                                               command_deadline):
                    print("FAIL: cursor is not after the live prompt")
                    return 1

            if performance_only:
                elapsed = [value for _, value in command_timings]
                print("ASTRA TERMINAL PERFORMANCE PASS")
                print("timing: p50 %.3fs max %.3fs commands %d"
                      % (statistics.median(elapsed), max(elapsed),
                         len(elapsed)))
                return 0

            # And no endpoint is quarantined.
            #
            # These three flags are sticky: an endpoint carrying one answers
            # every read with it until something recovers the endpoint, so a
            # single one here means a device is gone for the rest of the boot.
            # A storm quarantine on healthy storage is what made this gate fail
            # about half its runs, from a burst of transfers that were all
            # being serviced correctly -- so the table is read rather than only
            # rendered.
            rows = machine.screen()
            stuck = [row for row in rows
                     if any(flag in row for flag in
                            ("storm", "device-error", "overflow"))]
            if stuck:
                print("FAIL: an endpoint is quarantined")
                for row in stuck:
                    print("    |%s|" % row)
                dump_ring(machine)
                return 1
            if verbose:
                print("ok: no endpoint is quarantined")

            # What a program said, before what this shell says about it.
            #
            # Two separate failures hide here and both were real. A launched
            # program writes through the sink, which reaches the cell model but
            # is painted only by the flush at the bottom of the shell's pump --
            # and that pump used to return early on any pass with no keystroke,
            # so a program that printed one line and exited printed nothing at
            # all until somebody pressed a key. Waiting for the line above
            # catches that.
            #
            # The order catches the other. A child's exit is noticed while its
            # last words are still queued on the sink, so a launcher that
            # reported the exit without draining first printed "exited 13"
            # above the line that says what 13 meant. Both lines are on screen
            # either way, which is why this asserts on their order and not on
            # their presence.
            rows = machine.screen()
            said = next((index for index, row in enumerate(rows)
                         if "namespace bound" in row), None)
            exited = next((index for index, row in enumerate(rows)
                           if "events: exited 0" in row), None)
            if said is None or exited is None or said >= exited:
                print("FAIL: the child's line and the shell's report of its "
                      "exit are out of order (line at %r, exit at %r)"
                      % (said, exited))
                for row in rows:
                    if row:
                        print("    |%s|" % row)
                return 1
            if verbose:
                print("ok: the child's last line precedes its exit report")

            # A control capability, not a write to EVENTS:. The command asks
            # the protected events service, which forwards to the runtime that
            # owns the current call-site thresholds.
            machine.qmp.type_line("events --level-set shell warning")
            if machine.wait_for_screen(
                    "temporary level set for this boot",
                    command_deadline) is None or not machine.wait_for_prompt(
                        command_deadline):
                print("FAIL: event level control did not complete")
                for row in machine.screen():
                    if row:
                        print("    |%s|" % row)
                dump_ring(machine)
                return 1

            # A live tail, and the way out of it. Not in SCRIPT because
            # ending it is a bare return rather than a command: `events` is a
            # program now and what it reads is STDIN, which is lines -- so the
            # way out is an empty line, and the shell hands the child the
            # newline that was pressed rather than swallowing it.
            for line, expected in (("events --follow", "-- following"),
                                   (None, "WORK:>")):
                if line is not None:
                    machine.qmp.type_line(line)
                else:
                    machine.qmp.key("ret")
                if machine.wait_for_screen(expected, command_deadline) is None:
                    print("FAIL: the follow produced no %r within %.0fs"
                          % (expected, command_deadline))
                    for row in machine.screen():
                        if row:
                            print("    |%s|" % row)
                    dump_ring(machine)
                    return 1
                if verbose:
                    print("ok: follow -> %r" % expected)

            # And no input overflowed.
            #
            # The emulator's input queue is 32 events deep and drops silently
            # at the hardware level when it is full -- nothing above it in the
            # kernel or the model can undo a drop once it happens, so this
            # gate cannot see it through the screen the way it sees everything
            # else. It can see the shell's account of it: `pump_once` now
            # reports the overflow flag the kernel already surfaced as a
            # warning-level shell event, so this reads that back rather than
            # trusting that this run's timing happened to stay under 32 --
            # the same reasoning as the quarantine check above, and it must
            # be typed fresh: the last time this subsystem's warnings were on
            # screen was SCRIPT's own check of them, long since scrolled off.
            machine.qmp.type_line("events --subsystem shell --level warning")
            if not machine.wait_for_prompt(command_deadline):
                print("FAIL: the overflow check itself produced no prompt "
                      "within %.0fs" % command_deadline)
                for row in machine.screen():
                    if row:
                        print("    |%s|" % row)
                dump_ring(machine)
                return 1
            overflowed = [row for row in machine.screen()
                          if "input overflowed" in row]
            if overflowed:
                print("FAIL: input overflowed during the run")
                for row in overflowed:
                    print("    |%s|" % row)
                dump_ring(machine)
                return 1
            if verbose:
                print("ok: no input overflowed")

            # A count left here means keys are arriving and nobody is taking
            # them, which is how a refused input syscall presents.
            queued = machine.word(INPUT_STATUS) & INPUT_COUNT_MASK
            if queued != 0:
                print("FAIL: %d input events left unconsumed" % queued)
                return 1

            # More short-lived clients than the service has session slots.
            # Each command must send BYE before its process exits; otherwise
            # this deterministically exhausts the eight-slot table.
            failed = recycle_sessions(machine, command_deadline)
            if failed != -1:
                print("FAIL: short-lived VFS sessions were not recycled at "
                      "iteration %d" % failed)
                for row in machine.screen():
                    if row:
                        print("    |%s|" % row)
                dump_ring(machine)
                return 1
            if verbose:
                print("ok: short-lived VFS sessions are recycled")
        finally:
            machine.close()
    print("ASTRA TERMINAL PASS")
    if report_timings:
        elapsed = [value for _, value in command_timings]
        print("timing: p50 %.3fs max %.3fs commands %d"
              % (statistics.median(elapsed), max(elapsed), len(elapsed)))
    return 0


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
    parser.add_argument("--report-timings", action="store_true",
                        help="print Enter-to-next-prompt command latency")
    parser.add_argument("--prepared-image", action="store_true",
                        help="use an image that already contains the fixture")
    parser.add_argument("--performance-only", action="store_true",
                        help="run the Arty command-latency budget gate")
    parser.add_argument("--keyboard-evdev",
                        help="attach the release keyboard input-linux object")
    parser.add_argument("--pointer-evdev",
                        help="attach the release pointer input-linux object")
    parser.add_argument("--startup-soak", type=float, default=0.0,
                        help="require a live prompt for this many seconds")
    parser.add_argument("--release-io", action="store_true",
                        help="use the release launcher's nographic I/O flags")
    parser.add_argument("--session-only", action="store_true",
                        help="stress short-lived VFS session recycling")
    arguments = parser.parse_args()
    return run(arguments.qemu, arguments.rom, arguments.image,
               arguments.catalog, arguments.boot_deadline,
               arguments.command_deadline, arguments.verbose,
               arguments.report_timings, arguments.prepared_image,
               arguments.performance_only, arguments.keyboard_evdev,
               arguments.pointer_evdev, arguments.startup_soak,
               arguments.release_io, arguments.session_only)


if __name__ == "__main__":
    sys.exit(main())
