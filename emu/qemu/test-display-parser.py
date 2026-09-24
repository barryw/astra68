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


class Cycles:
    def __init__(self, values):
        self.values = values
        self.calls = 0

    def property(self, name):
        self.calls += 1
        value = self.values[name]
        return value.pop(0) if isinstance(value, list) else value


def snapshot(submit, complete, collect, counts=(1, 1)):
    return Cycles({
        "astra-display-submissions": list(counts),
        "astra-display-submit-cycle": submit,
        "astra-display-completion-cycle": complete,
        "astra-display-collect-cycle": collect,
    })


assert display.collected_cycle_span(snapshot(100, 150, 180)) == 80
assert display.collected_cycle_span(snapshot(200, 150, 180)) is None
assert display.collected_cycle_span(snapshot(100, 150, 180, (1, 2))) is None

assert display.pointer_route_settled(5, 76, 93, 9, 9, 4, 76, 93, 8)
assert not display.pointer_route_settled(5, 76, 93, 9, 8, 4, 76, 93, 8)
assert not display.pointer_route_settled(5, 75, 93, 9, 9, 4, 76, 93, 8)

print("display benchmark parser test: PASS")
