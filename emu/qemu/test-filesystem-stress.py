#!/usr/bin/env python3
"""Run the modeled filesystem stress workload through the production stack."""

import argparse
import importlib.util
import os
import queue
import signal
import shutil
import subprocess
import sys
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

BLOCK_PROPERTIES = (
    "astra-block-read-requests",
    "astra-block-read-sectors",
    "astra-block-write-requests",
    "astra-block-write-sectors",
    "astra-block-flush-requests",
)
PMMU_PROPERTIES = (
    "astra-pmmu-tlb-fills",
    "astra-pmmu-atc-hits",
    "astra-pmmu-table-walks",
    "astra-pmmu-crp-writes",
    "astra-pmmu-crp-changes",
)
HOST_PROPERTIES = (
    "astra-host-submissions",
    "astra-host-commands",
    "astra-host-execution-ns",
)
HOST_GAUGE_PROPERTIES = (
    "astra-host-inflight",
    "astra-host-max-inflight",
)
HOST_OPERATION_PROPERTIES = tuple(
    "astra-host-fs-" + name for name in (
        "invalid", "open", "close", "read", "write", "sync", "truncate",
        "stat", "readdir", "mkdir", "unlink", "rename", "chmod",
        "readlink", "symlink"))


def wait_serial(machine, marker):
    while True:
        try:
            line = machine.serial.get(timeout=0.2)
        except queue.Empty:
            if machine.process.poll() is not None:
                return False
            continue
        if line is None:
            return False
        machine.log.append(line)
        if marker in line:
            return True


def wait_text(machine, needles, after, deadline=None, on_lines=None):
    needles = (needles,) if isinstance(needles, str) else tuple(needles)
    collected = []
    while machine.process.poll() is None:
        if deadline is not None and time.monotonic() >= deadline:
            raise TimeoutError("command deadline expired\n%s" %
                               "\n".join(collected[-40:]))
        lines, highest = machine.said(after)
        if lines:
            collected.extend(lines)
            if on_lines is not None:
                on_lines(lines)
        if any(any(needle in line for line in collected)
               for needle in needles):
            return collected, highest
        after = highest
        time.sleep(0.25)
    return None, after


def settle(machine):
    last = machine.sequence()
    while machine.process.poll() is None:
        time.sleep(0.4)
        current = machine.sequence()
        if current == last:
            return
        last = current
    raise RuntimeError("QEMU exited while Terminal was settling")


def open_terminal(machine):
    if not wait_serial(machine, terminal.BOOT_MARKER):
        return False
    time.sleep(2.0)
    machine.qmp.double_click(*terminal.TERMINAL_ICON)
    if wait_text(machine, terminal.BANNER, 0)[0] is None:
        return False
    settle(machine)
    return True


def machine_stats(machine):
    machine.qmp.execute("stop")
    try:
        return {
            name: machine.qmp.execute("qom-get", {
                "path": "/machine", "property": name,
            }) for name in BLOCK_PROPERTIES + PMMU_PROPERTIES +
            HOST_PROPERTIES + HOST_GAUGE_PROPERTIES +
            HOST_OPERATION_PROPERTIES
        }
    finally:
        machine.qmp.execute("cont")


