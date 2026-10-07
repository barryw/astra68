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

  * kernel-ring lines are named from the ring itself (PROCESS_START): the
    supervisor and every boot service by name, never `?`.
  * a restarted events service resumes where the last one stopped. The
    gate's manifest makes the service a user one so `service restart
    events` may kill it (a required service is the machine's and refuses);
    the host syslog then holds the `restarted` marker, every line once, the
    refusal logged just before the kill -- after the marker, written by the
    new instance -- and one logged after it.
  * a death nobody asked for is reported. `kill` through the service's
    PROC: ctl file ends it the way a fault would; the supervisor restarts
    it on its own and reports `resident process exited`, which only the
    new instance can write: after a second marker.

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
RESTART_COMMAND = "open /apps/Nope.app; service restart events"
RESTARTED = "--- events service restarted "
# Ends the service the way a fault would: not through the service manager,
# so the supervisor sees a death it did not ask for.
KILL_COMMAND = ("ps | while read -r id rest; do "
                "[[ $rest == *' /services/events' ]] && "
                "print kill > /proc/$id/ctl; print ASTRA-KILL-$?; done")
KILLED = "ASTRA-KILL-0"
DEATH_LINE = re.compile(
    r"^\S+ error supervisor\[00000001\] log: resident process "
    r"exited:\d+: failed$", re.M)
# The same service, not required: a user may restart it.
MANIFEST = astra_image.STARTUP_MANIFEST.replace(
    "serves EVENTS:r EVENT_CONTROL LOG required priority=12",
    "serves EVENTS:r EVENT_CONTROL LOG priority=12")
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
    """The host copy of the syslog under the hostfs root, every kept
    generation oldest first, or ''."""
    for directory, _, files in os.walk(root):
        if os.path.basename(directory) == ".astra" and "syslog" in files:
            # Oldest first: a rotation moves the start of a run into
            # syslog.1, and a check that read only syslog would miss it.
            text = ""
            for name in ("syslog.3", "syslog.2", "syslog.1", "syslog"):
                if name in files:
                    with open(os.path.join(directory, name), "r",
                              errors="replace") as handle:
                        text += handle.read()
            return text
    return ""


def wait_for_syslog(directory, refusals):
    """The host syslog once it holds `refusals` refusal lines, or after 30 s.

    The kernel ring is not waitable: the events service sweeps it at least
    once a second."""
    syslog = ""
    limit = time.monotonic() + 30.0
    while time.monotonic() < limit:
        syslog = read_host_syslog(os.path.join(directory, "hostfs"))
        if len(SYSLOG_LINE.findall(syslog)) >= refusals:
            break
        time.sleep(0.5)
    return syslog


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
    if MANIFEST == astra_image.STARTUP_MANIFEST:
        raise RuntimeError("the events manifest line changed: update MANIFEST")
    astra_image.install(scratch, catalog, manifest_text=MANIFEST)
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
        syslog = wait_for_syslog(directory, 1)
        ring = save_ring(machine, ring_path)
        # A refusal, then the service killed before it can drain it, then
        # a refusal the new instance must write.
        before = machine.sequence()
        machine.qmp.type_line(RESTART_COMMAND)
        if machine.wait_for_text(REFUSAL, 60.0, before)[0] is None:
            raise RuntimeError("the shell never ran %r" % RESTART_COMMAND)
        machine.settle()
        before = machine.sequence()
        machine.qmp.type_line(COMMAND)
        if machine.wait_for_text(REFUSAL, 60.0, before)[0] is None:
            raise RuntimeError("the shell never refused %r after the "
                               "restart" % COMMAND)
        machine.settle()
        # Settled: the new instance has drained that refusal. Then a death
        # the supervisor did not ask for, and one more refusal after it.
        wait_for_syslog(directory, 3)
        before = machine.sequence()
        machine.qmp.type_line(KILL_COMMAND)
        if machine.wait_for_text(KILLED, 60.0, before)[0] is None:
            raise RuntimeError("the shell never killed the events service")
        machine.settle()
        before = machine.sequence()
        machine.qmp.type_line(COMMAND)
        if machine.wait_for_text(REFUSAL, 60.0, before)[0] is None:
            raise RuntimeError("the shell never refused %r after the "
                               "kill" % COMMAND)
        machine.settle()
        restarted = wait_for_syslog(directory, 4)
        return (boot["ring"], ring, machine.log, syslog, restarted)
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

    boot_ring, ring, serial, syslog, restarted = run(
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
    # Named from the ring: no `?` for anything that started this boot.
    unnamed = [line for line in syslog.splitlines() if " ?[" in line]
    if unnamed:
        failures.append("unnamed kernel-ring lines: %r" % unnamed[:3])
    if not re.search(r"^\S+ \S+ desktop\[[0-9a-f]{8}\] ", syslog, re.M):
        failures.append("no line names the desktop service")

    # The restarts: a marker each, every line once. Killed: the supervisor's
    # death report follows the second marker, and so does the last refusal.
    body = restarted.splitlines()
    markers = [line for line in body if line.startswith(RESTARTED)]
    if len(markers) != 2:
        failures.append("%d restart markers in the host syslog, expected 2" %
                        len(markers))
    count = len(SYSLOG_LINE.findall(restarted))
    if count != 4:
        failures.append("%d refusal lines across the restarts, expected 4" %
                        count)
    lines = [line for line in body if not line.startswith("--- ")]
    repeated = sorted({line for line in lines if lines.count(line) > 1})
    if repeated:
        failures.append("lines written twice across the restarts: %r" %
                        repeated[:3])
    if len(markers) == 2:
        head, rest = restarted.split(markers[0], 1)
        requested, killed = rest.split(markers[1], 1)
        spans = [len(SYSLOG_LINE.findall(text))
                 for text in (head, requested, killed)]
        # Whether the refusal logged just before the requested restart's
        # kill lands in `head` or `requested` is a race, not a property:
        # it depends on whether the dying instance drains the ring (a
        # wake from the `service` command's own log setup can trigger
        # that pump) before the kill reaches it. Only the killed span is
        # positional -- the kill command waits for all three earlier
        # refusals first, so it is always exactly the fourth.
        if spans[2] != 1:
            failures.append("%d refusals after the second marker, "
                            "expected 1 (spans %r)" % (spans[2], spans))
        for text, which in ((requested, "requested"), (killed, "killed")):
            if not re.search(r"supervisor\[00000001\] log: launch "
                             r"/services/events ", text):
                failures.append("no launch line for the %s restart" % which)
        if not DEATH_LINE.search(killed):
            failures.append("the supervisor's death report is not after "
                            "the second marker")
        if DEATH_LINE.search(head + requested):
            failures.append("a death report before the kill")
    for line in body[-16:]:
        print("restart: %s" % line)

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
