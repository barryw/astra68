#!/usr/bin/env python3
"""Boot the machine, take its trace ring away, and read it.

This is the gate for the event system as a whole. Everything below it has a
test of its own -- the ring has one, the macro has one, the catalog extractor
has one -- and none of them proves the thing that matters: that an event a
program emitted on the machine comes back out the other side as a line a person
can read, with its arguments substituted and its source location attached.

It asserts four properties in one pass:

  * a typed event survives the round trip. The supervisor records what
    namespace it bound; the ring carries a message id and eight bytes, and
    nothing else, because the format string stayed in a section the ROM image
    strips.
  * the format is resolved from the catalog. If the catalog were wrong or
    missing the line would read `<message 0x...>`, which is a failure here.
  * kernel events and user events are one ordered stream. The user event's
    sequence number falls among the kernel's, which is the property the whole
    "one ring" decision exists for.
  * a refusal is recorded and reaches the syslog. `open` asks the supervisor
    for an application that does not exist; the shell reports the refusal,
    the supervisor writes `application launch:<status>: failed` into the
    ring, and the events service appends it to the host's
    WORK:.astra/syslog, named `supervisor` (docs/SYSTEM_LOG.md section 5).

The shell's per-command activity clause is gone with the shell that emitted
it: zsh emits no typed events, so there is nothing for it to tag.
"""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import astra_image

COMMAND = "open /apps/Nope.app"
REFUSAL = "open: application launch failed"
SYSLOG_LINE = re.compile(
    r"^\S+ error supervisor\[00000001\] log: application launch:\d+: "
    r"failed$", re.M)
RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000
BOOT_MARKER = "stage 8"
HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))


def load_terminal_module():
    """test-terminal.py by path: its name is not an identifier."""
    path = os.path.join(HERE, "test-terminal.py")
    spec = importlib.util.spec_from_file_location("astra_terminal_gate", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def read_host_syslog(root):
    """The host copy of the syslog under the hostfs root, or ''."""
    for directory, _, files in os.walk(root):
        if os.path.basename(directory) == ".astra" and "syslog" in files:
            with open(os.path.join(directory, "syslog"), "r",
                      errors="replace") as handle:
                return handle.read()
    return ""


def save_ring(machine, path):
    """The trace ring, retained RAM at a fixed address, as bytes."""
    # The quotes around the path are required, and their absence is one line
    # of "invalid char" from the monitor.
    reply = machine.qmp.monitor('pmemsave 0x%08x %d "%s"' %
                                (RING_ADDRESS, RING_SIZE, path))
    if reply and reply.strip():
        raise RuntimeError("pmemsave: %s" % reply.strip())
    with open(path, "rb") as handle:
        return handle.read()


def run(qemu, rom, image, catalog, deadline):
    """Boots, types one refused command, and returns the ring at boot, the
    ring after the command, the serial log and the host syslog.

    Two rings, because opening a terminal writes enough (the loader's debug
    lines) to wrap the ring, and a wrap takes the boot's typed arguments with
    it. The boot ring is taken the moment the desktop is up.

    The typing goes through the desktop: a terminal is a window client now, so
    it is opened by double-clicking its icon rather than started by a manifest
    entry. The gate module owns that, and owns reading the shell back, so this
    borrows both rather than keeping a second copy that would rot separately.
    """
    gate = load_terminal_module()
    directory = tempfile.mkdtemp()
    ring_path = os.path.join(directory, "ring.bin")
    # A copy, with this build's catalog on it: the machine resolves ids from
    # /system and the gate must boot what a built machine actually has, not an
    # image that happens to be lying around.
    scratch = os.path.join(directory, "card.img")
    shutil.copyfile(image, scratch)
    astra_image.install(scratch, catalog)
    machine = gate.Machine(qemu, rom, scratch, directory)
    boot = {}
    wait_for_serial = machine.wait_for_serial

    def wait_then_save(marker, limit):
        found = wait_for_serial(marker, limit)
        if found and "ring" not in boot:
            boot["ring"] = save_ring(machine,
                                     os.path.join(directory, "boot.bin"))
        return found

    machine.wait_for_serial = wait_then_save
    try:
        if not gate.open_terminal(machine, deadline, 60.0):
            raise RuntimeError("no terminal within %.0fs" % deadline)
        # A launch the supervisor refuses: the shell reports it, and the
        # supervisor records why.
        machine.settle()
        before = machine.sequence()
        machine.qmp.type_line(COMMAND)
        if machine.wait_for_text(REFUSAL, 60.0, before)[0] is None:
            raise RuntimeError("the shell never refused %r" % COMMAND)
        machine.settle()
        # The kernel ring is not waitable: the events service sweeps it at
        # least once a second.
        syslog = ""
        limit = time.monotonic() + 30.0
        while time.monotonic() < limit:
            syslog = read_host_syslog(os.path.join(directory, "hostfs"))
            if SYSLOG_LINE.search(syslog):
                break
            time.sleep(0.5)
        return (boot["ring"], save_ring(machine, ring_path), machine.log,
                syslog)
    finally:
        machine.close()


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

    boot_ring, ring, serial, syslog = run(
        arguments.qemu, arguments.rom, arguments.image, arguments.catalog,
        arguments.boot_deadline)
    _, boot_lines = trace_decode.decode(boot_ring, catalog, names)
    header, lines = trace_decode.decode(ring, catalog, names)

    boot_user = [line for line in boot_lines if " kernel   " not in line]
    user = [line for line in lines if " kernel   " not in line]
    for line in user:
        print(line)

    failures = []
    if not boot_user or not user:
        failures.append("no user event reached the ring")
    resolved = [line for line in boot_user if "namespace bound" in line]
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
                  for other in boot_lines if " kernel   " in other]
        if not kernel or not (min(kernel) < sequence < max(kernel) or
                              sequence > min(kernel)):
            failures.append("the user event is not ordered among the kernel's")

    # The refusal is in the ring, as the supervisor's own text...
    refused = [line for line in user
               if "00000001/" in line and "application launch:" in line]
    if not refused:
        failures.append("the supervisor's launch refusal is not in the ring")
    # ...and in the syslog, named, as one line.
    found = SYSLOG_LINE.findall(syslog)
    if not found:
        failures.append("the refusal never reached WORK:.astra/syslog "
                        "(%d bytes) as a supervisor line" % len(syslog))
    else:
        print("syslog: %s" % found[-1])
    if not syslog.startswith("--- boot "):
        failures.append("the host syslog does not begin with a boot marker")

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
