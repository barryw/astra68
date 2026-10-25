#!/usr/bin/env python3
"""Run fsprobe in a booted Astra and write its record.

usage: fs-probe.py TREE QEMU OUT.json [--n N] [--label L] [--image IMG]
                   [--pause MS]

TREE is an Astra source tree with a built ROM, base.img and fsprobe (the
tree under test, or another one such as the pre-lane direct baseline).
Boots TREE/base.img (copied) with HostFS rooted in a fresh directory, opens
Terminal, types `fsprobe LABEL N PAUSE`, prints every fsprobe line as it
arrives, and writes OUT.json: per phase (stat, read) the median and p95
latency, and per operation the cross-address-space switches, HostFS CPU and
the client's syscalls (tools/fs-probe-compare.py reads it).
"""
import argparse
import importlib.util
import json
import os
import shutil
import sys
import tempfile
import time


def parse(lines, operations):
    """fsprobe's lines -> per-phase medians and the per-operation counters
    the lane must keep near zero."""
    phases = {}
    for line in lines:
        words = line.split()
        if len(words) < 3 or words[0] != "fsprobe:":
            continue
        kind, phase = words[1], words[2]
        fields = dict(word.split("=", 1) for word in words[3:]
                      if "=" in word)
        record = phases.setdefault(phase, {"hostfs_ns": 0, "client_sc": 0})
        if kind == "lat":
            record["median_ns"] = int(fields["med"])
        elif kind == "tail":
            record["p95_ns"] = int(fields["p95"])
        elif kind == "sw":
            record["cross"] = int(fields["cross"])
        elif kind == "proc":
            name = words[3]
            if "hostfs" in name:
                record["hostfs_ns"] += int(fields["rt"])
            if "fsprobe" in name:
                record["client_sc"] += int(fields["sc"])
    result = {}
    for phase in ("stat", "read"):
        record = phases.get(phase)
        if record is None or "median_ns" not in record:
            raise SystemExit("fs-probe: no %s phase in %r" % (phase,
                                                             lines[-10:]))
        result[phase] = {
            "median_ns": record["median_ns"],
            "p95_ns": record["p95_ns"],
            "cross_per_op": record["cross"] / operations,
            "hostfs_ns_per_op": record["hostfs_ns"] / operations,
            "client_syscalls_per_op": record["client_sc"] / operations,
        }
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("tree")
    parser.add_argument("qemu")
    parser.add_argument("out")
    parser.add_argument("--n", type=int, default=5000)
    parser.add_argument("--label", default="p")
    parser.add_argument("--image", default=None)
    parser.add_argument("--pause", type=int, default=600)
    args = parser.parse_args()
    tree = os.path.abspath(args.tree)
    here = os.path.join(tree, "emu", "qemu")
    sys.path.insert(0, here)
    spec = importlib.util.spec_from_file_location(
        "astra_terminal_gate", os.path.join(here, "test-terminal.py"))
    terminal = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(terminal)
    import astra_image  # noqa: E402

    out = os.path.abspath(args.out)
    work = tempfile.mkdtemp(prefix="fs-probe-", dir="/var/tmp")
    image = os.path.join(work, "card.img")
    shutil.copyfile(args.image or os.path.join(tree, "base.img"), image)
    cwd = os.getcwd()
    os.chdir(tree)
    astra_image.install_test_command(image, "fsprobe")
    hostfs = os.path.join(work, "hostfs")
    for sub in ("work", "sound"):
        os.makedirs(os.path.join(hostfs, sub))
    rom = os.path.join(tree, "sw", "boot", "build", "astra_boot.bin")
    machine = terminal.Machine(os.path.abspath(args.qemu), rom, image, work,
                               hostfs_root=hostfs)
    lines_out = []
    status = None
    try:
        if not terminal.open_terminal(machine, 180, 120):
            print("FAIL no terminal")
            return 1
        machine.settle()
        before = machine.sequence()
        machine.qmp.type_line("fsprobe %s %d %d; print -r -- FSPROBE-EXIT-$?" %
                              (args.label, args.n, args.pause))
        seen = set()
        deadline = time.monotonic() + 1800
        while time.monotonic() < deadline and status is None:
            lines, _ = machine.said(before)
            for index, line in enumerate(lines):
                key = (index, line)
                if key in seen:
                    continue
                seen.add(key)
                if line.startswith("fsprobe:"):
                    lines_out.append(line)
                    print(line, flush=True)
                elif line.startswith("FSPROBE-EXIT-") and \
                        line[13:].isdigit():
                    status = int(line[13:])
            time.sleep(0.05)
        print("fsprobe exit", status, flush=True)
    finally:
        machine.close()
        os.chdir(cwd)
        shutil.rmtree(work, ignore_errors=True)
    if status != 0:
        return 1
    record = {"identity": {"tree": tree, "qemu": os.path.abspath(args.qemu),
                           "n": args.n},
              "phases": parse(lines_out, args.n)}
    with open(out, "w") as output:
        json.dump(record, output, indent=1, sort_keys=True)
    print("fs-probe: wrote %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
