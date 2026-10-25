#!/usr/bin/env python3
"""System log gate: docs/SYSTEM_LOG.md section 8.

Boots the desktop with the `logburst` test command installed, opens a
terminal the way a person does, and judges the system log through its host
copy (`<run>/hostfs/.astra/syslog`) and, after the machine stops, through the
boot volume's copy:

  cost     a logged line costs at most LINE_NS more than formatting it
           alone, warm, from one thread and from four.
  burst    a cold 5000-line burst overruns the ring: each run's lines are all
           in the file, or the missing ones are counted by exactly one
           `[N lines dropped]`.
  wake     a line written after the service went back to sleep reaches the
           file within 3 s: the doorbell wakes the service. With --no-sweep
           the service has no timed wake at all, so only the doorbell can.
  fast-exit  ten programs that log one line and exit at once: every line
           is in the file.
  fork     `logburst --fork` logs a line and forks: the fork succeeds, the
           child logs from its own ring, and both lines are in the file.
  quiet    `logburst --wrap` fills the kernel ring with kernel records only:
           the events service reports that gap once, and over QUIET_S idle
           seconds the store's lost count does not move and its snapshot is
           not rewritten every sweep (a stuck trace cursor did both).
  fault    `logburst 1 --fault` logs a line and faults: the line is kept,
           and the kernel's user-fault report names the same process.
  show     `log show` finds both, by program, level and text.
  doom     Chocolate Doom, opened from the desktop, writes its start-up
           banner as chocolate-doom ... stdout, in order.
  stream   `log stream`, started before Doom, prints that banner.
  format   every line of the host copy is a record or a marker, and the
           file begins with a boot marker.
  store    the boot volume's copy holds the host copy's lines, less only
           what was written in the last seconds before the machine stopped.

It takes `qemu rom image`; the image is a fresh base system image (it ships
ChocolateDoom.app and `log`), copied first so the original is untouched.
The root observer reads live store banks through the provider's private mount;
the provider keeps its exclusive lease on the raw image until shutdown.
"""

import argparse
import datetime
import importlib.util
import mmap
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import astra_image


