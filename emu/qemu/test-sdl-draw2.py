#!/usr/bin/env python3
"""Boot an upstream SDL2 test program and observe its render submissions.

The image's test app is the program: testdraw2 by default, or any upstream
demo named with --program. Each must render, quit on Escape and exit clean.
"""

import argparse
import importlib.util
import os
import shutil
import sys
import tempfile
import time


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal_gate)


def display_count(machine, name):
    return machine.qmp.execute(
        "qom-get", {"path": "/machine", "property": name})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--program", default="testdraw2")
    arguments = parser.parse_args()
    program = arguments.program
    with tempfile.TemporaryDirectory(prefix="astra-sdl-draw2-") as temporary:
        image = os.path.join(temporary, "test.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal_gate.Machine(
            arguments.qemu, arguments.rom, image, temporary)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 30):
                trace = machine.trace()
                raise RuntimeError("Astra did not finish booting: serial=%r "
                                   "blits=%d trace=%r" %
                                   (machine.recent_serial(40),
                                    display_count(machine,
                                        "astra-display-blit-commands"),
                                    [line for line in trace if
                                     "pmmu_fault" not in line][-60:]))
            # TestDraw2's own drawing: every command its render-only
            # batches carry. A frame costs the display service no command
            # of its own (docs/MANAGED_GRAPHICS.md 5a), so blits would not
            # show TestDraw2 rendering.
            first = display_count(machine, "astra-display-render-commands")
            deadline = time.monotonic() + 10
            while (last := display_count(
                    machine, "astra-display-render-commands")) <= first + 100:
                if time.monotonic() >= deadline:
                    raise RuntimeError("upstream %s did not render" % program)
                time.sleep(0.1)
            before_escape = machine.trace_sequence()
            machine.qmp.key("esc")
            deadline = time.monotonic() + 10
            while True:
                trace = machine.trace()
                exits = []
                for line in trace:
                    record = terminal_gate.TRACE_RECORD.match(line)
                    if (record is None or int(record.group(1)) <= before_escape
                            or "process_exit" not in line):
                        continue
                    fields = line.split()
                    exits.append((int(fields[-3], 16), int(fields[-2], 16)))
                # Other processes exit too -- remote-desktop retries with no
                # host helper here -- so the test is a clean exit among
                # them and no faulting exit at all. The blit check below
                # proves the clean one stopped TestDraw2's drawing.
                # Reasons 3 and 4: user fault, signal (kernel process.h).
                if any(reason in (3, 4) for _, reason in exits):
                    raise RuntimeError("a process faulted after Escape: %r"
                                       % exits)
                if (0, 1) in exits:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError("Escape did not stop %s: %r" %
                                       (program, machine.recent_serial(30)))
                time.sleep(0.1)
            stopped = display_count(machine, "astra-display-render-commands")
            time.sleep(1)
            settled = display_count(machine, "astra-display-render-commands")
            if settled - stopped >= 100 or any(
                    "user fault" in line or "display window-command render "
                    "failed" in line for line in machine.recent_serial(100)):
                raise RuntimeError("SDL quit left rendering or faults: "
                                   "commands=%d/%d trace=%r" %
                                   (stopped, settled, trace[-20:]))
            print("SDL upstream %s QEMU: PASS (rendered, Escape, "
                  "clean exit; %d commands)" % (program, last - first))
        finally:
            machine.close()


if __name__ == "__main__":
    main()
