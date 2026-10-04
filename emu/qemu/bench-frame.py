#!/usr/bin/env python3
"""Boot an image whose test app is SDLFrameBench and report its frame rate.

usage: bench-frame.py QEMU ROM IMAGE [--span S] [--profile OUT.aprof]
                      [--probe ADDR]... [--probe-register REG]
                      [--fake-helper] [--ready-renders N]
                      [--pointer-hz HZ]

The test app is SDLFrameBench, or with --ready-renders any app: measurement
starts once it has rendered N commands.
Prints every SDL_FRAME_BENCH line the benchmark logs during SPAN seconds,
plus the display device's submissions per second. With --profile, QEMU must
be the host-profile build (emu/qemu/build.sh host-profile); the guest is
profiled over the same span (report with tools/astra-prof report).

Without a mailbox, QEMU completes every display request after a fixed 1 ms,
which swamps what is being measured. --fake-helper (Linux) serves the real
mailbox with the display gate's stand-in helper, which completes each
request as soon as it arrives: transport and guest cost, no FPGA time.

--pointer-hz moves the pointer in a circle HZ times a second for the whole
span, as a hand on a mouse does: what pointer motion costs the frame rate.
"""

import argparse
import importlib.util
import os
import shutil
import subprocess
import sys
import math
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal_gate)
spec = importlib.util.spec_from_file_location(
    "astra_display_mailbox_gate", os.path.join(HERE, "test-display-mailbox.py"))
mailbox_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mailbox_gate)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--span", type=float, default=20.0)
    parser.add_argument("--profile")
    parser.add_argument("--fake-helper", action="store_true")
    parser.add_argument("--ready-renders", type=int, default=0,
                        help="wait for N render commands, not SDLFrameBench")
    parser.add_argument("--probe", action="append", default=[],
                        help="guest address whose callers --profile records")
    parser.add_argument("--probe-register",
                        help="register whose values each --probe records")
    parser.add_argument("--pointer-hz", type=float, default=0.0,
                        help="pointer motion events a second during the span")
    parser.add_argument("--pointer-center", default="640,360",
                        help="X,Y screen centre of the pointer's circle")
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-bench-frame-") as work:
        image = os.path.join(work, "bench.img")
        shutil.copyfile(arguments.image, image)
        extra = []
        control = os.path.join(work, "profile.sock")
        if arguments.profile:
            plugin = os.path.join(os.path.dirname(arguments.qemu),
                                  "contrib/plugins/libastra_profile.so")
            if os.path.exists(arguments.profile):
                os.unlink(arguments.profile)
            extra = ["-plugin", "%s,output=%s,control=%s,label=frame%s" %
                     (plugin, os.path.abspath(arguments.profile), control,
                      "".join(",probe=%s" % probe
                              for probe in arguments.probe) +
                      (",register=%s" % arguments.probe_register
                       if arguments.probe_register else ""))]
        helper = None
        if arguments.fake_helper:
            mailbox = os.path.join(work, "mailbox.bin")
            mailbox_gate.create_mailbox(mailbox)
            with open(mailbox, "r+b") as stream:
                view = mailbox_gate.mmap.mmap(stream.fileno(),
                                              mailbox_gate.HEADER_BYTES)
            helper = mailbox_gate.Helper(view)
            helper.thread.start()
            os.environ["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
            os.environ["ASTRA_DISPLAY_PAYLOAD_PATH"] = \
                mailbox_gate.payload_path(mailbox)
        machine = terminal_gate.Machine(arguments.qemu, arguments.rom, image,
                                        work, extra_args=extra)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting")
            count = lambda name: machine.qmp.execute(
                "qom-get", {"path": "/machine",
                            "property": "astra-display-" + name})
            if arguments.ready_renders:
                deadline = time.monotonic() + 90
                while count("render-commands") < arguments.ready_renders:
                    if time.monotonic() >= deadline:
                        raise RuntimeError("the test app did not render")
                    time.sleep(0.1)
            elif not machine.wait_for_trace_text("SDL_FRAME_BENCH_READY", 90):
                raise RuntimeError("SDLFrameBench did not start: %s" %
                                   machine.recent_trace())
            # Let the first measurement window pass before counting.
            time.sleep(3.0)
            after = machine.sequence()
            before = count("submissions")
            rendered_before = count("render-commands")
            prof = [sys.executable, os.path.join(ROOT, "tools/astra-prof"),
                    "control", control]
            if arguments.profile:
                subprocess.run(prof + ["start", "frame"], check=True)
            stop = threading.Event()
            mover = threading.Thread(target=move_pointer,
                                     args=(machine, arguments.pointer_hz,
                                           stop, tuple(int(v) for v in
                                           arguments.pointer_center.split(
                                               ","))))
            start = time.monotonic()
            if arguments.pointer_hz > 0:
                mover.start()
            time.sleep(arguments.span)
            stop.set()
            if mover.is_alive():
                mover.join()
            if arguments.profile:
                subprocess.run(prof + ["stop"], check=True)
            elapsed = time.monotonic() - start
            submitted = count("submissions") - before
            rendered = count("render-commands") - rendered_before
            for line in machine.said(after)[0]:
                if "SDL_FRAME_BENCH" in line or "FAIL" in line:
                    print(line)
            print("display submissions %.1f/s, render commands %.1f/s"
                  ", pointer %.0f Hz" %
                  (submitted / elapsed, rendered / elapsed,
                   arguments.pointer_hz))
        finally:
            machine.close()
            if helper is not None:
                helper.stopping = True
                helper.thread.join(timeout=1)
                helper.release()
                view.close()


def move_pointer(machine, hz, stop, center):
    """Absolute motion, in screen pixels, around CENTER until STOP."""
    period = 1.0 / hz
    due = time.monotonic()
    step = 0
    while not stop.is_set():
        angle = step * 0.05
        machine.qmp.point(int(center[0] + 60 * math.cos(angle)),
                          int(center[1] + 40 * math.sin(angle)))
        step += 1
        due += period
        delay = due - time.monotonic()
        if delay > 0:
            stop.wait(delay)


if __name__ == "__main__":
    main()