def load(name, file):
    spec = importlib.util.spec_from_file_location(
        name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


terminal = load("astra_terminal_gate", "test-terminal.py")
mailbox_gate = load("astra_display_mailbox_gate", "test-display-mailbox.py")
audio_gate = load("astra_sdl_audio_gate", "test-sdl-audio.py")

LINE = re.compile(
    r"^(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}[+-]\d\d:\d\d|boot\+\d+\.\d{3}) "
    r"(debug|info|notice|warning|error) (\S+)\[([0-9a-f]{8})\] "
    r"(log|stdout|stderr|kernel): (.*)$")
BURST = re.compile(r"^logburst: (\d+) lines, (\d+) threads, (\d+) ns$")
FORKED = re.compile(r"^logburst: fork child ([0-9a-f]{8}) status (-?\d+)$")
BURST_LINE = re.compile(r"^burst (\d+) (\d+)$")
DROPPED = re.compile(r"^\[(\d+) lines dropped\]$")
COUNT = 5000
THREADS = 4
WARM_COUNT = 1000
ROUNDS = 5
# What a logged line may cost the program over formatting it alone, warm.
# Measured on beast under QEMU (Task 10): about 600-900 ns a line for a ring
# write, of which about 250 ns is committing the ring's pages on first touch;
# a LOG_WRITE syscall per line costs about 2,060 ns. 1,500 ns leaves the ring
# path headroom for a loaded host and still fails a logger that traps per
# line.
LINE_NS = 1500
# Exactly as Doom prints them: its Z_Init line ends in a space.
DOOM = ("Z_Init: Init zone memory allocation daemon. ",
        "V_Init: allocate screens.")
DOOM_BUNDLE = "/apps/ChocolateDoom.app"
STORE_SYSLOG = "/events/syslog"
CHECKS = ("cost", "burst", "wake", "fast-exit", "fork", "quiet", "fault",
          "doom", "stream")
FAST_EXITS = 10
# The doorbell answers at once and the sweep within its 1 s interval; with
# neither the line waits for an unrelated wake (a ring setup elsewhere).
WAKE_S = 3.0
STORE_TAIL_S = 10
# The kernel ring holds 2047 slots; 2 records a map and unmap.
WRAP_PAGES = 2500
QUIET_S = 8
# A pump that reads any user record saves the store once; a stuck cursor
# saved it every 1 s sweep (8 in QUIET_S). One for an unrelated event.
QUIET_SAVES = 1
REMOTE_DESKTOP_CONF = "services/remote-desktop/service.conf"
STORE_BANKS = ("/events/store.0", "/events/store.1")


def fail(text):
    print("FAIL %s" % text)
    sys.exit(1)


def records(text):
    """(line, match) for each line of a syslog text; match None for a
    marker or anything that is not a record."""
    return [(line, LINE.match(line)) for line in text.splitlines()]


def host_text(machine):
    return terminal.read_host_syslog(machine.reader.hostfs_root)


def command(machine, text, timeout, number):
    """Types `text` and returns (lines printed, exit status)."""
    marker = "SYSLOG-GATE-%u-" % number
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line(text + "; print -r -- " + marker + "$?")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        lines, _ = machine.said(before)
        for line in lines:
            if line.startswith(marker) and line[len(marker):].isdigit():
                return lines, int(line[len(marker):])
        time.sleep(0.1)
    fail("%r never finished: %r" % (text, machine.said(before)[0][-20:]))


def burst(machine, arguments, number):
    lines, status = command(machine, "logburst %s" % arguments, 120, number)
    for line in lines:
        match = BURST.match(line)
        if match is not None:
            if status != 0:
                break
            return int(match.group(3))
    fail("logburst %s: status %d, %r" % (arguments, status, lines[-10:]))


def burst_lines(machine, pid):
    """(sorted (thread, index) pairs, total announced drops) for `pid`."""
    pairs = []
    dropped = []
    for _, match in records(host_text(machine)):
        if (match is None or match.group(3) != "logburst" or
                int(match.group(4), 16) != pid or match.group(5) != "log"):
            continue
        line = BURST_LINE.match(match.group(6))
        if line is not None:
            pairs.append((int(line.group(1)), int(line.group(2))))
        drop = DROPPED.match(match.group(6))
        if drop is not None:
            dropped.append(int(drop.group(1)))
    return pairs, dropped


def burst_pids(machine):
    return {int(match.group(4), 16) for _, match in records(host_text(machine))
            if match is not None and match.group(3) == "logburst" and
            BURST_LINE.match(match.group(6))}


def check_count(machine, pid, threads, name):
    """Every line of the run is in the file once and in order, or counted
    by one drop marker. The service drains after the program yields, so
    wait for the file to settle."""
    expected = {(thread, index) for thread in range(threads)
                for index in range(COUNT // threads + (thread < COUNT % threads))}
    deadline = time.monotonic() + 60
    while True:
        pairs, dropped = burst_lines(machine, pid)
        if len(pairs) + sum(dropped) >= COUNT or time.monotonic() > deadline:
            break
        time.sleep(0.5)
    if len(set(pairs)) != len(pairs) or not set(pairs) <= expected:
        fail("%s: duplicated or foreign burst lines" % name)
    for thread in range(threads):
        indices = [index for t, index in pairs if t == thread]
        if indices != sorted(indices):
            fail("%s: thread %d's lines are out of order" % (name, thread))
    if threads == 1 and len(dropped) > 1:
        fail("%s: %d drop markers %r, at most one allowed" %
             (name, len(dropped), dropped))
    if len(pairs) + sum(dropped) != COUNT:
        fail("%s: %d lines and %r dropped, not %d" %
             (name, len(pairs), dropped, COUNT))
    return len(pairs), sum(dropped)


def check_cost(machine):
    """What a logged line costs the program, warm: its ring is set up before
    the clock starts, and WARM_COUNT lines fit the ring, so this measures
    writes, not setup and not drops. Each arm runs ROUNDS times, interleaved,
    and the cheapest run of each is compared: host load only ever adds time,
    so the minimum is the run it disturbed least."""
    results = {}
    for threads in (1, THREADS):
        extra = "" if threads == 1 else " --threads %d" % threads
        off, logged = [], []
        for _ in range(ROUNDS):
            off.append(burst(machine, "%d --off%s" % (WARM_COUNT, extra), 1))
            logged.append(burst(machine, "%d --warm%s" % (WARM_COUNT, extra),
                                2))
        per_line = (min(logged) - min(off)) / WARM_COUNT
        results[threads] = per_line
        print("cost: %d thread(s): off %r ns, logged %r ns, %.0f ns a line "
              "(bound %d)" % (threads, off, logged, per_line, LINE_NS))
    for threads, per_line in results.items():
        if per_line > LINE_NS:
            fail("cost: %d thread(s) pay %.0f ns a line (bound %d ns)" %
                 (threads, per_line, LINE_NS))
    print("PASS cost")
    return 2 * ROUNDS


def counted_run(machine, threads, name):
    pids = burst_pids(machine)
    burst(machine, "%d%s" % (COUNT, "" if threads == 1 else
                              " --threads %d" % threads), 3)
    deadline = time.monotonic() + 30
    while burst_pids(machine) == pids and time.monotonic() < deadline:
        time.sleep(0.25)
    new = burst_pids(machine) - pids
    if len(new) != 1:
        fail("%s: the run's lines name %r" % (name, new))
    pid = new.pop()
    return (pid,) + check_count(machine, pid, threads, name)


def check_burst(machine, earlier):
    """A cold COUNT-line burst overruns the ring: every line is in the file,
    or the missing ones are counted by exactly one drop marker."""
    # The `earlier` logged runs' lines are still draining: let every run's
    # reach the file, so each counted run below sees only its own pid appear.
    deadline = time.monotonic() + 60
    while len(burst_pids(machine)) < earlier:
        if time.monotonic() > deadline:
            fail("burst: %d of %d earlier logged runs reached the file" %
                 (len(burst_pids(machine)), earlier))
        time.sleep(0.5)
    single, kept, dropped = counted_run(machine, 1, "burst")
    _, kept4, dropped4 = counted_run(machine, THREADS, "burst-threads")
    print("burst: kept %d dropped %d; %d threads kept %d dropped %d" %
          (kept, dropped, THREADS, kept4, dropped4))
    print("PASS burst")
    return single


def check_wake(machine):
    """A line written after the service has drained the program's first
    line and gone back to sleep, by a program that then exits: nothing but
    its doorbell -- or, late, the service's 1 s sweep -- wakes the service
    for it (docs/SYSTEM_LOG.md section 3, Doorbell)."""
    pids = burst_pids(machine)
    warm = host_text(machine).count("log: logburst: warm")
    command(machine, "logburst 1 --warm --settle 2000", 60, 9)
    returned = time.monotonic()
    while burst_pids(machine) == pids:
        if time.monotonic() - returned > WAKE_S:
            fail("wake: a line written after the service slept was not in "
                 "the file within %.0f s; the run's first line %s" %
                 (WAKE_S, "was" if host_text(machine).count(
                     "log: logburst: warm") > warm else "was not either"))
        time.sleep(0.1)
    print("PASS wake (%.1f s after the program returned)" %
          (time.monotonic() - returned))


def check_fast_exit(machine):
    """FAST_EXITS programs that log one line and exit at once: each line is
    in the file. The producer is gone before the service drains its ring,
    which the service must still read to its end (docs/SYSTEM_LOG.md
    requirement 6)."""
    pids = burst_pids(machine)
    lines, status = command(machine, "repeat %d logburst 1" % FAST_EXITS,
                            120, 10)
    if status != 0 or sum(1 for line in lines if BURST.match(line)) != \
            FAST_EXITS:
        fail("fast-exit: status %d, %r" % (status, lines[-10:]))
    deadline = time.monotonic() + WAKE_S
    while len(burst_pids(machine) - pids) < FAST_EXITS:
        if time.monotonic() > deadline:
            break
        time.sleep(0.1)
    new = burst_pids(machine) - pids
    missing = FAST_EXITS - len(new)
    for pid in new:
        pairs, dropped = burst_lines(machine, pid)
        if pairs != [(0, 0)] or dropped:
            fail("fast-exit: pid %08x logged %r, dropped %r" %
                 (pid, pairs, dropped))
    if missing:
        fail("fast-exit: %d of %d one-line programs' lines are not in the "
             "file within %.0f s" % (missing, FAST_EXITS, WAKE_S))
    print("PASS fast-exit (%d programs)" % FAST_EXITS)


def check_fork(machine):
    """A program that has logged (so holds a ring producer) forks: the fork
    succeeds, and parent and child each have their line in the file, the
    child's under its own pid (docs/SYSTEM_LOG.md section 3, Fork and
    exec)."""
    lines, status = command(machine, "logburst --fork", 60, 11)
    forked = [FORKED.match(line) for line in lines if FORKED.match(line)]
    if status != 0 or not forked:
        fail("fork: status %d, %r" % (status, lines[-10:]))
    child = forked[-1].group(1)
    deadline = time.monotonic() + WAKE_S
    while True:
        said = [match for _, match in records(host_text(machine))
                if match is not None and match.group(3) == "logburst"]
        kids = [m for m in said if m.group(6) == "logburst: child" and
                m.group(4) == child]
        parents = [m for m in said if m.group(6) == "logburst: parent" and
                   m.group(4) != child]
        if kids and parents:
            break
        if time.monotonic() > deadline:
            fail("fork: child %s line %s, parent line %s" %
                 (child, "kept" if kids else "missing",
                  "kept" if parents else "missing"))
        time.sleep(0.1)
    print("PASS fork (child %s)" % child)


def mounted_store_root(machine):
    """Read the provider's private mount without opening its leased image."""
    provider = next((line.split() for line in machine.log
                     if line.startswith("ASTRA-STORAGE-PROVIDER ")), None)
    if provider is None or len(provider) != 3:
        fail("quiet: no provider mount identity")
    pid, domain = provider[1:]
    if not pid.isdecimal() or not domain.startswith("/var/tmp/astra-storage."):
        fail("quiet: invalid provider mount identity")
    with open("/proc/%s/mountinfo" % pid, encoding="ascii") as mounts:
        for line in mounts:
            fields = line.split()
            if fields[4].startswith(domain + "/") and \
                    fields[fields.index("-") + 1] == "ext4":
                root = "/proc/%s/root%s" % (pid, fields[4])
                if os.path.isdir(os.path.join(root, "events")):
                    return root
    fail("quiet: no mounted provider system volume")


def store_header(mount):
    """(generation, lost_in_transport) from the mounted provider volume."""
    newest = None
    for bank in STORE_BANKS:
        try:
            with open(os.path.join(mount, bank.lstrip("/")), "rb") as handle:
                head = handle.read(100)
        except FileNotFoundError:
            continue
        if len(head) < 100:
            continue
        generation = int.from_bytes(head[12:16], "big")
        lost = int.from_bytes(head[96:100], "big")
        if newest is None or generation > newest[0]:
            newest = (generation, lost)
    if newest is None:
        fail("quiet: no store snapshot on the volume")
    return newest


def check_quiet(machine):
    """A ring that wrapped past every user record holds only kernel records
    the events service skips. The gap is reported once; after that an idle
    machine neither counts more loss nor rewrites the store each sweep."""
    # logburst stays alive, holding the ring as it left it, while the store
    # is watched: its exit is the next thing to write a user record.
    # A second run would see the first's line: count them.
    wrapped = "logburst: wrapped %d" % WRAP_PAGES
    seen = sum(1 for _, m in records(host_text(machine))
               if m is not None and m.group(3) == "logburst" and
               m.group(6) == wrapped)
    settle_ms = (3 + QUIET_S + 20) * 1000
    machine.settle()
    start = machine.sequence()
    machine.qmp.type_line("logburst --wrap %d --settle %d; print -r -- "
                          "SYSLOG-GATE-12-$?" % (WRAP_PAGES, settle_ms))
    deadline = time.monotonic() + 60
    # Logged, not printed: drawing the line would emit display events, user
    # records that move even a stuck cursor back into the ring.
    while sum(1 for _, m in records(host_text(machine))
              if m is not None and m.group(3) == "logburst" and
              m.group(6) == wrapped) <= seen:
        if time.monotonic() > deadline:
            fail("quiet: logburst --wrap never logged %r: %r" %
                 (wrapped, machine.said(start)[0][-10:]))
        time.sleep(0.1)
    time.sleep(3)  # the next sweep reports the gap, once
    mount = mounted_store_root(machine)
    before = store_header(mount)
    time.sleep(QUIET_S)
    after = store_header(mount)
    deadline = time.monotonic() + settle_ms / 1000 + 60
    while "SYSLOG-GATE-12-0" not in machine.said(start)[0]:
        if time.monotonic() > deadline:
            fail("quiet: logburst --wrap did not exit 0: %r" %
                 machine.said(start)[0][-10:])
        time.sleep(0.2)
    saves = (after[0] - before[0]) & 0xFFFFFFFF
    if after[1] != before[1] or saves > QUIET_SAVES:
        fail("quiet: over %d s idle the store's lost count went %d -> %d "
             "and it was saved %d times (at most %d)" %
             (QUIET_S, before[1], after[1], saves, QUIET_SAVES))
    print("PASS quiet (lost %d steady, %d saves in %d s)" %
          (after[1], saves, QUIET_S))


def check_fault(machine):
    lines, status = command(machine, "logburst 1 --fault", 60, 5)
    if status == 0:
        fail("fault: logburst --fault exited 0: %r" % lines[-10:])
    deadline = time.monotonic() + 30
    while True:
        said = [match for _, match in records(host_text(machine))
                if match is not None]
        last = [m for m in said if m.group(3) == "logburst" and
                m.group(6) == "logburst: about to fault"]
        pid = int(last[-1].group(4), 16) if last else None
        report = [m for m in said if pid is not None and
                  (m.group(5) == "kernel" or m.group(3) == "kernel") and
                  m.group(6).startswith("user fault: process %08x " % pid)]
        if last and report:
            break
        if time.monotonic() > deadline:
            fail("fault: last line %s, kernel report %s" %
                 ("kept" if last else "missing",
                  "kept" if report else "missing"))
        time.sleep(0.5)
    if last[-1].group(2) != "error" or report[-1].group(2) != "error":
        fail("fault: levels %s and %s, not error" %
             (last[-1].group(2), report[-1].group(2)))
    print("PASS fault")
    print("PASS kernel (%s)" % report[-1].group(0))
    return pid


def check_show(machine, pid):
    lines, status = command(
        machine, "log show --program logburst --level error --grep 'about to'",
        60, 6)
    wanted = "error logburst[%08x] log: logburst: about to fault" % pid
    if status != 0 or not any(line.endswith(wanted) for line in lines):
        fail("show: status %d, no %r in %r" % (status, wanted, lines[-10:]))
    lines, status = command(
        machine, "log show --boot 0 --stream kernel --last 1h --grep %08x"
        % pid, 60, 7)
    wanted = "kernel[00000000] kernel: user fault: process %08x" % pid
    if status != 0 or not any(wanted in line for line in lines):
        fail("show: status %d, no %r in %r" % (status, wanted, lines[-10:]))
    leaked = [line for line in lines if LINE.match(line) and
              "user fault" not in line]
    if leaked:
        fail("show: lines past the filter: %r" % leaked[:5])
    lines, status = command(machine, "log show --boot -1", 60, 8)
    if status == 0:
        fail("show: --boot -1 on a first boot printed %r" % lines[-5:])
    print("PASS show")


def check_doom(machine, icon, stream):
    """Doom from the desktop while `log stream` runs in the terminal: the
    banner reaches the file, and the stream prints it as it arrives.

    The banner is one burst right behind Doom's ring setup, and the setup
    message itself wakes the service, so this does not depend on the
    doorbell; check_wake does."""
    machine.settle()
    before = machine.sequence()
    if stream:
        machine.qmp.type_line("log stream --program chocolate-doom")
        # Nothing marks the stream's start; let it take its offset.
        time.sleep(3)
    machine.qmp.double_click(*icon)
    clicked = time.monotonic()
    deadline = clicked + 60
    while True:
        said = [match for _, match in records(host_text(machine))
                if match is not None and match.group(3) == "chocolate-doom"]
        found = [next((index for index, m in enumerate(said)
                       if m.group(6) == text and m.group(5) == "stdout"),
                      None) for text in DOOM]
        if None not in found:
            break
        if time.monotonic() > deadline:
            fail("doom: %r not all logged; chocolate-doom said %r; "
                 "the log on /apps: %r" %
                 (DOOM, [m.group(0) for m in said[-20:]],
                  [line for line in host_text(machine).splitlines()
                   if "remote-desktop" not in line and
                   ("/apps/" in line or "desktop" in line or
                    "hocolate" in line)][-25:]))
        time.sleep(0.5)
    if found != sorted(found):
        fail("doom: banner out of order: %r" % found)
    pid_of = said[found[0]].group(4)
    print("PASS doom (banner in the file %.1f s after the click)" %
          (time.monotonic() - clicked))
    if not stream:
        return
    streamed, _ = machine.wait_for_text(
        # The terminal echo is a screen row: a stamped line is cut at its
        # width, so match each banner line's start.
        tuple("chocolate-doom[%s] stdout: %s" % (pid_of, text[:16])
              for text in DOOM), 30, before)
    if streamed is None:
        fail("stream: the terminal printed %r" %
             [line for line in machine.said(before)[0]
              if "stdout:" in line or "log stream" in line][-10:])
    print("PASS stream")


def check_format(text):
    lines = text.splitlines()
    if not lines or not lines[0].startswith("--- boot "):
        fail("format: the file begins %r" % (lines[:1],))
    bad = [line for line in lines
           if not line.startswith("---") and LINE.match(line) is None]
    if bad:
        fail("format: %d lines are not records: %r" % (len(bad), bad[:5]))
    print("PASS format (%d lines)" % len(lines))


def check_store(image, text, work, stopped):
    offset, length = astra_image.ext4_partition(image)
    volume = os.path.join(work, "volume.img")
    out = os.path.join(work, "store-syslog")
    astra_image._slice(image, offset, length, volume)
    # The machine was stopped, not shut down: replay its journal first.
    fsck = subprocess.run(["e2fsck", "-fy", volume], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT)
    if fsck.returncode > 2:
        fail("store: e2fsck %d: %s" % (fsck.returncode, fsck.stdout[-400:]))
    # Every kept generation, oldest first, as the host text is read.
    stored = []
    for suffix in (".3", ".2", ".1", ""):
        if os.path.exists(out):
            os.remove(out)
        subprocess.run(["debugfs", "-R", "dump %s%s %s" %
                        (STORE_SYSLOG, suffix, out), volume],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       check=False)
        if not os.path.exists(out):
            if suffix == "":
                fail("store: no %s on the volume" % STORE_SYSLOG)
            continue
        with open(out, encoding="utf-8", errors="replace") as handle:
            stored += handle.read().splitlines()
    host = text.splitlines()
    # The machine is stopped, not shut down, and the service keeps writing
    # to the end (the display, the sampler): the volume can lack
    # the last pass or two that never left the storage service's cache. So
    # the volume's copy must be the host copy, line for line, less only a
    # tail written in the last STORE_TAIL_S seconds before the stop.
    if stored != host[:len(stored)]:
        first = next(i for i, (a, b) in enumerate(zip(stored, host))
                     if a != b) if len(stored) <= len(host) else len(host)
        fail("store: %d lines on the volume, %d on the host, first "
             "difference at line %d: %r / %r" %
             (len(stored), len(host), first, stored[first:first + 1],
              host[first:first + 1]))
    tail = host[len(stored):]
    for line in tail:
        match = LINE.match(line)
        if match is not None and match.group(1).startswith("boot+"):
            continue  # no wall time to age it by; its place is checked above
        stamp = (datetime.datetime.fromisoformat(match.group(1)).timestamp()
                 if match is not None else None)
        if stamp is None or stamp < stopped - STORE_TAIL_S:
            fail("store: the volume lacks %d host lines, from %r, older "
                 "than the last %d s" % (len(tail), line, STORE_TAIL_S))
    print("PASS store (%d lines; %d written in the last %d s not yet on "
          "the volume)" % (len(stored), len(tail), STORE_TAIL_S))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--no-sweep", action="store_true",
                        help="install the events service built without its "
                             "1 s backstop sweep (services/events VARIANT="
                             "nosweep), so wake and fast-exit prove the "
                             "doorbell alone")
    parser.add_argument("--only", action="append", choices=CHECKS,
                        help="run only these machine checks (for "
                             "perturbation runs); format and store always run")
    arguments = parser.parse_args()
    wanted = set(arguments.only or CHECKS)
    with tempfile.TemporaryDirectory(prefix="astra-system-log-") as work:
        image = os.path.join(work, "card.img")
        shutil.copyfile(arguments.image, image)
        result = subprocess.run(
            ["make", "-C", os.path.join(ROOT, "sw", "userspace", "commands"),
             "build/m68k/logburst"], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT)
        if result.returncode != 0:
            fail("could not build logburst: %s" % result.stdout[-400:])
        astra_image.install_test_command(image, "logburst")
        # Without its host helper remote-desktop fails to launch, and the
        # supervisor reports each retry through the kernel ring: user records
        # every few seconds, which would mask the quiet check's stuck cursor.
        conf = os.path.join(work, "remote-desktop.conf")
        with open(conf, "w", encoding="ascii", newline="\n") as handle:
            handle.write(astra_image.CONFIGURATION[REMOTE_DESKTOP_CONF]
                         .replace("enabled true\n", "enabled false\n"))
        astra_image._replace_volume_file(
            image, conf, "/%s/%s" % (astra_image.CONFIG_DIRECTORY,
                                     REMOTE_DESKTOP_CONF))
        if arguments.no_sweep:
            events = os.path.join(ROOT, "sw", "userspace", "services",
                                  "events")
            result = subprocess.run(["make", "-C", events, "VARIANT=nosweep",
                                     "build/m68k-nosweep/events"],
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT)
            if result.returncode != 0:
                fail("could not build the no-sweep events service: %s" %
                     result.stdout[-400:])
            astra_image._replace_volume_file(
                image, os.path.join(events, "build", "m68k-nosweep",
                                    "events"), "/services/events")
            print("events service: no-sweep test build")
        run = os.path.join(work, "run")
        os.mkdir(run)
        # Doom needs a display helper and the host audio daemon, as in
        # test-chocolate-doom.py: stand-ins for both.
        mailbox = os.path.join(work, "mailbox.bin")
        mailbox_gate.create_mailbox(mailbox)
        stream = open(mailbox, "r+b")
        view = mmap.mmap(stream.fileno(), mailbox_gate.HEADER_BYTES)
        helper = mailbox_gate.Helper(view)
        helper.thread.start()
        os.environ["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
        os.environ["ASTRA_DISPLAY_PAYLOAD_PATH"] = \
            mailbox_gate.payload_path(mailbox)
        audio_socket = os.path.join(work, "audio.sock")
        audio_host = audio_gate.AudioHost(audio_socket)
        os.environ["ASTRA_AUDIO_HOST_SOCKET"] = audio_socket
        machine = terminal.Machine(arguments.qemu, arguments.rom, image, run)
        try:
            if not terminal.open_terminal(machine, 120, 60):
                fail("no terminal")
            # Now, while the desktop's layout lines are fresh.
            doom_icon = machine.application_icon(DOOM_BUNDLE)
            single = pid = earlier = 0
            if "cost" in wanted:
                earlier = check_cost(machine)
            if "burst" in wanted:
                single = check_burst(machine, earlier)
            if "wake" in wanted:
                check_wake(machine)
            if "fast-exit" in wanted:
                check_fast_exit(machine)
            if "fork" in wanted:
                check_fork(machine)
            if "quiet" in wanted:
                check_quiet(machine)
            if "fault" in wanted:
                pid = check_fault(machine)
                check_show(machine, pid)
            if "doom" in wanted or "stream" in wanted:
                check_doom(machine, doom_icon, "stream" in wanted)
            # The service writes both copies in one pass; let the last
            # one land.
            time.sleep(3)
        finally:
            stopped = time.time()
            machine.close()
            helper.stopping = True
            helper.thread.join(timeout=1)
            helper.release()
            view.close()
            stream.close()
            audio_host.close()
        text = terminal.read_host_syslog(machine.reader.hostfs_root)
        check_format(text)
        check_store(image, text, work, stopped)
        print("System log QEMU: PASS (burst pid %08x, fault pid %08x)" %
              (single, pid))


if __name__ == "__main__":
    main()
