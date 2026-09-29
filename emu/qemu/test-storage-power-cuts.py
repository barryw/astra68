#!/usr/bin/env python3
"""Exhaust every completed Astra block write/flush boundary."""

import argparse
import importlib.util
import os
import queue
import re
import shutil
import subprocess
import sys
import time


HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)

CUT = re.compile(r"Astra68 block power cut: transition=(\d+) op=(write|flush)")
PROPERTY = "astra-block-durability-transitions"
CUT_PROPERTY = "astra-block-power-cut-after"
QEMU_ARGS = ("-icount", "shift=8,align=off,sleep=on")
BLOCK_QUIET_SECONDS = 2.0
# Far longer than an event-store save takes to follow a launch under icount;
# silence that long means persistence stopped (it does after a failed save).
IDLE_QUIET_SECONDS = 60.0
# Every wait is bounded: a guest that stops talking fails the gate with its
# serial tail instead of holding an exhaustive run open forever.
WAIT_SECONDS = 600.0
PAYLOAD = "durable-data-68040"
# HOME: is the ext4 volume. WORK: is not a block path under the emulator:
# /services/hostfs serves WORK, and its publication replaces the storage
# binding, so a /work probe writes the QEMU host directory of one run and a
# fresh recovery run finds it ABSENT. trace_run proves the payload reached
# the block image.
PATH = "/home/durability-cut.txt"
WRITE = "posix --durability-gated %s %s" % (PATH, PAYLOAD)
CHECK = "posix --durability-check %s %s" % (PATH, PAYLOAD)


def wait_serial(machine, marker):
    end = time.monotonic() + WAIT_SECONDS
    while time.monotonic() < end:
        try:
            line = machine.serial.get(timeout=0.2)
        except queue.Empty:
            if machine.process.poll() is not None:
                return False
            continue
        if line is None:
            return False
        machine.log.append(line)
        if marker in line:
            return True
    return False


def wait_text(machine, needles, after):
    needles = (needles,) if isinstance(needles, str) else tuple(needles)
    end = time.monotonic() + WAIT_SECONDS
    while machine.process.poll() is None and time.monotonic() < end:
        lines, highest = machine.said(after)
        if any(any(needle in line for line in lines) for needle in needles):
            return lines, highest
        time.sleep(0.25)
    return None, after


def settle(machine):
    last = machine.sequence()
    end = time.monotonic() + WAIT_SECONDS
    while machine.process.poll() is None:
        if time.monotonic() >= end:
            raise RuntimeError("the terminal never settled")
        time.sleep(0.4)
        current = machine.sequence()
        if current == last:
            return
        last = current
    raise RuntimeError("QEMU exited while the terminal was settling")


def open_terminal(machine):
    if not wait_serial(machine, terminal.BOOT_MARKER):
        return False
    time.sleep(2.0)
    machine.qmp.double_click(*machine.desktop_icon())
    if wait_text(machine, terminal.BANNER, 0)[0] is None:
        return False
    settle(machine)
    # Durability counting needs a quiet disk. Without a host helper here,
    # media and remote-desktop fail and are retried forever, and every
    # failure is an event written and synced to storage. Both are user
    # services, so the gate stops them the way a user would.
    before = machine.sequence()
    machine.qmp.type_line("service stop remote-desktop; "
                          "service stop media; print -r -- PC-QUIET-$?")
    lines, _ = wait_text(
        machine, tuple("PC-QUIET-%d" % digit for digit in range(10)), before)
    if lines is None or not any(line.startswith("PC-QUIET-0")
                                for line in lines):
        return False
    settle(machine)
    return True


def transitions(machine):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": PROPERTY})


