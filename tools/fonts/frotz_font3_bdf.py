#!/usr/bin/env python3
"""Export Frotz's Z-machine graphics font to a fixed 8x8 BDF strike.

This is build-time conversion only. Astra's hardware glyph engine renders the
resulting AFNT at runtime. The source glyphs are GPL-2.0-or-later; see
third_party/frotz/src/sdl/sf_font3.c.
"""

import argparse
import re
from pathlib import Path


def convert(source: str) -> str:
    table = re.search(r"static zbyte Zfont3\[\] = \{(.*?)\};", source, re.S)
    if table is None:
        raise ValueError("Zfont3 table missing")
    glyphs = []
    for line in table.group(1).splitlines():
        match = re.fullmatch(r"/\*\s*(\d+)\*/\s*" +
                             r"((?:0x[0-9a-fA-F]{2},?\s*){8})", line.strip())
        if match is None:
            if line.strip():
                raise ValueError("malformed Zfont3 row")
            continue
        code = int(match.group(1))
        rows = [int(value, 16) for value in
                re.findall(r"0x([0-9a-fA-F]{2})", match.group(2))]
        if code != 32 + len(glyphs) or len(rows) != 8:
            raise ValueError("Zfont3 code or row count mismatch")
        glyphs.append(rows)
    if len(glyphs) != 95:
        raise ValueError("Zfont3 must contain codes 32 through 126")
    lines = ["STARTFONT 2.1", "COMMENT Derived from Frotz sf_font3.c",
             "COMMENT SPDX-License-Identifier: GPL-2.0-or-later",
             "FONT -astra-zmachine-graphics-medium-r-normal--8-80-72-72-C-80-ISO10646-1",
             "SIZE 8 72 72", "FONTBOUNDINGBOX 8 8 0 -2",
             "STARTPROPERTIES 6", 'FAMILY_NAME "Z-machine Graphics"',
             'WEIGHT_NAME "Medium"', "PIXEL_SIZE 8", "SPACING \"C\"",
             "FONT_ASCENT 6", "FONT_DESCENT 2", "ENDPROPERTIES",
             "CHARS 95"]
    for code, rows in enumerate(glyphs, 32):
        lines += [f"STARTCHAR Z{code}", f"ENCODING {code}",
                  "SWIDTH 1000 0", "DWIDTH 8 0", "BBX 8 8 0 -2",
                  "BITMAP", *(f"{row:02X}" for row in rows), "ENDCHAR"]
    lines.append("ENDFONT")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.write_text(convert(args.source.read_text(encoding="utf-8")),
                           encoding="ascii")


if __name__ == "__main__":
    main()
