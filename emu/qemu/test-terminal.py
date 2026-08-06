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
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import astra_image

# VEGA_POST_TEXT_BASE and its geometry, from sw/include/vega.h. The terminal
# writes cells here through ASTRA_SYSCALL_CONSOLE_WRITE.
PLANE = 0xFFF22000
PLANE_COLUMNS = 90
PLANE_ROWS = 30

# Vesta input block; the low byte of the status word is the queued count.
INPUT_STATUS = 0xFFF0070C
INPUT_COUNT_MASK = 0xFF

BOOT_MARKER = "stage 8"

# Typed in order. Each line is followed by Enter, then the screen is polled
# until `expect` appears, so the check waits on the machine rather than on a
# guessed settle time -- the emulated CPU is slow enough that a fixed sleep is
# either flaky or wasteful.
SCRIPT = [
    ("mkdir proto", "proto"),
    ("write hello.txt via the protocol", "hello.txt"),
    ("ls", "proto/"),
    ("cat hello.txt", "via the protocol"),
    # The namespace, end to end. The shell stands in WORK:, so the four lines
    # above are relative words inside it. SYS: is the same volume at its root
    # and shows the work directory the supervisor made, and it was granted read
    # and nothing else -- so the refusal below comes from the assign rather than
    # from the filesystem, which would have been happy to write there.
    ("ls sys:", "work/"),
    ("write sys:hello.txt no", "access denied"),
    # EVENTS: is a tree, and that is the whole claim: no bespoke client, no
    # query language, just a path. `ls` walks it and `cat` reads it.
    ("ls events:", "activity/"),
    ("ls events:boot/current", "earliest"),
    # One command is one story, read back on the machine that recorded it. The
    # first line typed at this prompt begins activity 1 -- the kernel's counter
    # starts there every boot -- and that line was `mkdir proto`, so this is
    # the shell's own account of the first thing this test did.
    ("cat events:activity/00000001", "command accepted"),
    # And the refusal, which is the one that pays for the design: the sixth
    # line typed was `write sys:hello.txt no`, and its story holds both the
    # line the shell accepted and the refusal it answered with -- neither of
    # which passed an activity to the other.
    ("cat events:activity/00000006", "command refused"),
    # The command: the same store, the last screen of it, and two dimensions at
    # once -- which is the one thing a path could not say until subsystem/ grew
    # levels under it.
    ("events", "namespace bound"),
    ("events --subsystem shell --level warning", "command refused"),
    ("events --boot -1", "the store is RAM"),
]

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
        for character in text:
            if character in QCODE:
                self.key(QCODE[character])
            elif character in SHIFTED:
                self.chord("shift", SHIFTED[character])
            elif character.isalnum():
                self.key(character)
            else:
                raise RuntimeError("no qcode for %r" % character)
        self.key("ret")


class Machine:
    def __init__(self, qemu, rom, image, socket_directory):
        self.qmp_path = os.path.join(socket_directory, "terminal-qmp.sock")
        command = [qemu, "-M", "astra68", "-m", "32M", "-bios", rom,
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

    def wait_for_screen(self, text, deadline):
        end = time.monotonic() + deadline
        while True:
            rows = self.screen()
            if any(text in row for row in rows):
                return rows
            if time.monotonic() >= end:
                return None
            time.sleep(0.25)

    def close(self):
        try:
            self.qmp.execute("quit")
        except (BrokenPipeError, EOFError, OSError, RuntimeError):
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            self.process.kill()


def run(qemu, rom, image, catalog, boot_deadline, command_deadline, verbose):
    with tempfile.TemporaryDirectory(prefix="astra-terminal-") as temporary:
        scratch = os.path.join(temporary, "card.img")
        shutil.copyfile(image, scratch)
        # Into the copy, so the image this gate was pointed at is untouched.
        astra_image.install_catalog(scratch, catalog)
        machine = Machine(qemu, rom, scratch, temporary)
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

            for line, expected in SCRIPT:
                machine.qmp.type_line(line)
                rows = machine.wait_for_screen(expected, command_deadline)
                if rows is None:
                    print("FAIL: %r produced no %r within %.0fs"
                          % (line, expected, command_deadline))
                    for row in machine.screen():
                        if row:
                            print("    |%s|" % row)
                    return 1
                if verbose:
                    print("ok: %r -> %r" % (line, expected))

            # A live tail, and the way out of it. Not in SCRIPT because ending
            # it is a keystroke rather than a line: any key ends a follow and
            # that key is consumed, so a typed line would lose its first
            # character. Enter is the key with nothing to lose.
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
                    return 1
                if verbose:
                    print("ok: follow -> %r" % expected)

            # A count left here means keys are arriving and nobody is taking
            # them, which is how a refused input syscall presents.
            queued = machine.word(INPUT_STATUS) & INPUT_COUNT_MASK
            if queued != 0:
                print("FAIL: %d input events left unconsumed" % queued)
                return 1
        finally:
            machine.close()
    print("ASTRA TERMINAL PASS")
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
    arguments = parser.parse_args()
    return run(arguments.qemu, arguments.rom, arguments.image,
               arguments.catalog, arguments.boot_deadline,
               arguments.command_deadline, arguments.verbose)


if __name__ == "__main__":
    sys.exit(main())
