#!/usr/bin/env python3
"""Pinned Frotz graphics glyphs survive the AFNT hardware-font import."""

import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FONT = ROOT / "sw/userspace/graphics/fonts/astra-zmachine-graphics.afnt"


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


exporter = module(ROOT / "tools/fonts/frotz_font3_bdf.py", "frotz_font3_bdf")
afnt = module(ROOT / "tools/fonts/afnt.py", "afnt")
source = (ROOT / "third_party/frotz/src/sdl/sf_font3.c").read_text()
bdf = (ROOT / "sw/userspace/graphics/fonts/zmachine-graphics.bdf").read_text()
assert exporter.convert(source) == bdf
assert "CHARS 95\n" in bdf
try:
    exporter.convert(source.replace("/*126*/", "/*127*/", 1))
except ValueError as error:
    assert "code or row count" in str(error)
else:
    raise AssertionError("font3 code mismatch accepted")

data = FONT.read_bytes()
cmap, strikes, glyphs, bitmap = afnt.unpack_afnt(data)
assert len(strikes) == 1 and strikes[0][2:4] == (8, 8)
assert len(cmap) == len(glyphs) == 96  # 95 ZSCII graphics + replacement
assert {32, 33, 65, 126, 0xfffd} <= dict(cmap).keys()
glyph = glyphs[dict(cmap)[65]]
assert bitmap[glyph[1]:glyph[1] + glyph[2]] == bytes.fromhex(
    "000000f8f8f8f8f8")
metadata = json.loads(afnt.parse_afnt(data)[b"NAME"])
assert metadata["family"] == "Z-machine Graphics"
assert metadata["license"] == "GPL-2.0-or-later"
assert metadata["source_revision"] == "042d7bcadc1e2a8090cf737c841d8b7fc14b6eff"
print("AFNT Z-machine graphics font: PASS")