def independent_fsck(e2fsck, image, volume):
    offset, length = terminal.astra_image.ext4_partition(image)
    terminal.astra_image._slice(image, offset, length, volume)
    repaired = subprocess.run([e2fsck, "-fy", volume],
                              stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True)
    clean = subprocess.run([e2fsck, "-fn", volume],
                           stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
    if repaired.returncode > 2 or clean.returncode != 0:
        raise RuntimeError("independent e2fsck failed\n%s\n%s" %
                           (repaired.stdout, clean.stdout))


def run_seed(args, seed):
    name = "%08x" % seed
    image = os.path.join(args.work, "fsstress-%s.img" % name)
    volume = os.path.join(args.work, "fsstress-%s.ext4" % name)
    run_directory = os.path.join(args.work, "fsstress-%s-run" % name)
    hostfs_root = None if args.host_work is None else \
        os.path.join(args.host_work, "fsstress-%s-hostfs" % name)
    command = ("fsstress -n %d -s 0x%08x -w %d -f %d -b %d%s" %
               (args.operations, seed, args.workers, args.files,
                args.max_bytes,
                " -o %s" % args.operation if args.operation else ""))
    qemu_args = list(args.qemu_arg)
    if args.exec_log:
        qemu_args += ["-D", "%s-%s.log" % (args.exec_log, name)]

    shutil.copyfile(args.image, image)
    os.makedirs(run_directory, exist_ok=True)
    machine = terminal.Machine(args.qemu, args.rom, image, run_directory,
                               qemu_args, hostfs_root=hostfs_root)
    profiler = None

    def start_profiler():
        nonlocal profiler
        if args.perf is not None and profiler is None:
            profiler = subprocess.Popen([
                args.perf, "record", "-F", str(args.perf_frequency),
                "-e", "cpu-clock:u",
                "-k", "1",
                "-o", args.perf_data, "-p", str(machine.process.pid),
            ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)

    def stop_profiler(check=True):
        nonlocal profiler
        if profiler is None:
            return
        profiler.send_signal(signal.SIGINT)
        errors = profiler.communicate()[1]
        result = profiler.returncode
        profiler = None
        if check and result not in (0, -signal.SIGINT,
                                    128 + signal.SIGINT):
            raise RuntimeError("perf failed (%d): %s" %
                               (result, errors.strip()))

    def write_perf_report():
        if args.perf_report is None:
            return
        data = args.perf_data
        if "-jitdump" in qemu_args:
            data += ".jit"
            result = subprocess.run([
                args.perf, "inject", "-j", "-i", args.perf_data,
                "-o", data,
            ], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            if result.returncode != 0:
                raise RuntimeError("perf inject failed (%d): %s" %
                                   (result.returncode,
                                    result.stderr.strip()))
        with open(args.perf_report, "w", encoding="utf-8") as stream:
            result = subprocess.run([
                args.perf, "report", "--stdio", "--no-children",
                "--percent-limit", "0.0", "--sort", "symbol",
                "-i", data,
            ], stdout=stream, stderr=subprocess.PIPE, text=True)
        if result.returncode != 0:
            raise RuntimeError("perf report failed (%d): %s" %
                               (result.returncode, result.stderr.strip()))

    try:
        if args.tune_runtime and not hotplug.tune_runtime(machine.qmp):
            raise RuntimeError("QEMU runtime tuning failed")
        if not open_terminal(machine):
            raise RuntimeError("boot did not reach Terminal")
        if args.exec_log:
            machine.qmp.monitor("log exec,nochain")
        stats_before = machine_stats(machine)
        before = machine.sequence()
        machine.qmp.type_text(command)
        start_profiler()
        machine.qmp.key("ret")
        lines, _ = wait_text(
            machine, ("ASTRA FSSTRESS PASS", "ASTRA FSSTRESS FAIL",
                      "fsstress: crashed", "resident process exited:"),
            before, time.monotonic() + args.command_deadline)
        stop_profiler()
        write_perf_report()
        if lines is None:
            raise RuntimeError("QEMU exited during fsstress")
        for line in lines:
            if line.startswith("fsstress:") or line.startswith("ASTRA FSSTRESS"):
                print(line, flush=True)
        if not any(line.startswith("fsstress: operation-elapsed-ns=")
                   for line in lines):
            raise RuntimeError("fsstress omitted operation-phase timing")
        if not any("ASTRA FSSTRESS PASS" in line for line in lines):
            raise RuntimeError("fsstress reported failure")
        if args.exec_log:
            machine.qmp.monitor("log none")
        stats_after = machine_stats(machine)
        delta = {name: stats_after[name] - stats_before[name]
                 for name in BLOCK_PROPERTIES}
        pmmu_delta = {name: stats_after[name] - stats_before[name]
                      for name in PMMU_PROPERTIES}
        host_delta = {name: stats_after[name] - stats_before[name]
                      for name in HOST_PROPERTIES}
        host_operation_delta = {
            name: stats_after[name] - stats_before[name]
            for name in HOST_OPERATION_PROPERTIES}
        print("fsstress: block read-requests=%d read-sectors=%d" %
              (delta["astra-block-read-requests"],
               delta["astra-block-read-sectors"]), flush=True)
        print("fsstress: block write-requests=%d write-sectors=%d flush=%d" %
              (delta["astra-block-write-requests"],
               delta["astra-block-write-sectors"],
               delta["astra-block-flush-requests"]), flush=True)
        print("fsstress: pmmu fills=%d atc-hits=%d walks=%d "
              "crp-writes=%d crp-changes=%d" %
              tuple(pmmu_delta[name] for name in PMMU_PROPERTIES), flush=True)
        print("fsstress: host submissions=%d commands=%d execution-ns=%d" %
              tuple(host_delta[name] for name in HOST_PROPERTIES), flush=True)
        print("fsstress: host operations " + " ".join(
            "%s=%d" % (name.removeprefix("astra-host-fs-"),
                         host_operation_delta[name])
            for name in HOST_OPERATION_PROPERTIES), flush=True)
        print("fsstress: host inflight=%d max-inflight=%d->%d" %
              (stats_after["astra-host-inflight"],
               stats_before["astra-host-max-inflight"],
               stats_after["astra-host-max-inflight"]), flush=True)
        if (stats_after["astra-host-inflight"] != 0 or
                stats_after["astra-host-max-inflight"] <
                stats_before["astra-host-max-inflight"] or
                (args.workers > 1 and
                 stats_after["astra-host-max-inflight"] < 2)):
            raise RuntimeError("invalid host concurrency accounting")
        if sum(host_operation_delta.values()) != host_delta[
                "astra-host-commands"]:
            raise RuntimeError("host operation accounting disagrees")
        if (pmmu_delta["astra-pmmu-tlb-fills"] == 0 or
                pmmu_delta["astra-pmmu-crp-writes"] == 0 or
                pmmu_delta["astra-pmmu-crp-changes"] == 0 or
                pmmu_delta["astra-pmmu-crp-changes"] >
                pmmu_delta["astra-pmmu-crp-writes"] or
                pmmu_delta["astra-pmmu-atc-hits"] +
                pmmu_delta["astra-pmmu-table-walks"] >
                pmmu_delta["astra-pmmu-tlb-fills"]):
            raise RuntimeError("invalid PMMU accounting: %r" % pmmu_delta)
    except Exception as error:
        machine.recent_serial()
        raise RuntimeError("seed 0x%08x failed (%s)\n%s" %
                           (seed, error, "\n".join(machine.log[-100:]))) \
            from error
    finally:
        if profiler is not None:
            stop_profiler(False)
        machine.close()
        if args.qemu_log:
            with open(args.qemu_log, "w", encoding="utf-8") as stream:
                stream.write("\n".join(machine.log))
                stream.write("\n")
    if args.defer_fsck:
        shutil.rmtree(run_directory)
        print("ASTRA FSSTRESS IMAGE AWAITS EXTERNAL FSCK seed=0x%08x "
              "image=%s" % (seed, os.path.abspath(image)), flush=True)
        return False
    independent_fsck(args.e2fsck, image, volume)
    os.unlink(image)
    os.unlink(volume)
    shutil.rmtree(run_directory)
    print("ASTRA FSSTRESS IMAGE PASS seed=0x%08x" % seed, flush=True)
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--e2fsck")
    parser.add_argument(
        "--defer-fsck", action="store_true",
        help="retain each exercised image for independent fsck on another host")
    parser.add_argument("--work", required=True)
    parser.add_argument(
        "--host-work",
        help="separate host-filesystem root from retained image artifacts")
    parser.add_argument("--operations", type=int, default=1000)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--files", type=int, default=16)
    parser.add_argument("--max-bytes", type=int, default=8192)
    parser.add_argument("--operation", choices=(
        "write", "read", "append", "truncate", "rename", "move",
        "delete", "sync"))
    parser.add_argument("--command-deadline", type=float, default=120.0)
    parser.add_argument("--qemu-arg", action="append", default=[])
    parser.add_argument("--qemu-log")
    parser.add_argument("--perf")
    parser.add_argument("--perf-data")
    parser.add_argument("--perf-frequency", type=int, default=999)
    parser.add_argument(
        "--perf-report",
        help="write a complete workload-scoped perf symbol report")
    parser.add_argument("--tune-runtime", action="store_true")
    parser.add_argument("--exec-log",
                        help="QEMU execution-log prefix; enabled only around "
                             "fsstress")
    parser.add_argument("--seeds", nargs="+", type=lambda text: int(text, 0),
                        default=[0x68A57A31])
    args = parser.parse_args()
    if (args.perf is None) != (args.perf_data is None):
        parser.error("--perf and --perf-data must be used together")
    if args.perf_report is not None and args.perf is None:
        parser.error("--perf-report requires --perf and --perf-data")
    if args.perf_frequency <= 0:
        parser.error("--perf-frequency must be positive")
    if args.defer_fsck == (args.e2fsck is not None):
        parser.error("choose exactly one of --e2fsck or --defer-fsck")
    if (args.operations < 1 or args.operations > 0xffffffffffffffff or
            args.workers < 1 or args.workers > 0xffffffff or
            args.files < 1 or args.files > 0xffffffff or
            args.max_bytes < 1 or args.max_bytes > 0xffffffff or
            args.command_deadline <= 0.0 or
            any(seed < 1 or seed > 0xffffffff for seed in args.seeds)):
        parser.error("counts and seeds must be positive 32-bit values")
    os.makedirs(args.work, exist_ok=True)
    if args.host_work is not None:
        os.makedirs(args.host_work, exist_ok=True)
    verified = True
    for seed in args.seeds:
        try:
            verified = run_seed(args, seed) and verified
        except Exception:
            print("retained failing artifacts in %s" % args.work, flush=True)
            raise
    if verified:
        print("ASTRA FILESYSTEM STRESS PASS: %d seed(s)" % len(args.seeds))
    else:
        print("ASTRA FILESYSTEM STRESS EXECUTION PASS; FSCK DEFERRED: "
              "%d seed(s)" % len(args.seeds))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
