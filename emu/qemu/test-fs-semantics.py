#!/usr/bin/env python3
"""Filesystem semantic gate (docs/FILESYSTEM_PRODUCTION_PRD.md, Phase 1).

usage: test-fs-semantics.py QEMU ROM IMAGE [--case NAME]... [--check NAME]...
                            [--bench LABEL OUT.json]

Boots a copy of IMAGE with `fssem` installed. The provider root is a
Linux-mounted ext4 image; QEMU itself runs as the caller, never as root
(FS11). This gate opens a terminal the way a person does and runs
`fssem` over the selected cases (all of CASES by default). Then it runs
the selected orchestrated CHECKS (all by default). After the machine
stops, it runs `e2fsck -fn` on the provider image.

--bench runs `fsbench --label LABEL` instead and copies its result to
OUT.json: the QEMU-on-beast record that per-task comparisons use.
"""

import argparse
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import astra_image  # noqa: E402
import qemu_runtime  # noqa: E402


def load(name, file):
    spec = importlib.util.spec_from_file_location(
        name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


terminal = load("astra_terminal_gate", "test-terminal.py")

# Every fssem case the suite runs. Later tasks append here and in fssem.c.
CASES = ["basic", "raw-lane", "lane-fork-exec", "raw-readonly",
         "raw-cross-grant", "raw-subroot", "raw-delegate", "raw-retire"]
# Orchestrated checks: name -> function(machine, provider_mount). Later tasks
# add entries; each prints "PASS <name>" or calls fail().
CHECKS = {}


def fail(text):
    print("FAIL %s" % text)
    sys.exit(1)


def command(machine, text, timeout, number):
    """Types `text` and returns (lines printed, exit status)."""
    marker = "FSSEM-GATE-%u-" % number
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


def run_cases(machine, cases):
    lines, status = command(machine, "fssem " + " ".join(cases), 600, 1)
    done = [line for line in lines if line.startswith("FSSEM DONE")]
    for line in lines:
        if line.startswith("FSSEM "):
            print(line)
    if status != 0 or done != ["FSSEM DONE passed %d failed 0" % len(cases)]:
        fail("fssem: status %d, %r" % (status, done))
    print("PASS fssem %s" % " ".join(cases))


def run_bench(machine, mount, label, output):
    lines, status = command(machine, "fsbench --label %s" % label, 3600, 2)
    if status != 0 or "fsbench: done %s" % label not in lines:
        fail("fsbench: status %d, %r" % (status, lines[-10:]))
    with open(os.path.join(mount, "work", ".astra",
                           "fsbench-%s.json" % label)) as source:
        guest = json.load(source)
    commit = subprocess.run(["git", "-C", ROOT, "rev-parse", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    with open(output, "w") as out:
        json.dump({"identity": {"host": "beast-qemu", "commit": commit},
                   "guest": guest}, out, indent=1, sort_keys=True)
    print("PASS bench %s -> %s" % (label, output))


def scoped_commands(machine):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": "astra-host-scoped-commands"})


def check_lane(machine, mount):
    """The lane is the path taken (FS44): fssem's own work moves the
    provider's scoped-command count, and raw-lane's forged MKDIR left
    nothing in SOUND."""
    before = scoped_commands(machine)
    lines, status = command(machine, "fssem basic", 120, 61)
    if status != 0:
        fail("fssem basic: %r" % lines[-5:])
    moved = scoped_commands(machine) - before
    if moved < 10:
        fail("only %d scoped commands for fssem basic" % moved)
    if os.path.exists(os.path.join(mount, "sound", "raw-lane-forged")):
        fail("a forged volume reached SOUND")
    print("PASS lane %d" % moved)


CHECKS["lane"] = check_lane


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--check", action="append", choices=sorted(CHECKS))
    parser.add_argument("--bench", nargs=2, metavar=("LABEL", "OUT"))
    arguments = parser.parse_args()
    # fssem is a TEST_COMMAND: "make all" never builds it, so it is built
    # directly here before being installed, the same as every other gate's
    # test command (test-fpu.py's fpucheck, test-system-log.py's logburst).
    built = subprocess.run(
        ["make", "-C", os.path.join(ROOT, "sw", "userspace", "commands"),
         "build/m68k/fssem"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if built.returncode != 0:
        fail("could not build fssem: %s" % built.stdout[-400:].decode(
            "utf-8", "replace"))
    work = tempfile.mkdtemp(prefix="astra-fs-sem-", dir="/tmp")
    try:
        image = os.path.join(work, "card.img")
        shutil.copyfile(arguments.image, image)
        astra_image.install_test_command(image, "fssem")
        launcher, provider = qemu_runtime.root_provider(work)
        run = tempfile.mkdtemp(prefix="astra-fs-run-", dir="/tmp")
        machine = terminal.Machine(arguments.qemu, arguments.rom, image, run,
                                   launcher=launcher)
        mount = machine.reader.hostfs_root
        try:
            if not terminal.open_terminal(machine, 120, 60):
                fail("no terminal")
            if arguments.bench:
                run_bench(machine, mount, *arguments.bench)
            else:
                run_cases(machine, arguments.case or CASES)
                for name in arguments.check or sorted(CHECKS):
                    CHECKS[name](machine, mount)
        finally:
            machine.close()
            shutil.rmtree(run, ignore_errors=True)
        # close() returns once the helper has written the image back; a
        # non-zero exit means the write-back may not have happened.
        if machine.process.returncode != 0:
            fail("launcher exited %d" % machine.process.returncode)
        checked = subprocess.run(["/usr/sbin/e2fsck", "-fn", provider],
                                 capture_output=True, text=True)
        if checked.returncode != 0:
            fail("provider e2fsck: %s" % checked.stdout[-400:])
        print("PASS provider e2fsck")
        print("ASTRA FS SEMANTICS PASS")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
