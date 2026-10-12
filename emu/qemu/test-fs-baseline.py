#!/usr/bin/env python3
"""held-miss floor: the record must reach disk before the check can fail
the run (review finding -- raising before the write discarded all 14
workloads' data on a slow host)."""

import importlib.util
import json
import subprocess
import sys
import tempfile
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "fs_baseline", Path(__file__).with_name("fs-baseline.py"))
fs_baseline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fs_baseline)


def guest(mb_s):
    return {"workloads": {"held-miss": {"throughput_bps": mb_s * 1e6}}}


with tempfile.TemporaryDirectory() as directory:
    slow_path = str(Path(directory) / "slow.json")
    try:
        fs_baseline.write_record({}, guest(15), 20.0, slow_path)
        raised = False
    except SystemExit:
        raised = True
    assert raised, "15 MB/s is below the 20 MB/s floor and must exit non-zero"
    assert Path(slow_path).exists(), "record must be written before the exit"
    assert json.loads(Path(slow_path).read_text(
        ))["checks"]["held_miss_floor"]["ok"] is False

    fast_path = str(Path(directory) / "fast.json")
    fs_baseline.write_record({}, guest(30), 20.0, fast_path)  # must not raise
    assert Path(fast_path).exists()
    assert json.loads(Path(fast_path).read_text(
        ))["checks"]["held_miss_floor"]["ok"] is True

    # A floor of 0 disables the check outright, even at a throughput that
    # would otherwise fail it.
    disabled_path = str(Path(directory) / "disabled.json")
    fs_baseline.write_record({}, guest(1), 0, disabled_path)  # must not raise
    assert json.loads(Path(disabled_path).read_text(
        ))["checks"]["held_miss_floor"]["ok"] is True

print("fs-baseline held-miss floor test: PASS")

# Missing one workload or one failed run must survive five-run aggregation.
compare = Path(__file__).resolve().parents[2] / "tools/fs-baseline-compare.py"
with tempfile.TemporaryDirectory() as directory:
    paths = {}
    healthy = {"median_ns": 100, "p95_ns": 200,
               "throughput_bps": 1000, "failed": False}
    for name, workloads in (
        ("healthy", {"w": healthy}),
        ("slow", {"w": dict(healthy, median_ns=111)}),
        ("failed", {"w": dict(healthy, failed=True)}),
        ("missing", {}),
        ("zero-rate", {"w": dict(healthy, throughput_bps=0)}),
    ):
        paths[name] = str(Path(directory) / (name + ".json"))
        Path(paths[name]).write_text(json.dumps({"guest": {"workloads": workloads}}))
    def check(base, new, expected):
        result = subprocess.run([sys.executable, str(compare), base, new],
                                text=True, capture_output=True, check=False)
        assert result.returncode == expected, (result.stdout, result.stderr)
    healthy_path = paths["healthy"]
    check(healthy_path, ",".join([healthy_path] * 3 + [paths["slow"]] * 2), 0)
    check(healthy_path, ",".join([healthy_path] * 2 + [paths["slow"]] * 3), 1)
    for broken in ("failed", "missing"):
        partial = ",".join([healthy_path] * 4 + [paths[broken]])
        check(healthy_path, partial, 1)
        check(partial, healthy_path, 1)
    check(healthy_path, paths["zero-rate"], 1)
print("fs-baseline five-run comparison test: PASS")

# A historical benchmark image must never receive the current tree's ABI.
from unittest import mock
semantic_spec = importlib.util.spec_from_file_location(
    "fs_semantic", Path(__file__).with_name("test-fs-semantics.py"))
semantic = importlib.util.module_from_spec(semantic_spec)
semantic_spec.loader.exec_module(semantic)
with tempfile.TemporaryDirectory() as directory:
    image = Path(directory) / "historical.img"
    image.write_bytes(b"historical guest libraries and commands")
    machine = mock.Mock()
    machine.process.returncode = 0
    machine.reader.hostfs_root = directory
    def boot(_qemu, _rom, copied_image, _run, **kwargs):
        assert Path(copied_image).read_bytes() == image.read_bytes()
        assert kwargs["source_root"] == directory
        return machine
    def subprocess_run(command, **kwargs):
        assert command[0] != "make", "benchmark built a current test command"
        return subprocess.CompletedProcess(command, 0, stdout="", stderr="")
    with mock.patch.object(sys, "argv", ["test-fs-semantics.py", "qemu", "rom",
             str(image), "--bench", "old", str(Path(directory) / "out.json"),
             "--tree", directory]), \
         mock.patch.object(semantic.astra_image, "install",
             side_effect=AssertionError("benchmark replaced historical libraries")), \
         mock.patch.object(semantic.astra_image, "install_test_command",
             side_effect=AssertionError("benchmark injected a current command")), \
         mock.patch.object(semantic.qemu_runtime, "root_provider",
             return_value=("launcher", "provider")) as provider, \
         mock.patch.object(semantic.terminal, "Machine", side_effect=boot), \
         mock.patch.object(semantic.terminal, "open_terminal", return_value=True), \
         mock.patch.object(semantic, "run_bench") as benchmark, \
         mock.patch.object(semantic.subprocess, "run", side_effect=subprocess_run):
        semantic.main()
        assert provider.call_args.kwargs.get("system") is None
        benchmark.assert_called_once_with(machine, directory, "old",
                                          str(Path(directory) / "out.json"),
                                          tree=directory)
        machine.trace_with_syslog.assert_not_called()
        machine.close.assert_called_once()
print("fs-baseline historical image isolation test: PASS")
with mock.patch.object(semantic.terminal.trace_decode, "kernel_event_names",
         return_value={}) as names, \
     mock.patch.object(semantic.terminal.trace_decode, "load_catalogs",
         return_value={}) as catalogs:
    semantic.terminal.TraceReader(None, "ring", source_root="/historical")
    names.assert_called_once_with("/historical/sw/kernel/trace.h")
    catalogs.assert_called_once_with([
        "/historical/sw/userspace/supervisor/build/m68k/astra_supervisor.elf",
        "/historical/sw/userspace/services/terminal/build/m68k/terminal.elf"])
print("fs-baseline historical trace catalog test: PASS")
