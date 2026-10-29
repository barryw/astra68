#!/usr/bin/env python3
"""Source and AFNT contract for the shipped 8x8 hardware pixel font."""

import importlib.util
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FONT_DIR = ROOT / "sw/userspace/graphics/fonts"


def module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


halve = module(ROOT / "tools/fonts/halve_bdf_height.py", "halve_bdf_height")
afnt = module(ROOT / "tools/fonts/afnt.py", "afnt")
source = (FONT_DIR / "spleen/8x16.bdf").read_text(encoding="ascii")
derived = (FONT_DIR / "spleen/8x8.bdf").read_text(encoding="ascii")
assert halve.halve(source) == derived
for suffix, bold, italic, style in (
        ("-bold", True, False, "Bold"),
        ("-italic", False, True, "Italic"),
        ("-bold-italic", True, True, "Bold Italic")):
    alternate = (FONT_DIR / f"spleen/8x8{suffix}.bdf").read_text(
        encoding="ascii")
    assert halve.halve(source, bold=bold, italic=italic) == alternate
    variant = (FONT_DIR / f"astra-pixel-mono{suffix}.afnt").read_bytes()
    alternate_cmap, alternate_strikes, alternate_glyphs, _ = afnt.unpack_afnt(
        variant)
    assert len(alternate_cmap) == len(alternate_glyphs) == 1002
    assert alternate_strikes[0][2:4] == (8, 8)
    variant_metadata = json.loads(afnt.parse_afnt(variant)[b"NAME"])
    assert variant_metadata["family"] == "Astra Pixel Mono"
    assert variant_metadata["style"] == style
assert "FONTBOUNDINGBOX 8 8 0 -2" in derived
assert "FONT_ASCENT 6" in derived
assert "FONT_DESCENT 2" in derived
assert "CHARS 1001" in derived
try:
    halve.halve(source.replace("CHARS 1001", "CHARS 1000", 1))
except ValueError as error:
    assert "CHARS count" in str(error)
else:
    raise AssertionError("incorrect glyph count was accepted")

data = (FONT_DIR / "astra-pixel-mono.afnt").read_bytes()
cmap, strikes, glyphs, bitmap = afnt.unpack_afnt(data)
assert len(cmap) == len(glyphs) == 1002
assert len(strikes) == 1
strike = strikes[0]
assert (strike[1], strike[2], strike[3], strike[4], strike[5],
        strike[9]) == (afnt.MASK1, 8, 8, 6 * 64, 2 * 64, 8 * 64)
assert all(glyph[3:6] == (8, 8, 1) and glyph[9] == 8 * 64
           for glyph in glyphs)
assert {ord("A"), ord("?"), 0x00e9, 0x2500, 0xfffd} <= {
    codepoint for codepoint, _ in cmap}
metadata = json.loads(afnt.parse_afnt(data)[b"NAME"])
assert metadata["family"] == "Astra Pixel Mono"
assert metadata["license"] == "BSD-2-Clause"
assert metadata["source_files"][0]["name"] == "8x8.bdf"
glyph_id = dict(cmap)[ord("A")]
glyph = glyphs[glyph_id]
assert bitmap[glyph[1]:glyph[1] + glyph[2]] == bytes.fromhex(
    "00fec6fec6c60000")
print("AFNT 8x8 pixel-font contract: PASS")
