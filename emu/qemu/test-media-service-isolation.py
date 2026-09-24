#!/usr/bin/env python3
"""A missing Linux audio provider must fail media, not the desktop."""

import argparse
import importlib.util
import os
import shutil
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "remote_service", os.path.join(HERE, "test-remote-desktop-service.py"))
remote = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(remote)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-media-isolation-") as root:
        image = os.path.join(root, "storage.img")
        shutil.copyfile(args.image, image)
        machine = remote.terminal.Machine(args.qemu, args.rom, image, root)
        try:
            if not remote.terminal.open_terminal(machine, 120.0, 30.0):
                raise RuntimeError("desktop/terminal did not survive media failure")
            deadline = time.monotonic() + 30.0
            for number in range(1, 100):
                lines = remote.command(machine, "service inspect media", None,
                                       30.0, number)
                if any("state: failed" in line for line in lines):
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError("media failure was not reported:\n" +
                                       "\n".join(lines + machine.trace()[-30:]))
                time.sleep(0.1)
            time.sleep(6.0)  # A retry must not publish a missing provider.
            remote.command(machine, "service inspect media", "state: failed",
                           30.0, 100)
            remote.command(machine, "print -r -- DESKTOP-ALIVE", "DESKTOP-ALIVE",
                           30.0, 101)
        finally:
            machine.close()
    print("ASTRA MEDIA ISOLATION PASS")


if __name__ == "__main__":
    main()
