#!/usr/bin/env python3
"""Boot the SDL render probe and require texture-engine work and readback.

The probe (sw/userspace/sdl2/tests/render_probe.c) draws through every
texture-engine path of the astra renderer and reads back a target and the
window. Host-only QEMU renders nothing, so this gate checks what reached the
display device: TRIANGLES commands in render-only batches, completed surface
reads, and no failed submission.
"""

import argparse
import importlib.util
import os
import shutil
import sys
import tempfile


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal_gate)

# ARGB target readback, then the window read twice (native and converted).
EXPECTED_SURFACE_READS = 3


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-sdl-render-") as temporary:
        image = os.path.join(temporary, "test.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal_gate.Machine(
            arguments.qemu, arguments.rom, image, temporary)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting")
            if not machine.wait_for_trace_text("SDL_RENDER_READY", 60):
                raise RuntimeError("SDL render probe did not finish: %s" %
                                   machine.recent_trace())

            def display_count(name):
                return machine.qmp.execute(
                    "qom-get", {"path": "/machine", "property": name})

            triangles = display_count("astra-display-triangle-commands")
            reads = display_count("astra-display-surface-reads")
            render_only = display_count("astra-display-render-only-batches")
            blits = display_count("astra-display-blit-commands")
            fills = display_count("astra-display-fill-commands")
            submissions = display_count("astra-display-submissions")
            completions = display_count("astra-display-completions")
            if triangles < 3 or reads != EXPECTED_SURFACE_READS or \
                    render_only == 0 or submissions != completions:
                raise RuntimeError(
                    "SDL render probe work: triangles=%d reads=%d "
                    "render_only=%d submissions=%d completions=%d" %
                    (triangles, reads, render_only, submissions,
                     completions))
            print("SDL render QEMU: PASS (%d triangle commands, %d surface "
                  "reads, %d render-only batches, %d blits, %d fills, "
                  "%d/%d submissions completed)" %
                  (triangles, reads, render_only, blits, fills, completions,
                   submissions))
        finally:
            machine.close()


if __name__ == "__main__":
    main()
