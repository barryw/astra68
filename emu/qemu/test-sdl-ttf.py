#!/usr/bin/env python3
"""Boot SDL_ttf with a real TTF and reject missing/corrupt fonts."""

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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-sdl-ttf-") as temporary:
        image = os.path.join(temporary, "test.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal_gate.Machine(
            arguments.qemu, arguments.rom, image, temporary)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting: %r" %
                                   machine.recent_serial(40))
            deadline = time.monotonic() + 45
            while time.monotonic() < deadline:
                trace = machine.trace()
                failures = [line for line in trace if "SDL_TTF_" in line and
                            "_FAIL" in line]
                if failures:
                    index = next(index for index, line in enumerate(trace)
                                 if "SDL_TTF_" in line and "_FAIL" in line)
                    raise RuntimeError("SDL_ttf probe failed: %r" %
                                       trace[max(0, index - 3):index + 10])
                if any("SDL_TTF_READY" in line for line in trace):
                    print("SDL_ttf QEMU: PASS (TTF render, missing/corrupt fonts)")
                    return
                time.sleep(0.1)
            raise RuntimeError("SDL_ttf probe timed out: %r" %
                               machine.recent_trace())
        finally:
            machine.close()


if __name__ == "__main__":
    main()
