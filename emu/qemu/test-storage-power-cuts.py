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
PAYLOAD = "durable-data-68030"
PATH = "WORK:durability-cut.txt"
WRITE = "posix --durability-gated %s %s" % (PATH, PAYLOAD)
CHECK = "posix --durability-check %s %s" % (PATH, PAYLOAD)


def wait_serial(machine, marker):
    while True:
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


def wait_text(machine, needles, after):
    needles = (needles,) if isinstance(needles, str) else tuple(needles)
    while machine.process.poll() is None:
        lines, highest = machine.said(after)
        if any(any(needle in line for line in lines) for needle in needles):
            return lines, highest
        time.sleep(0.25)
    return None, after


def settle(machine):
    last = machine.sequence()
    while machine.process.poll() is None:
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
    machine.qmp.double_click(*terminal.TERMINAL_ICON)
    if wait_text(machine, terminal.BANNER, 0)[0] is None:
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

    while machine.process.poll() is None:
        time.sleep(0.1)
        current = transitions(machine)
        if current != previous:
            previous = current
            changed = True
            quiet_since = time.monotonic()
        elif changed and time.monotonic() - quiet_since >= BLOCK_QUIET_SECONDS:
            return
    raise RuntimeError("QEMU exited while background storage was settling")


def write_probe(machine, cut=0):
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
    return "\n".join(machine.log[-100:])


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
    machine = terminal.Machine(args.qemu, args.rom, image, run_directory,
                               QEMU_ARGS)
    try:
        if open_terminal(machine):
            write_probe(machine, cut)
        if machine.process.poll() is None:
            raise RuntimeError("cut %d was not reached" % cut)
    except (BrokenPipeError, ConnectionError, EOFError, OSError,
            RuntimeError):
        pass
    finally:
        log = close_and_log(machine)
    match = CUT.search(log)
    if machine.process.returncode != 86 or match is None or \
            int(match.group(1)) != cut:
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


def recovery_run(args, image, run_directory, cut, synced):
    machine = terminal.Machine(args.qemu, args.rom, image, run_directory,
                               QEMU_ARGS)
    try:
        if not open_terminal(machine):
            raise RuntimeError("recovery after cut %d did not reach Terminal" %
                               cut)
        outcome = check_probe(machine)
        # The cut hook fires after host I/O completes but before its completion
        # is published to Astra.  The observed transition itself therefore has
        # not completed from fsync's point of view; only later cuts are post-fsync.
        if cut > synced and outcome != "EXACT":
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
            fresh_copy(args.image, image)
            os.makedirs(run_directory, exist_ok=True)
            traces.append(trace_run(args, image, run_directory))
            os.unlink(image)
            shutil.rmtree(run_directory)
        if traces[0] != traces[1]:
            raise RuntimeError(
                "write/flush transition count is not repeatable: %r" %
                (traces,))
        synced, total = traces[0]
        print("ASTRA POWER CUT TRACE PASS: synced=%d total=%d" %
              (synced, total), flush=True)
    if args.trace_only:
        return 0
    end = total if args.end is None else args.end
    if args.start < 1 or end < args.start or end > total:
        raise RuntimeError("cut range must be within 1..%d" % total)

    for cut in range(args.start, end + 1):
        image = os.path.join(args.work, "cut-%06d.img" % cut)
        volume = os.path.join(args.work, "cut-%06d.ext4" % cut)
        cut_directory = os.path.join(args.work, "cut-%06d-run" % cut)
        recovery_directory = os.path.join(
            args.work, "cut-%06d-recovery" % cut)
        fresh_copy(args.image, image)
        os.makedirs(cut_directory, exist_ok=True)
        os.makedirs(recovery_directory, exist_ok=True)
        try:
            cut_run(args, image, cut_directory, cut)
            independent_fsck(args, image, volume)
            recovery_run(args, image, recovery_directory, cut, synced)
        except Exception:
            print("retained failing image: %s" % image, file=sys.stderr)
            raise
        os.unlink(image)
        os.unlink(volume)
        shutil.rmtree(cut_directory)
        shutil.rmtree(recovery_directory)
        print("cut %d/%d PASS" % (cut, total), flush=True)
    if args.start == 1 and end == total:
        print("ASTRA POWER CUT SWEEP PASS: %d transitions" % total)
    else:
        print("ASTRA POWER CUT RANGE PASS: %d..%d of %d" %
              (args.start, end, total))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
