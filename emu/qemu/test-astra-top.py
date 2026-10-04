#!/usr/bin/env python3
"""astra-top parses the sampler's file and does the window arithmetic right."""

import importlib.util
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "astra_top", os.path.join(HERE, "astra-top.py"))
top = importlib.util.module_from_spec(spec)
spec.loader.exec_module(top)


def process(pid, name, priority, runs, syscalls, runtime_ns):
    return top.PROCESS.pack(80, pid, 1, pid, 10, runs, 0, syscalls, 0, 3,
                            2, 1, 1, priority, 19, 0, 0, 0, runtime_ns,
                            5 * 10**9, 0, 0, 0, 0, 10, name.encode())


def sample(sequence, now_ns, switches, processes):
    scheduler = top.SCHEDULER.pack(now_ns, switches, 0, switches, 0, 0,
                                   switches // 2, 0, 0, 0, 0, 0, 0,
                                   len(processes), len(processes))
    return top.HEADER.pack(top.SAMPLE_MAGIC, top.HEADER.size,
                           top.SCHEDULER.size, top.PROCESS.size,
                           len(processes), sequence) + scheduler + \
        b"".join(processes)


def parse(data):
    with tempfile.NamedTemporaryFile() as handle:
        handle.write(data)
        handle.flush()
        return top.read_sample(handle.name)


assert top.PROCESS.size == 112 and top.SCHEDULER.size == 64
# One second of guest time: Doom ran 600 ms, media 100 ms. Doom's switch
# counter wraps, as a 32-bit counter does after long enough.
seq_a, before, procs_a = parse(sample(1, 10**9, 0xFFFFFFF0, [
    process(42, "/apps/Doom.app", 16, 0xFFFFFFFE, 100, 4 * 10**9),
    process(7, "/services/media", 24, 50, 10, 10**8)]))
seq_b, after, procs_b = parse(sample(2, 2 * 10**9, 0x10, [
    process(42, "/apps/Doom.app", 16, 8, 160, 4 * 10**9 + 6 * 10**8),
    process(7, "/services/media", 24, 250, 30, 2 * 10**8),
    process(99, "/commands/ps", 16, 3, 5, 10**7)]))
assert (seq_a, seq_b) == (1, 2)
window = top.guest_window(before, procs_a, after, procs_b)
rows = {row["name"]: row for row in window["processes"]}
assert abs(rows["/apps/Doom.app"]["cpu"] - 60.0) < 1e-9
assert rows["/apps/Doom.app"]["runs_per_s"] == 10      # wrapped
assert abs(rows["/services/media"]["cpu"] - 10.0) < 1e-9
assert rows["/services/media"]["runs_per_s"] == 200
assert rows["/commands/ps"]["new"] and abs(rows["/commands/ps"]["cpu"] - 1) < 1e-9
assert abs(window["idle"] - 29.0) < 1e-9
assert window["scheduler_per_s"]["context_switches"] == 0x20
assert window["processes"][0]["name"] == "/apps/Doom.app"
try:
    top.guest_window(after, procs_b, before, procs_a)
    raise AssertionError("out-of-order samples were accepted")
except ValueError:
    pass
try:
    parse(sample(1, 1, 1, [])[:-1] + b"x" + b"y")
    raise AssertionError("a malformed sample was accepted")
except ValueError:
    pass
# Commits at 0, 16.7, 33.4 and 100 ms over a 0.1 s window: one 66.6 ms gap.
pointer = top.pointer_report([0.0, 0.0167, 0.0334, 0.1], 12, 0.1)
assert pointer["commits_per_s"] == 40 and pointer["input_per_s"] == 120
assert pointer["gaps_over_50ms"] == 1
assert abs(pointer["gap_ms_max"] - 66.6) < 1e-6
assert abs(pointer["gap_ms_p50"] - 16.7) < 1e-6
assert "gap_ms_max" not in top.pointer_report([1.0], 0, 1.0)
# The symbol reader names an address in this Python's own binary, which is
# an ELF with a symbol table on the Linux hosts that run it.
if sys.platform.startswith("linux"):
    functions = top.elf_functions(sys.executable)
    if functions:
        start, _, name = functions[len(functions) // 2]
        assert top.name_address(functions, start) == name
        assert top.name_address(functions, 0) is None
assert top.name_address([(0x100, 0x10, "f")], 0x10f) == "f"
assert top.name_address([(0x100, 0x10, "f")], 0x110) is None
print("astra-top: PASS")
