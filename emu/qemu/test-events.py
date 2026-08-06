#!/usr/bin/env python3
"""Boot the machine, take its trace ring away, and read it.

This is the gate for the event system as a whole. Everything below it has a
test of its own -- the ring has one, the macro has one, the catalog extractor
has one -- and none of them proves the thing that matters: that an event a
program emitted on the machine comes back out the other side as a line a person
can read, with its arguments substituted and its source location attached.

It asserts three properties in one pass:

  * a typed event survives the round trip. The supervisor records what
    namespace it bound; the ring carries a message id and eight bytes, and
    nothing else, because the format string stayed in a section the ROM image
    strips.
  * the format is resolved from the catalog. If the catalog were wrong or
    missing the line would read `<message 0x...>`, which is a failure here.
  * kernel events and user events are one ordered stream. The user event's
    sequence number falls among the kernel's, which is the property the whole
    "one ring" decision exists for.
  * one command is one story. A refused command emits from the shell twice --
    once accepting the line, once refusing it -- and both carry the same
    non-zero activity, which nothing in the shell passed to either.
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
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import astra_image

COMMAND = "write sys:nope no"
RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000
BOOT_MARKER = "stage 8"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


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
        self.file.readline()
        self.execute("qmp_capabilities")

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write((json.dumps(request) + "\n").encode())
        while True:
            reply = json.loads(self.file.readline())
            if "return" in reply or "error" in reply:
                return reply

    def monitor(self, line):
        return self.execute("human-monitor-command", {"command-line": line})

    def send(self, down, qcode):
        self.execute("input-send-event", {"events": [
            {"type": "key",
             "data": {"down": down, "key": {"type": "qcode", "data": qcode}}}]})
        time.sleep(0.02)

    def key(self, qcode):
        for down in (True, False):
            self.send(down, qcode)

    def type_line(self, text):
        """A colon is shift and semicolon; assign names are case-insensitive,
        so it is the only shifted key a command line here needs."""
        codes = {" ": "spc", ".": "dot", "/": "slash", "-": "minus"}
        for character in text:
            if character in codes:
                self.key(codes[character])
            elif character == ":":
                self.send(True, "shift")
                self.key("semicolon")
                self.send(False, "shift")
            else:
                self.key(character)
        self.key("ret")


def run(qemu, rom, image, catalog, deadline):
    """Boots, waits for the terminal, and returns the ring as bytes."""
    directory = tempfile.mkdtemp()
    socket_path = os.path.join(directory, "events-qmp.sock")
    ring_path = os.path.join(directory, "ring.bin")
    # A copy, with this build's catalog on it: the machine resolves ids from
    # SYS: and the gate must boot what a built machine actually has, not an
    # image that happens to be lying around.
    scratch = os.path.join(directory, "card.img")
    shutil.copyfile(image, scratch)
    astra_image.install(scratch, catalog)
    image = scratch
    machine = subprocess.Popen(
        [qemu, "-M", "astra68", "-m", "32M", "-bios", rom,
         "-display", "none", "-monitor", "none", "-serial", "stdio",
         "-no-reboot", "-qmp", "unix:%s,server=on,wait=off" % socket_path,
         "-drive", "if=none,format=raw,file=%s" % image],
        stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT)

    # Drained on a thread rather than left in the pipe: a full pipe stops the
    # machine, and a machine stopped before it emitted anything would look
    # exactly like an event system that did not work.
    serial = []
    booted = threading.Event()

    def drain():
        for line in machine.stdout:
            text = line.decode("utf-8", "replace").rstrip()
            serial.append(text)
            if BOOT_MARKER in text:
                booted.set()

    threading.Thread(target=drain, daemon=True).start()

    try:
        qmp = Qmp(socket_path)
        if not booted.wait(deadline):
            raise RuntimeError("no %r within %.0fs" % (BOOT_MARKER, deadline))
        # The terminal is up, so everything the boot path emits has been
        # emitted. The ring is retained RAM at a fixed address; the quotes
        # around the path are required, and their absence is one line of
        # "invalid char" from the monitor.
        # A command that fails, so the shell emits both halves of a story:
        # the line it accepted and the refusal it answered with.
        qmp.type_line(COMMAND)
        time.sleep(8)
        reply = qmp.monitor('pmemsave 0x%08x %d "%s"' %
                            (RING_ADDRESS, RING_SIZE, ring_path))
        if reply.get("return"):
            raise RuntimeError("pmemsave: %s" % reply["return"].strip())
        with open(ring_path, "rb") as handle:
            return handle.read(), serial
    finally:
        # Reaped, not just signalled. A lingering emulator competes with
        # whatever gate runs next, and a boot deadline missed for that reason
        # looks exactly like a machine that would not boot.
        machine.kill()
        machine.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--elf", default=os.path.join(
        ROOT, "sw/userspace/supervisor/build/m68k/astra_supervisor.elf"))
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    arguments = parser.parse_args()

    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import event_catalog
    import trace_decode

    catalog_base, blob = event_catalog.read_section(arguments.elf)
    catalog = {int(key, 16): value for key, value in
               event_catalog.parse(catalog_base, blob).items()}
    names = trace_decode.kernel_event_names(
        os.path.join(ROOT, "sw/kernel/trace.h"))

    ring, serial = run(arguments.qemu, arguments.rom, arguments.image,
                       arguments.catalog, arguments.boot_deadline)
    header, lines = trace_decode.decode(ring, catalog, names)

    user = [line for line in lines if " kernel   " not in line]
    for line in user:
        print(line)

    failures = []
    if not user:
        failures.append("no user event reached the ring")
    resolved = [line for line in user if "namespace bound" in line]
    if not resolved:
        failures.append("the supervisor's namespace event was not resolved "
                        "through the catalog")
    else:
        line = resolved[0]
        # The arguments came out of the payload, not out of the format string.
        if not re.search(r"namespace bound, \d+ assigns on session \d+", line):
            failures.append("arguments were not substituted: %r" % line)
        if "src/vfs_host.c:" not in line:
            failures.append("no source location: %r" % line)
        # One ordered stream: the user event is numbered among the kernel's.
        sequence = int(re.search(r"seq (\d+)", line).group(1))
        kernel = [int(re.search(r"seq (\d+)", other).group(1))
                  for other in lines if " kernel   " in other]
        if not kernel or not (min(kernel) < sequence < max(kernel) or
                              sequence > min(kernel)):
            failures.append("the user event is not ordered among the kernel's")

    # One command, one story: the shell's two events share an activity that
    # nothing passed to either of them.
    shell = [line for line in user if "command " in line]
    activities = set(re.findall(r"act ([0-9a-f]+)", " ".join(shell)))
    if len(shell) < 2:
        failures.append("the shell emitted %d events for a command, not 2"
                        % len(shell))
    elif len(activities) != 1:
        failures.append("the command's events carry %d activities: %s"
                        % (len(activities), sorted(activities)))

    print("ring: %d records, %d wraps, %d dropped" %
          (len(lines), header["wraps"], header["dropped"]))
    if failures:
        for failure in failures:
            print("FAIL: %s" % failure, file=sys.stderr)
        print("--- last serial ---", file=sys.stderr)
        for line in serial[-12:]:
            print(line, file=sys.stderr)
        return 1
    print("ASTRA EVENTS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
