#!/usr/bin/env python3
"""Boot native Codec Kit gate with isolated host decoder worker."""

import argparse
import filecmp
import importlib.util
import os
import re
import shutil
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--worker", required=True)
    parser.add_argument("--work", required=True)
    parser.add_argument("--deadline", type=float, default=120.0)
    args = parser.parse_args()
    worker = os.path.abspath(args.worker)
    installed = "/usr/local/libexec/astra-codec-worker"
    if os.geteuid() != 0 and (not os.path.isfile(installed) or
                                not filecmp.cmp(worker, installed,
                                                shallow=False)):
        raise RuntimeError("installed Codec worker differs from %s" % worker)

    os.makedirs(args.work, exist_ok=True)
    image = os.path.join(args.work, "codec.img")
    shutil.copyfile(args.image, image)
    environment = os.environ.copy()
    environment["ASTRA_CODEC_WORKER_PATH"] = worker
    machine = terminal.Machine(args.qemu, args.rom, image, args.work,
                               environment=environment)
    try:
        end = time.monotonic() + args.deadline
        progress = []
        certifier_pid = None
        while machine.process.poll() is None and time.monotonic() < end:
            try:
                trace = machine.trace_with_syslog()
            except terminal.trace_decode.TraceError:
                time.sleep(0.25)
                continue
            for index, line in enumerate(trace):
                if "codec" in line.lower() and line not in progress:
                    progress.append(line)
                match = re.search(r"info\s+([0-9a-fA-F]{8})/0 "
                                  r"/services/codec-certify", line)
                if match:
                    certifier_pid = int(match.group(1), 16)
                match = re.search(r"process_exit\s+0x([0-9a-fA-F]{8}) "
                                  r"0x([0-9a-fA-F]{8})", line)
                if match and certifier_pid == int(match.group(1), 16):
                    if int(match.group(2), 16) != 0:
                        raise RuntimeError("Codec certifier failed: %r" %
                                           trace[max(0, index - 35):index + 8])
                    print("Codec Kit QEMU: PASS (WAV, video, seek, close)")
                    return
            time.sleep(0.25)
        raise RuntimeError("Codec Kit guest gate timed out: progress=%r "
                           "tail=%r" % (progress[-25:],
                                        machine.trace_with_syslog()[-20:]))
    finally:
        machine.close()


if __name__ == "__main__":
    main()
