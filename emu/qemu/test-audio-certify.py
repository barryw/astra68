#!/usr/bin/env python3
"""Boot the audio test image and check its guest-side result."""

import argparse
import importlib.util
import os
import shutil
import sys
import time

sys.dont_write_bytecode = True
HERE = os.environ.get("ASTRA_QEMU_TEST_ROOT",
                      os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--work", required=True)
    parser.add_argument("--hostfs-root")
    parser.add_argument("--deadline", type=float, default=120.0)
    parser.add_argument("--expect-no-provider", action="store_true")
    args = parser.parse_args()

    os.makedirs(args.work, exist_ok=True)
    image = os.path.join(args.work, "audio.img")
    shutil.copyfile(args.image, image)
    machine = terminal.Machine(args.qemu, args.rom, image, args.work,
                               hostfs_root=args.hostfs_root)
    try:
        end = time.monotonic() + args.deadline
        after = 0
        expected = ("ASTRA GUEST AUDIO FAIL stage=00000002 detail=00000010"
                    if args.expect_no_provider else "ASTRA GUEST AUDIO PASS")
        while machine.process.poll() is None and time.monotonic() < end:
            try:
                lines, after = machine.said(after)
            except terminal.trace_decode.TraceError:
                time.sleep(0.25)
                continue
            for line in lines:
                if "ASTRA GUEST AUDIO" in line:
                    print(line)
                    if expected in line:
                        return
                    raise RuntimeError("unexpected guest audio result")
            time.sleep(0.25)
        machine.recent_serial()
        raise RuntimeError("guest audio result not seen: " +
                           "\n".join(machine.log[-20:]))
    finally:
        machine.close()


if __name__ == "__main__":
    main()
