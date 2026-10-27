#!/usr/bin/env python3
"""Make the pinned Terasic ADV7513 setup match Astra's 24-bit stereo I2S."""

from pathlib import Path
import re
import sys


path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
for old, new in (("0cbc", "0c84"), ("1472", "140b"),
                 ("7307", "7301"), ("761f", "7600")):
    pattern = rf"(?m)^(\s*\d+\s*:\s*LUT_DATA\s*<=\s*16'h){old}(;)"
    text, count = re.subn(pattern, rf"\g<1>{new}\2", text)
    if count != 1:
        raise SystemExit(f"ADV7513 16'h{old}: expected one active LUT entry, found {count}")
# ADV7513 Programming Guide, Table 14: both writes are required after power-up.
for pattern, replacement in (
    (r"(?m)^(parameter\s+LUT_SIZE\s*=\s*)33(\s*;)", r"\g<1>35\2"),
    (r"(?m)^(\s*32\s*:\s*LUT_DATA\s*<=\s*16'hfa7d;[^\n]*\n)",
     r"\g<1>\t33\t:\tLUT_DATA\t<=\t16'he0d0;  // Required fixed register\n"
     r"\t34\t:\tLUT_DATA\t<=\t16'hf900;  // Required fixed register\n"),
):
    text, count = re.subn(pattern, replacement, text)
    if count != 1:
        raise SystemExit("ADV7513 fixed-register LUT: expected pinned vendor layout")
path.write_text(text, encoding="utf-8")
