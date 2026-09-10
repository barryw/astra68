#!/usr/bin/env python3
"""Retarget Terasic's HDMI PLL to 148.5 MHz pixel and 165 MHz build clocks."""

from pathlib import Path
import re
import sys


path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")

replacements = (
    (
        r'(<ipxact:parameter parameterId="clockRate"[^>]*>.*?'
        r'<ipxact:value>)74250000(</ipxact:value>)',
        r'\g<1>148500000\g<2>',
    ),
    (
        r'(<ipxact:parameter parameterId="gui_number_of_clocks"'
        r'[^>]*>.*?<ipxact:value>)1(</ipxact:value>)',
        r'\g<1>2\g<2>',
    ),
    (
        r'(<ipxact:parameter parameterId="gui_output_clock_frequency0"'
        r'[^>]*>.*?<ipxact:value>)74\.25(</ipxact:value>)',
        r'\g<1>148.5\g<2>',
    ),
    (
        r'(<ipxact:parameter parameterId="gui_output_clock_frequency1"'
        r'[^>]*>.*?<ipxact:value>)12\.288(</ipxact:value>)',
        r'\g<1>165.0\g<2>',
    ),
    (
        r'(&lt;key&gt;CLOCK_RATE&lt;/key&gt;\s*'
        r'&lt;value&gt;)74250000(&lt;/value&gt;)',
        r'\g<1>148500000\g<2>',
    ),
)
for pattern, replacement in replacements:
    text, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise SystemExit(f"pixel PLL field: expected one match, found {count}")

path.write_text(text, encoding="utf-8")
