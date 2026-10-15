#!/usr/bin/env python3
"""Time each stage of an Astra boot, so the boot path's cost is a number.

The standing instruction on this project is to profile everything so that
regressions are visible. The boot path had no such number: the emulator parks
in the terminal and never exits, so wall-clock-to-exit measures nothing, and
the one real latency defect found so far -- every blocking transfer costing a
full two-second lease timeout -- was invisible in aggregate and only showed up
when someone measured the interval between two specific events.

This measures the interval between stages, read off the serial stream itself.

If every stage comes back MISSED, suspect the ROM before the harness. A stale
`sw/boot/build/astra_boot.bin` boots as far as POST PASS and stops, which reads
exactly like a broken measurement and is not one -- `make` in sw/boot replaces
it.

What these numbers are: host time for a fixed workload, comparable between two
builds on the same machine. That is exactly what a regression check needs.

What they are not: physical MC68040 time. The CPU is TCG here and on the board
too, so no figure from either is a cycle count. A stage that doubles is worth
investigating on any machine; a stage that takes 40 ms is not thereby a claim
about hardware.
"""

import argparse
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time

import astra_image
from qemu_runtime import DEFAULT_MEMORY
import importlib.util

_SPEC = importlib.util.spec_from_file_location("terminal_gate", Path(__file__).with_name("test-terminal.py"))
terminal_gate = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(terminal_gate)

# In the order the boot emits them. Each is matched once, in sequence, so a
# marker that also appears in later output cannot be picked up twice.
MARKERS = [
    ("POST PASS", "POST"),
    ("Starting Axiom kernel", "kernel handoff"),
    ("Initial image ....... loaded", "user image loaded"),
    ("stage 8", "desktop up"),
]


def run_once(qemu, rom, image, memory, deadline, directory):
    """Serial observation timestamps start at the canonical provider launch."""
    machine = terminal_gate.Machine(qemu, rom, image, directory, memory=memory)
    reached = {}
    pending = list(MARKERS)
    end = time.monotonic() + deadline
    seen = 0
    try:
        while pending and machine.process.poll() is None and time.monotonic() < end:
            samples = machine.serial_times[seen:]
            seen += len(samples)
            for elapsed, line in samples:
                if pending and pending[0][0] in line:
                    _, label = pending.pop(0)
                    reached[label] = elapsed
            time.sleep(0.02)
    finally:
        machine.close()
    return reached


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="qemu-system-m68k carrying the astra68 machine")
    parser.add_argument("rom", help="astra_boot.bin")
    parser.add_argument("--image", required=True, help="canonical system image")
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG,
                        help="catalog installed with commands and services")
    parser.add_argument("--memory", default=DEFAULT_MEMORY)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--deadline", type=float, default=120.0,
                        help="give up on a run after this many seconds")
    parser.add_argument("--budget", type=float, default=0.0,
                        help="fail if 'desktop up' exceeds this many seconds")
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-boot-time-") as temporary:
        image = os.path.join(temporary, "card.img")
        shutil.copyfile(arguments.image, image)
        astra_image.install(image, arguments.catalog)
        runs = [run_once(arguments.qemu, arguments.rom, image,
                         arguments.memory, arguments.deadline, temporary)
                for _ in range(arguments.runs)]

    print("%-22s %8s %8s   %s" % ("stage", "median", "delta", "samples"))
    previous = 0.0
    missed = []
    terminal = None
    for _, label in MARKERS:
        values = sorted(run[label] for run in runs if label in run)
        if not values:
            print("%-22s %8s" % (label, "MISSED"))
            missed.append(label)
            continue
        median = values[len(values) // 2]
        print("%-22s %8.2f %8.2f   %s" %
              (label, median, median - previous,
               " ".join("%.2f" % value for value in values)))
        previous = median
        if label == "desktop up":
            terminal = median

    if missed:
        print("FAIL: never reached %s" % ", ".join(missed))
        return 1
    if arguments.budget > 0.0:
        if terminal is None:
            print("FAIL: no desktop to measure against the budget")
            return 1
        if terminal > arguments.budget:
            print("FAIL: desktop up at %.2fs exceeds the %.2fs budget"
                  % (terminal, arguments.budget))
            return 1
        print("within budget: %.2fs of %.2fs" % (terminal, arguments.budget))
    return 0


if __name__ == "__main__":
    sys.exit(main())
