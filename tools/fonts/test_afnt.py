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
source_bytes = source.read_bytes()
generated = afnt.emit_cp437_hex(source_bytes)
rows = generated.splitlines()

assert source_bytes[:8] == b"AFNT\x00\x00\x00\x02"
cmap, strikes, glyphs, bitmap = afnt.unpack_afnt(source_bytes)
assert len(strikes) == 1
(strike_id, bitmap_format, pixel_width, pixel_height, ascent, descent,
 line_gap, cap_height, x_height, max_advance, underline_position,
 underline_thickness, strikeout_position, strikeout_thickness,
 glyph_first, glyph_count, glyph_record_size, flags, reserved) = strikes[0]
assert (strike_id, bitmap_format, pixel_width, pixel_height) == (0, 1, 8, 16)
assert (ascent, descent, line_gap, max_advance) == (12 * 64, 4 * 64, 0,
                                                    8 * 64)
assert cap_height >= x_height > 0
assert underline_position > 0 and underline_thickness > 0
assert strikeout_position > 0 and strikeout_thickness > 0
assert glyph_first == 0 and glyph_count == 1002
assert glyph_record_size == afnt.GLYPH.size and flags == reserved == 0
replacement_id = dict(cmap)[0xfffd]
replacement = glyphs[glyph_first + replacement_id]
assert any(bitmap[replacement[1]:replacement[1] + replacement[2]])

assert len(rows) == 256 * 16
assert rows[ord("A") * 16:(ord("A") + 1) * 16] == [
    "00", "00", "7c", "c6", "c6", "c6", "fe", "c6",
    "c6", "c6", "c6", "c6", "00", "00", "00", "00",
]
assert rows[0xdb * 16:(0xdb + 1) * 16] == ["ff"] * 16
print("AFNT rescue-font contract: PASS")
