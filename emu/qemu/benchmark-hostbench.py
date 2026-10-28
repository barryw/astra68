#!/usr/bin/env python3
"""Run the guest host-channel layer benchmark and print its measurements."""

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

PREFIXES = ("RAW USER ", "ADAPT ", "LAYER ", "MEM ")
PASS = "ASTRA RAW USER PASS"
FAIL = "ASTRA RAW USER FAIL"


def wait_for_result(machine, deadline):
    end = time.monotonic() + deadline
    lines = []
    after = 0

    while machine.process.poll() is None and time.monotonic() < end:
        try:
            added, after = machine.said(after)
        except terminal.trace_decode.TraceError:
            time.sleep(0.25)
            continue
        except (BrokenPipeError, EOFError, OSError, RuntimeError):
            break
        lines.extend(added)
        if any(PASS in line for line in lines):
            return lines
        if any(FAIL in line for line in lines):
            raise RuntimeError("hostbench failed\n%s" % "\n".join(lines[-40:]))
        time.sleep(0.25)
    machine.recent_serial()
    raise RuntimeError("hostbench did not finish\n%s" %
                       "\n".join(lines[-40:] + machine.log[-20:]))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--work", required=True)
    parser.add_argument("--hostfs-root")
    parser.add_argument("--deadline", type=float, default=300.0)
    parser.add_argument("--qemu-arg", action="append", default=[])
    args = parser.parse_args()

    os.makedirs(args.work, exist_ok=True)
    image = os.path.join(args.work, "hostbench.img")
    run = os.path.join(args.work, "run")
    os.makedirs(run, exist_ok=True)
    shutil.copyfile(args.image, image)
    machine = terminal.Machine(args.qemu, args.rom, image, run,
                               args.qemu_arg, hostfs_root=args.hostfs_root)
    try:
        lines = wait_for_result(machine, args.deadline)
        for line in lines:
            if line.startswith(PREFIXES) or PASS in line:
                print(line)
    finally:
        machine.close()


if __name__ == "__main__":
    main()
