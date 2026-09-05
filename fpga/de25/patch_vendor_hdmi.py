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
path.write_text(text, encoding="utf-8")
