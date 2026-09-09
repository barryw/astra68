#!/usr/bin/env python3
"""Regression check for the ROM CPU benchmark record consumed by the display gate."""

import importlib.util
from pathlib import Path


display_path = Path(__file__).with_name("test-display.py")
spec = importlib.util.spec_from_file_location("astra_display_gate", display_path)
display = importlib.util.module_from_spec(spec)
spec.loader.exec_module(display)

assert display.parse_cpu_benchmark(
    "CPU benchmark ...... 112489000 Hz effective (best 10561 us kernel decode)"
) == (112489000, 10561)
assert display.parse_cpu_benchmark("CPU benchmark ...... malformed") is None

print("display benchmark parser test: PASS")