def wait_for_background_commit(machine, baseline):
    previous = transitions(machine)
    changed = previous != baseline
    quiet_since = time.monotonic()
    end = quiet_since + WAIT_SECONDS

    while machine.process.poll() is None:
        if time.monotonic() >= end:
            raise RuntimeError("background storage never settled")
        time.sleep(0.1)
        current = transitions(machine)
        if current != previous:
            previous = current
            changed = True
            quiet_since = time.monotonic()
        elif changed and time.monotonic() - quiet_since >= BLOCK_QUIET_SECONDS:
            return
        elif not changed and \
                time.monotonic() - quiet_since >= IDLE_QUIET_SECONDS:
            # The event store writes only when events arrived, so a commit
            # after READY is not guaranteed; a long silence is settled too.
            return
    raise RuntimeError("QEMU exited while background storage was settling")


def write_probe(machine, cut=0, seen=None):
    """Run the probe, cutting power at write/flush transition @p cut (0: no
    cut). @p seen, when given, records whether fsync was acknowledged -- the
    guest printed SYNCED -- before the machine went away."""
    settle(machine)
    machine.qmp.execute("stop")
    machine.qmp.execute("qom-set", {
        "path": "/machine", "property": PROPERTY, "value": 0})
    machine.qmp.execute("qom-set", {
        "path": "/machine", "property": CUT_PROPERTY, "value": 0})
    machine.qmp.execute("cont")
    before = machine.sequence()
    machine.qmp.type_line(WRITE)
    lines, ready_sequence = wait_text(
        machine, ("ASTRA DURABILITY READY", "posix:"), before)
    if lines is None or not any("ASTRA DURABILITY READY" in line
                                for line in lines):
        raise RuntimeError("durability probe failed before start gate")
    wait_for_background_commit(machine, 0)
    machine.qmp.execute("stop")
    machine.qmp.execute("qom-set", {
        "path": "/machine", "property": PROPERTY, "value": 0})
    machine.qmp.execute("qom-set", {
        "path": "/machine", "property": CUT_PROPERTY, "value": cut})
    machine.qmp.execute("cont")
    machine.qmp.key("ret")
    lines, synced_sequence = wait_text(
        machine, ("ASTRA DURABILITY SYNCED", "posix:"), ready_sequence)
    if lines is None or not any("ASTRA DURABILITY SYNCED" in line
                                for line in lines):
        raise RuntimeError("durability probe failed before fsync")
    if seen is not None:
        seen["synced"] = True
    machine.qmp.execute("stop")
    synced = transitions(machine)
    machine.qmp.execute("cont")
    machine.qmp.key("ret")
    if not any("ASTRA DURABILITY PASS" in line for line in lines):
        lines, _ = wait_text(
            machine, ("ASTRA DURABILITY PASS", "posix:"), synced_sequence)
    if lines is None or not any("ASTRA DURABILITY PASS" in line
                                for line in lines):
        raise RuntimeError("durability probe failed after fsync")
    machine.qmp.execute("stop")
    total = transitions(machine)
    machine.qmp.execute("cont")
    return synced, total


def check_probe(machine):
    settle(machine)
    before = machine.sequence()
    machine.qmp.type_line(CHECK)
    lines, _ = wait_text(machine, ("ASTRA DURABILITY ", "posix:"), before)
    if lines is None:
        raise RuntimeError("durability check did not complete")
    for result in ("EXACT", "ABSENT", "PREFIX"):
        if any("ASTRA DURABILITY " + result in line for line in lines):
            return result
    raise RuntimeError("durability check rejected recovered data")


def close_and_log(machine):
    machine.close()
    time.sleep(0.1)
    machine.recent_serial()
    return "\n".join(machine.log)


def serial_tail(machine):
    machine.recent_serial()
    tail = "\n".join(machine.log[-100:])
    # Once a service drains the ring the console goes quiet, so why a boot
    # stopped is only in the ring: decode its tail while the machine lives.
    if machine.process.poll() is None:
        try:
            tail += "\n--- trace ring (newest 250) ---\n" + \
                "\n".join(machine.trace()[-250:])
        except Exception as error:
            tail += "\n--- trace ring unavailable: %s ---" % error
    return tail


