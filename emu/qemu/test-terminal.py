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

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
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

# The empty prompt this fixture's shell settles on between commands. It is
# `shell.assign + ":" + shell.directory + "> "` from console_shell.c's
# `prompt()`, and this script never types `cd` or switches assigns, so it is
# always exactly this -- `screen()` below rstrips every row, which is why the
# trailing space here is not part of the constant.
PROMPT = "WORK:>"

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
    # A write to a name that exists only on COMMANDS:'s read-only member:
    # `status` is shipped on `/commands`, and the writable member,
    # `/local/commands`, is empty in this fixture. Without the fix, `write`
    # opens with WRITE|TRUNCATE and no CREATE, on the reasoning that a
    # truncate-without-create open is an existence probe -- but the ext4
    # backend's "wb" mode creates whether or not CREATE was asked for, so
    # the probe always succeeds at the writable member by creating an empty
    # `status` there, and the walk never reaches the member that actually
    # holds the name. A person editing the shipped file would silently get a
    # second, shadow copy instead of the edit they typed, and every later
    # `cat`, `ls` and `write` of `commands:status` would see the shadow, not
    # the original. The fix locates the holder before opening, so this must
    # report the refusal the read-only member actually gives.
    ("write commands:status no", "access denied"),
    # The mirror case: a name on *no* member of COMMANDS: at all. The line
    # above proves a name that genuinely exists, on a member that cannot
    # take a write, is refused; this one proves a name nobody has yet still
    # gets created -- on member 0, the only member willing to take it --
    # rather than being refused by the same "access denied" the read-only
    # member earns further down the walk.
    ("write commands:brandnew text", "brandnew"),
    # Where it landed, not merely that something happened: member 0 is
    # /local/commands, the writable member, and this is the only place a
    # name that was on no member could have gone.
    ("ls commands:", "brandnew  [0]"),
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
    # What the machine's interrupt endpoints are doing, asked from the prompt.
    # Before this existed a device that quarantined itself was invisible: it
    # looked exactly like a device nobody was using, and every call against it
    # came back with an I/O error three layers from its cause.
    #
    # Read through SYS:, not the bare name: the gate now installs a shadow
    # under this same name on COMMANDS:'s writable member (below), and a bare
    # `devices` or a `COMMANDS:devices` walks that union's members in the same
    # order either way, member 0 first -- so both would now launch the shadow,
    # not this program. SYS: is one mount of the whole volume, not a union, and
    # reaches the shipped binary directly, which is what this line is actually
    # here to run.
    ("sys:commands/devices", "delivered"),
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
    # Both members listed, nothing deduplicated. The line above already proves
    # a lookup finds the writable member's `devices` first; this proves `ls`
    # still shows the shipped member's `devices` too -- checked by name and
    # member rather than by the bare "[1]" every other row here would also
    # satisfy, since a listing that quietly dropped the loser would read no
    # differently from one that never had it.
    ("ls commands:", "devices  [1]"),
    # Last on purpose. `run()` reads the *current* screen after SCRIPT finishes
    # to check that this line's own output and the shell's report of its exit
    # are still on it and in order (see the child-order check below) -- the
    # terminal is 30 rows and five new commands' worth of listings sit between
    # here and where this line used to be, which was enough to scroll that
    # output off before the check ever read it. Run last, its output is what
    # the check finds.
    ("events --boot -1", "the store is RAM"),
]

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
assert SCRIPT[-1] == ("events --boot -1", "the store is RAM"), (
    "SCRIPT's last entry must stay (\"events --boot -1\", \"the store is "
    "RAM\") -- the exit-order check in run() rereads the rendered screen "
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
        astra_image.install(scratch, catalog)
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
                    print("FAIL: %r produced %r but the shell never came "
                          "back to %r within %.0fs"
                          % (line, expected, PROMPT, command_deadline))
                    for row in machine.screen():
                        if row:
                            print("    |%s|" % row)
                    dump_ring(machine)
                    return 1

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
                         if "the store is RAM" in row), None)
            exited = next((index for index, row in enumerate(rows)
                           if "events: exited 13" in row), None)
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
