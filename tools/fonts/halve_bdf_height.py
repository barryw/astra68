#!/usr/bin/env python3
"""Derive a half-height bitmap BDF strike by combining adjacent scanlines.

This build-time transformation preserves every glyph's width, advance and
Unicode label. OR-combining each row pair keeps single-pixel strokes visible.
It requires an even, fixed-height cell and refuses ambiguous BDF geometry.
"""

import argparse
from pathlib import Path
import re


def halve(source: str) -> str:
    lines = source.splitlines()
    result = []
    cell_width = cell_height = None
    glyphs = 0
    at = 0
    while at < len(lines):
        line = lines[at]
        fields = line.split()
        if line.startswith("FONT "):
            # Keep a distinct XLFD identity for the derived strike.
            def half_xlfd(match):
                pixel, point = map(int, match.groups())
                if pixel % 2 or point % 2:
                    raise ValueError("XLFD size must be even")
                return f"--{pixel // 2}-{point // 2}-"
            changed, count = re.subn(r"--(\d+)-(\d+)-", half_xlfd,
                                     line, count=1)
            if count != 1:
                raise ValueError("FONT must use a sized XLFD name")
            result.append(changed)
        elif line.startswith("SIZE "):
            if len(fields) != 4 or int(fields[1]) % 2:
                raise ValueError("SIZE must have an even pixel height")
            result.append(f"SIZE {int(fields[1]) // 2} {fields[2]} {fields[3]}")
        elif line.startswith("FONTBOUNDINGBOX "):
            if len(fields) != 5:
                raise ValueError("invalid FONTBOUNDINGBOX")
            cell_width, cell_height, left, bottom = map(int, fields[1:])
            if cell_height < 2 or cell_height % 2 or bottom % 2:
                raise ValueError("font cell height and descent must be even")
            result.append(f"FONTBOUNDINGBOX {cell_width} {cell_height // 2} "
                          f"{left} {bottom // 2}")
        elif fields[:1] in (["FONT_ASCENT"], ["FONT_DESCENT"],
                             ["PIXEL_SIZE"], ["POINT_SIZE"]):
            if len(fields) != 2 or int(fields[1]) % 2:
                raise ValueError(f"{fields[0]} must be even")
            result.append(f"{fields[0]} {int(fields[1]) // 2}")
        elif line.startswith("SWIDTH "):
            if len(fields) != 3:
                raise ValueError("invalid SWIDTH")
            result.append(f"SWIDTH {int(fields[1]) * 2} {int(fields[2]) * 2}")
        elif line.startswith("BBX "):
            if cell_width is None or len(fields) != 5:
                raise ValueError("glyph precedes font geometry")
            width, height, left, bottom = map(int, fields[1:])
            if (width != cell_width or height != cell_height or
                    left != 0 or bottom % 2):
                raise ValueError("glyph is not an even fixed-height cell")
            result.append(f"BBX {width} {height // 2} {left} {bottom // 2}")
        elif line == "BITMAP":
            if cell_width is None or cell_height is None:
                raise ValueError("bitmap precedes font geometry")
            pitch = (cell_width + 7) // 8
            rows = lines[at + 1:at + 1 + cell_height]
            if len(rows) != cell_height or any(
                    len(row) != pitch * 2 or
                    any(digit not in "0123456789ABCDEFabcdef" for digit in row)
                    for row in rows):
                raise ValueError("truncated or invalid glyph bitmap")
            if (at + 1 + cell_height >= len(lines) or
                    lines[at + 1 + cell_height] != "ENDCHAR"):
                raise ValueError("glyph bitmap does not end at ENDCHAR")
            result.append(line)
            for row in range(0, cell_height, 2):
                merged = int(rows[row], 16) | int(rows[row + 1], 16)
                result.append(f"{merged:0{pitch * 2}X}")
            at += cell_height
            glyphs += 1
        else:
            result.append(line)
        at += 1
    if cell_width is None or glyphs == 0:
        raise ValueError("BDF contains no fixed-height bitmap glyphs")
    declared = next((int(line.split()[1]) for line in lines
                     if line.startswith("CHARS ")), None)
    if declared != glyphs:
        raise ValueError("CHARS count does not match bitmap glyphs")
    return "\n".join(result) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(halve(args.input.read_text(encoding="ascii")),
                           encoding="ascii")


if __name__ == "__main__":
    main()