def trace_run(args, image, run_directory):
    machine = terminal.Machine(args.qemu, args.rom, image, run_directory,
                               QEMU_ARGS)
    try:
        if not open_terminal(machine):
            raise RuntimeError("trace boot did not reach Terminal")
        synced, total = write_probe(machine)
        if os.environ.get("ASTRA_QEMU_BLOCK_TRACE"):
            for line in machine.log:
                if "Astra68 block durability transition=" in line:
                    print(line, flush=True)
        return synced, total
    except Exception as error:
        status = machine.process.poll()
        raise RuntimeError("trace QEMU failed (%s, status %s)\n%s" %
                           (error, "running" if status is None else status,
                            serial_tail(machine))) from error
    finally:
        close_and_log(machine)


def cut_run(args, image, run_directory, cut):
    """Cut power at transition @p cut. Returns None when this run never made
    that many transitions, else whether fsync was acknowledged before the cut.

    Background writers (the event store saves whenever events arrive) make the
    transition sequence differ from run to run, so a transition index is not
    a fixed point in the probe. What the recovered data must be is decided by
    what this run's guest was told: once SYNCED was printed the data is owed.
    Seeing SYNCED races the cut, which can only make a run lenient."""
    machine = terminal.Machine(args.qemu, args.rom, image, run_directory,
                               QEMU_ARGS)
    seen = {"synced": False}
    booted = False
    probed = False
    try:
        booted = open_terminal(machine)
        if booted:
            write_probe(machine, cut, seen)
            probed = True
    except (BrokenPipeError, ConnectionError, EOFError, OSError,
            RuntimeError):
        pass
    finally:
        log = close_and_log(machine)
    match = CUT.search(log)
    if machine.process.returncode == 86 and match is not None and \
            int(match.group(1)) == cut:
        return seen["synced"]
    if probed:
        return None
    raise RuntimeError("cut %d exited %s without its marker\n%s" %
                       (cut, machine.process.returncode,
                        "\n".join(machine.log[-20:])))


