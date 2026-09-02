#!/usr/bin/env python3
"""Measure the steady-state userspace MC68030 host-channel ceiling."""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile
import time

sys.dont_write_bytecode = True


HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)
HOTPLUG_SPEC = importlib.util.spec_from_file_location(
    "astra_input_hotplug", os.path.join(HERE, "astra-input-hotplug.py"))
hotplug = importlib.util.module_from_spec(HOTPLUG_SPEC)
HOTPLUG_SPEC.loader.exec_module(hotplug)

RESULT = re.compile(
    r"RAW USER depth=([0-9a-f]{8}) iterations=([0-9a-f]{8}) "
    r"elapsed-ns=([0-9a-f]{16})")
LAYER_RESULT = re.compile(
    r"LAYER name=([a-z-]+) iterations=([0-9a-f]{8}) "
    r"elapsed-ns=([0-9a-f]{16})")
ADAPT_RESULT = re.compile(
    r"ADAPT polls=([0-9a-f]{8}) iterations=([0-9a-f]{8}) "
    r"misses=([0-9a-f]{8}) elapsed-ns=([0-9a-f]{16})")
MEMORY_RESULT = re.compile(
    r"MEM name=([a-z]+) bytes=([0-9a-f]{16}) "
    r"elapsed-ns=([0-9a-f]{16})")
PROPERTIES = (
    "astra-host-submissions",
    "astra-host-commands",
    "astra-host-execution-ns",
    "astra-host-inflight",
    "astra-host-max-inflight",
)


