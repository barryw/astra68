#!/usr/bin/env python3
"""Launches programs until storage dies, then reads the ring for why.

The block device's completion endpoint stops serving partway through a run:
`astra_irq_read` starts answering DEVICE_ERROR and never stops, so every later
transfer is refused and the volume is gone until reboot. It is timing
dependent, so it is reproduced by doing the thing that perturbs the timing --
launching programs -- rather than by asking for it directly.

The point of this probe is the discriminator. A quarantine taken on the
dispatch path emits KERNEL_TRACE_EVENT_IRQ_QUARANTINE naming its reason; the
three sites inside kernel_irq_ack that set the same sticky flag emit nothing.
So the ring says which half of irq.c to look in, and the trace ring is RAM and
survives the fault that killed storage.
"""

import argparse
import importlib.util
import os
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

import astra_image                                    # noqa: E402
import trace_decode                                   # noqa: E402

RING_ADDRESS = 0x020C4000
RING_SIZE = 0x10000
BOOT_MARKER = "stage 8"
# Where the shell says a transfer was refused, and how it says the device is
# gone for good. Either one ends the run. Read out of the trace ring, which is
# where the shell's output goes now that it draws glyphs into a window -- see
# the gate's own docstring.
DEAD = ("read stopped at", "I/O error")


def load_terminal_module():
    """test-terminal.py by path: its name is not an identifier."""
    path = os.path.join(HERE, "test-terminal.py")
    spec = importlib.util.spec_from_file_location("astra_terminal_gate", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--elf", default=os.path.join(
        ROOT, "sw/userspace/supervisor/build/m68k/astra_supervisor.elf"))
    parser.add_argument("--launches", type=int, default=24)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    arguments = parser.parse_args()

    gate = load_terminal_module()
    directory = tempfile.mkdtemp(prefix="astra-irq-")
    scratch = os.path.join(directory, "card.img")
    ring_path = os.path.join(directory, "ring.bin")
    import shutil
    shutil.copyfile(arguments.image, scratch)
    astra_image.install(scratch, arguments.catalog)
    # The same boot-before-the-run the gate does, because the last line of its
    # script reads the previous boot back and there has to be one. This probe
    # exists to reproduce the gate's timing, so it does what the gate does.
    if not gate.warm_the_store(arguments.qemu, arguments.rom, scratch,
                               directory, arguments.boot_deadline, 60.0):
        return 1

    machine = gate.Machine(arguments.qemu, arguments.rom, scratch, directory)
    died_at = None
    try:
        if not gate.open_terminal(machine, arguments.boot_deadline, 60.0):
            return 1

        # The gate's own script first. Launching alone does not do it: 24
        # consecutive `status` launches on a freshly booted machine all pass,
        # so whatever arms this is in what the gate does before it -- the
        # writes, the journal behind them, and the events reads.
        for line, expected in gate.SCRIPT:
            machine.settle()
            before = machine.sequence()
            machine.qmp.type_line(line)
            said, _ = machine.wait_for_text(expected, 60.0, before)
            if said is None:
                died_at = "script: %r" % line
                break
            if any(any(mark in text for mark in DEAD) for text in said):
                died_at = "script: %r" % line
                break

        # The step the gate actually dies on, which SCRIPT does not carry.
        if died_at is None:
            machine.settle()
            before = machine.sequence()
            machine.qmp.type_line("events --follow")
            if machine.wait_for_text("-- following", 60.0, before)[0] is None:
                died_at = "events --follow"
            else:
                machine.qmp.key("ret")
                machine.settle()

        if died_at is None:
            for index in range(1, arguments.launches + 1):
                machine.settle()
                before = machine.sequence()
                machine.qmp.type_line("status %d" % (index % 10))
                said, _ = machine.wait_for_text("exited %d" % (index % 10),
                                                20.0, before)
                if said is None:
                    died_at = index
                    break
                if any(any(mark in text for mark in DEAD) for text in said):
                    died_at = index
                    break

        print("=== launches before the device died: %s ===" % died_at)
        for text in machine.said()[0][-30:]:
            if text.strip():
                print("    |%s|" % text)

        time.sleep(1.0)
        reply = machine.qmp.monitor('pmemsave 0x%08x %d "%s"'
                                    % (RING_ADDRESS, RING_SIZE, ring_path))
        if reply and reply.strip():
            raise RuntimeError("pmemsave: %s" % reply.strip())
        with open(ring_path, "rb") as handle:
            ring = handle.read()
    finally:
        machine.process.kill()
        machine.process.wait()

    names = trace_decode.kernel_event_names(
        os.path.join(ROOT, "sw/kernel/trace.h"))
    header, lines = trace_decode.decode(ring, {}, names)
    print("=== ring: %d lines ===" % len(lines))
    interesting = [line for line in lines
                   if "IRQ" in line.upper() or "QUARANT" in line.upper()]
    for line in interesting[-60:]:
        print(line)
    if not interesting:
        print("no IRQ records in the ring at all")
    return 0 if died_at is None else 2


if __name__ == "__main__":
    sys.exit(main())
