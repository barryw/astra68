#!/usr/bin/env python3
"""Boot the SDL render probe and require hardware drawing and indexed blits.

The probe (sw/userspace/sdl2/tests/render_probe.c) draws through every
texture-engine path of the astra renderer, reads back a target and the
window, then presents an INDEX8 window with two palettes. Host-only QEMU
renders nothing, so this gate checks commands reaching the display device.
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

            def display_count(name):
                return machine.qmp.execute(
                    "qom-get", {"path": "/machine", "property": name})

            deadline = time.monotonic() + 15
            while True:
                triangles = display_count("astra-display-triangle-commands")
                reads = display_count("astra-display-surface-reads")
                palette_blits = display_count(
                    "astra-display-palette-blit-commands")
                render_only = display_count(
                    "astra-display-render-only-batches")
                blits = display_count("astra-display-blit-commands")
                fills = display_count("astra-display-fill-commands")
                submissions = display_count("astra-display-submissions")
                completions = display_count("astra-display-completions")
                if (triangles >= 3 and reads >= EXPECTED_SURFACE_READS and
                        palette_blits >= 2 and submissions == completions) or \
                        time.monotonic() >= deadline:
                    break
                time.sleep(0.05)
            root = machine.reader.hostfs_root
            visible_logs = root is not None and os.access(
                root, os.R_OK | os.X_OK)
            syslog = terminal_gate.read_host_syslog(root) if visible_logs else ""
            if visible_logs:
                marker_deadline = time.monotonic() + 5
                while "SDL_INDEXED_READY" not in syslog and \
                        time.monotonic() < marker_deadline:
                    time.sleep(0.05)
                    syslog = terminal_gate.read_host_syslog(root)
            if triangles < 3 or reads != EXPECTED_SURFACE_READS or \
                    palette_blits < 2 or render_only == 0 or \
                    submissions != completions or \
                    (visible_logs and "SDL_INDEXED_READY" not in syslog):
                markers = [line for line in syslog.splitlines()
                           if "SDL_" in line or "render-probe" in line]
                raise RuntimeError(
                    "SDL render probe work: triangles=%d reads=%d "
                    "palette_blits=%d render_only=%d "
                    "submissions=%d completions=%d markers=%s" %
                    (triangles, reads, palette_blits, render_only, submissions,
                     completions, markers[-12:]))
            print("SDL render QEMU: PASS (%d triangle commands, %d surface "
                  "reads, %d palette blits, %d render-only batches, "
                  "%d blits, %d fills, "
                  "%d/%d submissions completed)" %
                  (triangles, reads, palette_blits, render_only, blits,
                   fills, completions, submissions))
        finally:
            machine.close()


if __name__ == "__main__":
    main()
