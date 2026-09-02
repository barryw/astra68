#!/usr/bin/env python3
"""Measure the bare MC68030-to-QEMU AstraHost channel without Axiom."""

import argparse
import importlib.util
import os
import queue
import re
import subprocess
import tempfile
import threading
import time


HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)

RESULT = re.compile(
    r"^RAW (empty|command) depth=([0-9a-f]{8}) "
    r"iterations=([0-9a-f]{8}) elapsed-us=([0-9a-f]{8})$")
PROPERTIES = (
    "astra-host-submissions",
    "astra-host-commands",
    "astra-host-execution-ns",
    "astra-host-inflight",
    "astra-host-max-inflight",
)


def run(qemu, rom, expected_mode, deadline):
    with tempfile.TemporaryDirectory(prefix="astra-raw-host-") as directory:
        qmp_path = os.path.join(directory, "qmp.sock")
        hostfs = os.path.join(directory, "hostfs")
        os.mkdir(hostfs)
        environment = os.environ.copy()
        environment["ASTRA_HOSTFS_ROOT"] = hostfs
        process = subprocess.Popen([
            qemu, "-M", "astra68", "-m", "128M", "-bios", rom,
            "-display", "none", "-monitor", "none", "-serial", "stdio",
            "-no-reboot", "-qmp", "unix:%s,server=on,wait=off" % qmp_path,
        ], stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1, env=environment)
        lines = queue.Queue()

        def pump():
            for line in process.stdout:
                lines.put(line.rstrip("\n"))
            lines.put(None)

        thread = threading.Thread(target=pump, daemon=True)
        thread.start()
        qmp = None
        output = []
        result = None
        try:
            qmp = terminal.Qmp(qmp_path, deadline=deadline)
            end = time.monotonic() + deadline
            while time.monotonic() < end:
                try:
                    line = lines.get(timeout=0.1)
                except queue.Empty:
                    if process.poll() is not None:
                        break
                    continue
                if line is None:
                    break
                output.append(line)
                if line.startswith("ASTRA RAW HOST FAIL"):
                    raise RuntimeError("raw host benchmark failed\n%s" %
                                       "\n".join(output[-40:]))
                match = RESULT.match(line)
                if match is not None:
                    result = match
                if line == "ASTRA RAW HOST PASS":
                    break
            if (result is None or not output or
                    output[-1] != "ASTRA RAW HOST PASS"):
                raise RuntimeError("raw host benchmark failed\n%s" %
                                   "\n".join(output[-40:]))
            mode, depth_hex, iterations_hex, elapsed_hex = result.groups()
            if mode != expected_mode:
                raise RuntimeError("expected %s result, got %s" %
                                   (expected_mode, mode))
            values = {name: qmp.execute("qom-get", {
                "path": "/machine", "property": name,
            }) for name in PROPERTIES}
        finally:
            if qmp is not None:
                try:
                    qmp.execute("quit")
                except (BrokenPipeError, EOFError, OSError, RuntimeError):
                    pass
                qmp.close()
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            thread.join(timeout=1)

    iterations = int(iterations_hex, 16)
    depth = int(depth_hex, 16)
    elapsed_us = int(elapsed_hex, 16)
    expected_commands = iterations if expected_mode == "command" else 0
    expected_inflight = 0 if depth <= 1 else depth
    if (iterations == 0 or elapsed_us == 0 or
            values["astra-host-commands"] != expected_commands or
            values["astra-host-inflight"] != 0 or
            (expected_mode == "empty" and
             (depth != 0 or
             (values["astra-host-submissions"] != 0 or
              values["astra-host-max-inflight"] != 0))) or
            (expected_mode == "command" and
             (depth == 0 or values["astra-host-submissions"] == 0 or
              values["astra-host-submissions"] > iterations or
              values["astra-host-max-inflight"] != expected_inflight))):
        raise RuntimeError("invalid %s counters: %r" %
                           (expected_mode, values))
    elapsed_ns = elapsed_us * 1000
    callback_ns = values["astra-host-execution-ns"]
    submissions = values["astra-host-submissions"]
    print("raw-host: %s depth=%d submissions=%d commands=%d max-inflight=%d "
          "elapsed-ns=%d ns-per-iteration=%.1f iterations-per-second=%.1f "
          "callback-ns-per-command=%.1f" % (
              expected_mode, depth, submissions, values["astra-host-commands"],
              values["astra-host-max-inflight"], elapsed_ns,
              elapsed_ns / iterations, iterations * 1_000_000_000 / elapsed_ns,
              callback_ns / iterations), flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("empty_rom")
    parser.add_argument("command_rom")
    parser.add_argument("--deadline", type=float, default=30.0)
    arguments = parser.parse_args()
    if arguments.deadline <= 0:
        parser.error("--deadline must be positive")
    run(arguments.qemu, arguments.empty_rom, "empty", arguments.deadline)
    run(arguments.qemu, arguments.command_rom, "command", arguments.deadline)
    print("ASTRA RAW HOST CHANNEL PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
