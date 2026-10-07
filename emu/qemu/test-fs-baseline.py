#!/usr/bin/env python3
"""held-miss floor: the record must reach disk before the check can fail
the run (review finding -- raising before the write discarded all 14
workloads' data on a slow host)."""

import importlib.util
import json
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