def independent_fsck(args, image, volume):
    offset, length = terminal.astra_image.ext4_partition(image)
    terminal.astra_image._slice(image, offset, length, volume)
    repaired = subprocess.run([args.e2fsck, "-fy", volume],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
    clean = subprocess.run([args.e2fsck, "-fn", volume],
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
    if repaired.returncode > 2 or clean.returncode != 0:
        raise RuntimeError("independent e2fsck failed\n%s\n%s" %
                           (repaired.stdout, clean.stdout))


def recovery_run(args, image, run_directory, cut, acknowledged):
    machine = terminal.Machine(args.qemu, args.rom, image, run_directory,
                               QEMU_ARGS)
    try:
        if not open_terminal(machine):
            raise RuntimeError("recovery after cut %d did not reach Terminal" %
                               cut)
        outcome = check_probe(machine)
        # The cut hook fires after host I/O completes but before its completion
        # is published to Astra, so SYNCED printed means fsync returned before
        # the cut transition.
        if acknowledged and outcome != "EXACT":
            raise RuntimeError("cut %d followed acknowledged fsync but data is %s"
                               % (cut, outcome))
        write_probe(machine)
        if check_probe(machine) != "EXACT":
            raise RuntimeError("filesystem failed a new fsync after cut %d" %
                               cut)
    except Exception as error:
        status = machine.process.poll()
        raise RuntimeError("recovery QEMU failed after cut %d (%s, status "
                           "%s)\n%s" %
                           (cut, error,
                            "running" if status is None else status,
                            serial_tail(machine))) from error
    finally:
        close_and_log(machine)


def fresh_copy(source, target):
    if os.path.exists(target):
        os.unlink(target)
    shutil.copyfile(source, target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--e2fsck", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--trace-only", action="store_true")
    parser.add_argument("--expected-synced", type=int)
    parser.add_argument("--expected-total", type=int)
    parser.add_argument("--start", type=int, default=1)
    parser.add_argument("--end", type=int)
    args = parser.parse_args()

    os.makedirs(args.work, exist_ok=True)
    # The probe is the posix test command, which is not in the release set:
    # install the current build into the gate's own template, so a missing
    # command fails here instead of leaving the guest shell with nothing to
    # run.
    template = os.path.join(args.work, "template.img")
    fresh_copy(args.image, template)
    terminal.astra_image.install_test_command(template, "posix")
    if (args.expected_synced is None) != (args.expected_total is None):
        raise RuntimeError("expected synced and total counts must be supplied "
                           "together")
    if args.expected_synced is not None:
        synced, total = args.expected_synced, args.expected_total
        if synced < 1 or total < synced:
            raise RuntimeError("expected counts must satisfy 1 <= synced <= "
                               "total")
        print("ASTRA POWER CUT TRACE REUSED: synced=%d total=%d" %
              (synced, total), flush=True)
    else:
        traces = []
        for index in range(2):
            image = os.path.join(args.work, "trace-%d.img" % index)
            run_directory = os.path.join(args.work, "trace-%d" % index)
            fresh_copy(template, image)
            os.makedirs(run_directory, exist_ok=True)
            traces.append(trace_run(args, image, run_directory))
            with open(image, "rb") as handle:
                if PAYLOAD.encode("ascii") not in handle.read():
                    raise RuntimeError("durability probe never reached the "
                                       "block volume; %s is not on it" % PATH)
            os.unlink(image)
            shutil.rmtree(run_directory)
        # Background writes make the count vary between runs; the traces
        # only size the sweep, and each cut is judged by its own run.
        synced = min(trace[0] for trace in traces)
        total = max(trace[1] for trace in traces)
        print("ASTRA POWER CUT TRACE PASS: synced=%d total=%d" %
              (synced, total), flush=True)
    if args.trace_only:
        return 0
    end = total if args.end is None else args.end
    if args.start < 1 or end < args.start or end > total:
        raise RuntimeError("cut range must be within 1..%d" % total)

    owed = 0
    skipped = 0
    for cut in range(args.start, end + 1):
        image = os.path.join(args.work, "cut-%06d.img" % cut)
        volume = os.path.join(args.work, "cut-%06d.ext4" % cut)
        cut_directory = os.path.join(args.work, "cut-%06d-run" % cut)
        recovery_directory = os.path.join(
            args.work, "cut-%06d-recovery" % cut)
        fresh_copy(template, image)
        os.makedirs(cut_directory, exist_ok=True)
        os.makedirs(recovery_directory, exist_ok=True)
        try:
            acknowledged = cut_run(args, image, cut_directory, cut)
            if acknowledged is not None:
                independent_fsck(args, image, volume)
                recovery_run(args, image, recovery_directory, cut,
                             acknowledged)
        except Exception:
            print("retained failing image: %s" % image, file=sys.stderr)
            raise
        os.unlink(image)
        if os.path.exists(volume):
            os.unlink(volume)
        shutil.rmtree(cut_directory)
        shutil.rmtree(recovery_directory)
        if acknowledged is None:
            skipped += 1
            print("cut %d/%d SKIPPED: this run made fewer transitions" %
                  (cut, total), flush=True)
            continue
        owed += acknowledged
        print("cut %d/%d PASS%s" % (cut, total,
                                    " (after acknowledged fsync)"
                                    if acknowledged else ""), flush=True)
    # Background writes may shift a cut, but a sweep that never cut after an
    # acknowledged fsync has tested nothing that matters.
    if args.start == 1 and end == total and owed == 0:
        raise RuntimeError("no cut followed an acknowledged fsync")
    if skipped * 2 > end + 1 - args.start:
        raise RuntimeError("%d of %d cuts were never reached" %
                           (skipped, end + 1 - args.start))
    if args.start == 1 and end == total:
        print("ASTRA POWER CUT SWEEP PASS: %d transitions" % total)
    else:
        print("ASTRA POWER CUT RANGE PASS: %d..%d of %d" %
              (args.start, end, total))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
