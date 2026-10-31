#!/usr/bin/env python3
"""Keep physical RFB shortcut probes free of implicit Shift."""

import importlib.util
from pathlib import Path
import struct


script = Path(__file__).with_name("de25-rfb.py")
spec = importlib.util.spec_from_file_location("de25_rfb", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class Connection:
    def __init__(self):
        self.events = []

    def sendall(self, payload):
        self.events.append(struct.unpack(">BBHI", payload))


upper = Connection()
lower = Connection()
shifted = Connection()
module.send_shortcut(upper, "Ctrl+C")
module.send_shortcut(lower, "Ctrl+c")
module.send_shortcut(shifted, "Ctrl+Shift+C")

assert upper.events == lower.events
assert upper.events == [
    (4, 1, 0, 0xffe3), (4, 1, 0, ord("c")),
    (4, 0, 0, ord("c")), (4, 0, 0, 0xffe3),
]
assert shifted.events[1] == (4, 1, 0, 0xffe1)
assert shifted.events[2] == (4, 1, 0, ord("c"))
assert shifted.events[-2] == (4, 0, 0, 0xffe1)
print("DE25 RFB shortcut modifiers PASS")
