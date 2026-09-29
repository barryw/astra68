#!/usr/bin/env python3
"""Boot an SDL test app and require a completed display submission."""

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-sdl-video-") as temporary:
        image = os.path.join(temporary, "test.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal_gate.Machine(
            arguments.qemu, arguments.rom, image, temporary)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting")
            if not machine.wait_for_trace_text("SDL_VIDEO_READY", 60):
                raise RuntimeError("SDL app did not present: %s" %
                                   machine.recent_trace())
            def display_count(name):
                return machine.qmp.execute(
                    "qom-get", {"path": "/machine", "property": name})

            batches = display_count("astra-display-render-batches")
            blits = display_count("astra-display-blit-commands")
            submissions = display_count("astra-display-submissions")
            completions = display_count("astra-display-completions")
            if not batches or not blits or not submissions or not completions:
                raise RuntimeError("SDL app did not submit display work: "
                                   "batches=%d blits=%d submissions=%d "
                                   "completions=%d" %
                                   (batches, blits, submissions, completions))
            print("SDL video QEMU: PASS (window present; %d batches, %d blits, "
                  "%d/%d submissions completed)" %
                  (batches, blits, completions, submissions))
        finally:
            machine.close()


if __name__ == "__main__":
    main()