def run(arguments):
    with tempfile.TemporaryDirectory(
            prefix="astra-user-host-", dir=arguments.work) as directory, \
         tempfile.TemporaryDirectory(
            prefix="astra-user-hostfs-", dir=arguments.host_work) as hostfs:
        image = os.path.join(directory, "storage.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal.Machine(arguments.qemu, arguments.rom, image,
                                   directory, hostfs_root=hostfs)
        output = []
        results = []
        layers = []
        adaptive = []
        memory = []
        passed = False
        try:
            if arguments.tune_runtime and not hotplug.tune_runtime(machine.qmp):
                raise RuntimeError("QEMU runtime tuning failed")
            if not machine.wait_for_serial(terminal.BOOT_MARKER,
                                           arguments.deadline):
                raise RuntimeError("boot did not reach stage 8")
            end = time.monotonic() + arguments.deadline
            after = 0
            while time.monotonic() < end:
                if machine.process.poll() is not None:
                    break
                lines, after = machine.said(after)
                for line in lines:
                    output.append(line)
                    if "ASTRA RAW USER FAIL" in line:
                        raise RuntimeError(
                            "userspace host benchmark failed\n%s" %
                            "\n".join(output[-60:]))
                    match = RESULT.search(line)
                    if match is not None:
                        results.append(tuple(int(value, 16)
                                             for value in match.groups()))
                    match = LAYER_RESULT.search(line)
                    if match is not None:
                        layers.append((match.group(1),
                                       int(match.group(2), 16),
                                       int(match.group(3), 16)))
                    match = ADAPT_RESULT.search(line)
                    if match is not None:
                        adaptive.append(tuple(int(value, 16)
                                              for value in match.groups()))
                    match = MEMORY_RESULT.search(line)
                    if match is not None:
                        memory.append((match.group(1),
                                       int(match.group(2), 16),
                                       int(match.group(3), 16)))
                    if "ASTRA RAW USER PASS" in line:
                        passed = True
                if passed:
                    break
                time.sleep(0.1)
            if not passed:
                raise RuntimeError("userspace host benchmark timed out\n%s" %
                                   "\n".join(output[-60:]))
            values = {name: machine.qmp.execute("qom-get", {
                "path": "/machine", "property": name,
            }) for name in PROPERTIES}
        except Exception as error:
            raise RuntimeError("host benchmark failed (%s)\n%s\n%s" %
                               (error, "\n".join(output[-60:]),
                                "\n".join(machine.recent_serial()))) from error
        finally:
            machine.close()

    depths = [result[0] for result in results]
    expected_depths = []
    depth = 1
    while depth <= depths[-1] if depths else False:
        expected_depths.append(depth)
        depth <<= 1
    if (not results or depths != expected_depths or
            any(iterations == 0 or elapsed == 0
                for _, iterations, elapsed in results) or
            [name for name, _, _ in layers] != [
                "transport-unsupported", "transport-stat", "backend-stat",
                "direct-vfs-stat", "path-normalise",
                "assign-member-hit", "assign-member-miss", "assign-resolve",
                "filesystem-stat",
                "direct-backend-open-close-paired",
                "direct-vfs-open-close-paired",
                "filesystem-direct-open-close-paired"] or
            any(iterations == 0 or elapsed == 0
                for _, iterations, elapsed in layers) or
            not adaptive or
            [polls for polls, _, _, _ in adaptive] != [
                1 << shift for shift in range(17)] or
            any(iterations == 0 or misses > iterations or elapsed == 0
                for _, iterations, misses, elapsed in adaptive) or
            [name for name, _, _ in memory] != ["memcpy", "memcmp"] or
            any(byte_count == 0 or elapsed == 0
                for _, byte_count, elapsed in memory) or
            values["astra-host-commands"] !=
            sum(iterations for _, iterations, _ in results) +
            sum((iterations + 1) *
                (0 if name.startswith(("path-", "assign-")) else
                 2 if name.endswith(("open-close", "open-close-paired"))
                 else 1)
                for name, iterations, _ in layers) + 2 +
            sum(iterations for _, iterations, _, _ in adaptive) or
            values["astra-host-inflight"] != 0 or
            values["astra-host-max-inflight"] != depths[-1]):
        raise RuntimeError("invalid userspace host results: %r %r" %
                           (results, values))
    for name, byte_count, elapsed in memory:
        print("memory: name=%s bytes=%d elapsed-ns=%d bytes-per-second=%.1f" % (
            name, byte_count, elapsed,
            byte_count * 1_000_000_000 / elapsed), flush=True)
    for depth, iterations, elapsed in results:
        print("raw-user: depth=%d iterations=%d elapsed-ns=%d "
              "ns-per-request=%.1f requests-per-second=%.1f" % (
                  depth, iterations, elapsed, elapsed / iterations,
                  iterations * 1_000_000_000 / elapsed), flush=True)
    for name, iterations, elapsed in layers:
        print("layer: name=%s iterations=%d elapsed-ns=%d "
              "ns-per-operation=%.1f operations-per-second=%.1f" % (
                  name, iterations, elapsed, elapsed / iterations,
                  iterations * 1_000_000_000 / elapsed), flush=True)
    for polls, iterations, misses, elapsed in adaptive:
        print("adaptive: polls=%d iterations=%d misses=%d hit-rate=%.1f%% "
              "ns-per-request=%.1f requests-per-second=%.1f" % (
                  polls, iterations, misses,
                  (iterations - misses) * 100.0 / iterations,
                  elapsed / iterations,
                  iterations * 1_000_000_000 / elapsed), flush=True)
    print("raw-user: submissions=%d commands=%d max-inflight=%d "
          "host-execution-ns=%d" % (
              values["astra-host-submissions"],
              values["astra-host-commands"],
              values["astra-host-max-inflight"],
              values["astra-host-execution-ns"]), flush=True)
    print("ASTRA RAW USERSPACE HOST CHANNEL PASS")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--work")
    parser.add_argument("--host-work")
    parser.add_argument("--tune-runtime", action="store_true")
    parser.add_argument("--deadline", type=float, default=180.0)
    arguments = parser.parse_args()
    if arguments.deadline <= 0:
        parser.error("--deadline must be positive")
    if arguments.work is not None:
        os.makedirs(arguments.work, exist_ok=True)
    if arguments.host_work is not None:
        os.makedirs(arguments.host_work, exist_ok=True)
    run(arguments)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
