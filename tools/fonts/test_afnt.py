#!/usr/bin/env python3
"""Small contract test for the resident rescue-font image."""

from pathlib import Path
import importlib.util


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/fonts/afnt.py"
spec = importlib.util.spec_from_file_location("afnt", TOOL)
assert spec is not None and spec.loader is not None
afnt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(afnt)

source = ROOT / "sw/userspace/graphics/fonts/astra-mono.afnt"
generated = afnt.emit_cp437_hex(source.read_bytes())
rows = generated.splitlines()

assert len(rows) == 256 * 16
assert rows[ord("A") * 16:(ord("A") + 1) * 16] == [
    "00", "00", "7c", "c6", "c6", "c6", "fe", "c6",
    "c6", "c6", "c6", "c6", "00", "00", "00", "00",
]
assert rows[0xdb * 16:(0xdb + 1) * 16] == ["ff"] * 16
print("AFNT rescue-font contract: PASS")
