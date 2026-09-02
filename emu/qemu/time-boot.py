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

What they are not: 68030 time. The CPU is TCG here and it is TCG on the board
too, so no figure from either is a cycle count. A stage that doubles is worth
investigating on any machine; a stage that takes 40 ms is not thereby a claim
about hardware.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time

import astra_image

# In the order the boot emits them. Each is matched once, in sequence, so a
# marker that also appears in later output cannot be picked up twice.
MARKERS = [
    ("POST PASS", "POST"),
    ("Starting Axiom kernel", "kernel handoff"),
    ("Initial image ....... loaded", "user image loaded"),
    ("block round-trip verified", "block round-trip"),
    ("volume found in the partition table", "partition read"),
    ("volume mounted, journal recovered", "mount + journal"),
    ("volume verified, written and re-read", "volume verify"),
    ("stage 8", "terminal up"),
]


def run_once(qemu, rom, image, memory, deadline):
    """Boots once and returns {label: seconds since launch}."""
    command = [qemu, "-M", "astra68", "-m", memory, "-bios", rom,
               "-nographic", "-monitor", "none", "-serial", "stdio",
               "-no-reboot"]
    if image is not None:
        command += ["-drive", "if=none,format=raw,file=%s" % image]

    process = subprocess.Popen(
        command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1)

    start = time.monotonic()
    reached = {}
    pending = list(MARKERS)

    # The deadline has to be enforced by killing the process, not by checking
    # the clock in the loop: booting without media legitimately goes quiet
    # after the block lease and parks, so the next read never returns and a
    # check that only runs per line never runs again.
    finished = threading.Event()

    def watchdog():
        if not finished.wait(deadline):
            process.kill()

    guard = threading.Thread(target=watchdog, daemon=True)
    guard.start()

    try:
        for line in process.stdout:
            if pending[0][0] in line:
                _, label = pending.pop(0)
                reached[label] = time.monotonic() - start
                # Break here rather than at the top of the next iteration:
                # the last marker is the last line the boot emits, and the
                # service is resident, so waiting for one more line hangs.
                if not pending:
                    break
    finally:
        # The service is resident by design and will not exit on its own.
        finished.set()
        process.kill()
        process.wait()
    return reached


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="qemu-system-m68k carrying the astra68 machine")
    parser.add_argument("rom", help="astra_boot.bin")
    parser.add_argument("--image", help="card image; omit to boot without media")
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG,
                        help="catalog installed with commands and services")
    parser.add_argument("--memory", default="128M")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--deadline", type=float, default=120.0,
                        help="give up on a run after this many seconds")
    parser.add_argument("--budget", type=float, default=0.0,
                        help="fail if 'terminal up' exceeds this many seconds")
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-boot-time-") as temporary:
        image = arguments.image
        if image is not None:
            scratch = os.path.join(temporary, "card.img")
            shutil.copyfile(image, scratch)
            astra_image.install(scratch, arguments.catalog)
            image = scratch
        runs = [run_once(arguments.qemu, arguments.rom, image,
                         arguments.memory, arguments.deadline)
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
        if label == "terminal up":
            terminal = median

    # Booting without media legitimately stops after the user image comes up.
    if missed and arguments.image is not None:
        print("FAIL: never reached %s" % ", ".join(missed))
        return 1
    if arguments.budget > 0.0:
        if terminal is None:
            print("FAIL: no terminal to measure against the budget")
            return 1
        if terminal > arguments.budget:
            print("FAIL: terminal up at %.2fs exceeds the %.2fs budget"
                  % (terminal, arguments.budget))
            return 1
        print("within budget: %.2fs of %.2fs" % (terminal, arguments.budget))
    return 0


if __name__ == "__main__":
    sys.exit(main())
