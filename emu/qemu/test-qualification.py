#!/usr/bin/env python3
"""Boot the K1-K10 qualification kernel and read its verdict.

This is the only thing that binds a device interrupt end to end: it arms the
source, blocks on the endpoint with no deadline, reads the record, checks it
against what that device must report, consumes it, acknowledges it, and closes
the endpoint. Nothing in a normal boot does that for USB -- the desktop binds
storage, input and display -- so without this gate the USB interrupt path is
covered by nothing.

The kernel is a separate build (`make KERNEL_K1_QUALIFICATION=1` in sw/boot),
and it is not a machine anyone uses: it has no debug surface, it does not start
the initial user image, and the harness is the whole workload.

Two things are asserted:

  * the qualified mask is exactly what this machine can prove. A source that
    is present but cannot be provoked from inside the machine -- storage and
    input under the emulator, where nothing plays the part the AstraHost link
    played -- is left out of the mask rather than claimed, so the mask is the
    gate's subject rather than a detail. Pass the expected value with --mask.
  * every milestone marker the harness prints is present, K1 through K8, and
    the machine reaches KERNEL MULTITASKING.

A source that regresses does not produce a smaller mask: the harness fails the
record check and the process dies, the milestone never prints, and the gate
times out with the serial tail. Both halves are failures here.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time

# Bits 7, 8 and 9: USB, Vega and Astraea. Storage (4) and input (5) are
# present under the emulator and cannot be provoked from inside it.
DEFAULT_MASK = 0x380
IRQ_LINE = re.compile(r"Device IRQs \.+ K10 (PASS|partial), mask=0x([0-9A-Fa-f]{8})")
MARKERS = [
    "K1 PROTECTED ENTRY PASS",
    "K2 THREAD SUBSTRATE PASS",
    "K2 BLOCKING SUBSTRATE PASS",
    "K3 DEADLINE QUEUE PASS",
    "K3 ONE-SHOT SCHEDULER PASS",
    "K4 HANDLE SYNCHRONIZATION PASS",
    "K5 THREAD LIFECYCLE PASS",
    "K6 BOUNDED WAIT-MULTIPLE PASS",
    "K7 MESSAGE PORTS PASS",
    "K8 SHARED BULK IPC PASS",
    "K2 PERFORMANCE PASS",
    "KERNEL MULTITASKING",
]
LAST_MARKER = "KERNEL MULTITASKING"


def boot(qemu, rom, image, memory, deadline):
    """Boots once and returns every serial line, stopping at the last marker.

    The image is copied first. A qualification boot has no clean shutdown --
    the harness is still looping when the gate kills it -- and a shared image
    killed mid-run comes back dirty for whoever boots it next.
    """
    directory = tempfile.mkdtemp()
    try:
        scratch = os.path.join(directory, "card.img")
        shutil.copyfile(image, scratch)
        command = [qemu, "-M", "astra68", "-m", memory, "-bios", rom,
                   "-nographic", "-monitor", "none", "-serial", "stdio",
                   "-no-reboot",
                   "-drive", "if=none,format=raw,file=%s" % scratch]
        process = subprocess.Popen(
            command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)

        # The deadline has to kill the process rather than be checked in the
        # loop: a harness that dies leaves the machine idle and the next read
        # never returns, so a check that only runs per line never runs again.
        finished = threading.Event()

        def watchdog():
            if not finished.wait(deadline):
                process.kill()

        guard = threading.Thread(target=watchdog, daemon=True)
        guard.start()
        lines = []
        try:
            for line in process.stdout:
                lines.append(line.rstrip("\n"))
                if LAST_MARKER in line:
                    break
        finally:
            finished.set()
            process.kill()
            process.wait()
        return lines
    finally:
        shutil.rmtree(directory, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu", help="qemu-system-m68k carrying the astra68 machine")
    parser.add_argument("rom", help="astra_boot.bin built with KERNEL_K1_QUALIFICATION=1")
    parser.add_argument("--image", required=True,
                        help="storage image; copied, never booted in place")
    parser.add_argument("--memory", default="128M")
    parser.add_argument("--mask", default=hex(DEFAULT_MASK),
                        help="IRQ sources this machine must qualify")
    parser.add_argument("--deadline", type=float, default=120.0)
    arguments = parser.parse_args()

    expected = int(arguments.mask, 0)
    started = time.monotonic()
    lines = boot(arguments.qemu, arguments.rom, arguments.image,
                 arguments.memory, arguments.deadline)
    elapsed = time.monotonic() - started

    failures = []
    text = "\n".join(lines)
    for marker in MARKERS:
        if marker not in text:
            failures.append("missing: %s" % marker)

    match = IRQ_LINE.search(text)
    if match is None:
        failures.append("the harness never reported its device IRQ mask")
    else:
        qualified = int(match.group(2), 16)
        print("K10 %s, mask=0x%08x in %.1fs" %
              (match.group(1), qualified, elapsed))
        if qualified != expected:
            failures.append("qualified 0x%08x, expected 0x%08x"
                            % (qualified, expected))

    if failures:
        for failure in failures:
            print("FAIL: %s" % failure, file=sys.stderr)
        print("--- last serial ---", file=sys.stderr)
        for line in lines[-20:]:
            print(line, file=sys.stderr)
        return 1
    print("ASTRA QUALIFICATION PASS %d markers" % len(MARKERS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
