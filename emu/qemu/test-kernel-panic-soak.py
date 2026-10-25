#!/usr/bin/env python3
"""Keep a disposable full Astra boot alive through service retry churn."""

import argparse
import importlib.util
import os
import shutil
import tempfile
import time


HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)


def run(qemu, rom, image, seconds):
    with tempfile.TemporaryDirectory(prefix="astra-panic-soak-") as temporary:
        scratch = os.path.join(temporary, "card.img")
        shutil.copyfile(image, scratch)
        machine = terminal.Machine(qemu, rom, scratch, temporary)
        try:
            if not machine.wait_for_serial(terminal.BOOT_MARKER, 120.0):
                raise RuntimeError("stage 8 missing")
            start = time.monotonic()
            next_report = start + 30.0
            next_sample = start
            sequence = machine.sequence()
            retries = 0
            while time.monotonic() - start < seconds:
                serial = machine.recent_serial()
                if machine.process.poll() is not None or any(
                    "KERNEL PANIC" in line or "SYSTEM HALTED" in line
                    for line in serial
                ):
                    raise RuntimeError("guest halted during soak")
                if time.monotonic() >= next_sample:
                    lines, sequence = machine.said(sequence)
                    retries += sum("launch-failed /services/remote-desktop"
                                   in line for line in lines)
                    next_sample += 5.0
                if time.monotonic() >= next_report:
                    print("PANIC SOAK elapsed=%d remote-failures=%d" %
                          (time.monotonic() - start, retries), flush=True)
                    next_report += 30.0
                time.sleep(1.0)
            lines, _ = machine.said(sequence)
            retries += sum("launch-failed /services/remote-desktop" in line
                           for line in lines)
            if retries == 0:
                raise RuntimeError("remote-desktop retry workload did not run")
            print("ASTRA PANIC SOAK PASS seconds=%d remote-failures=%d" %
                  (seconds, retries))
            return 0
        except Exception:
            print("last serial lines:")
            for line in machine.recent_serial(50):
                print("    %s" % line)
            print("last trace records:")
            for line in machine.recent_trace(50):
                print("    %s" % line)
            print("last syslog lines:")
            for line in machine.syslog_lines()[-50:]:
                print("    %s" % line)
            raise
        finally:
            machine.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image", help="prepared image; copied before launch")
    parser.add_argument("--seconds", type=int, default=600)
    arguments = parser.parse_args()
    if arguments.seconds < 1:
        parser.error("--seconds must be positive")
    return run(arguments.qemu, arguments.rom, arguments.image,
               arguments.seconds)


if __name__ == "__main__":
    raise SystemExit(main())
