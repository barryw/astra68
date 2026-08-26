#!/usr/bin/env python3
# astra_boot.bin -> rom_init.hex for $readmemh into the SoC's 32-bit big-endian ROM.
# One 8-hex-digit word per line, MSB-first. Usage: bin2hex.py in.bin out.hex
import sys

src = sys.argv[1] if len(sys.argv) > 1 else "build/astra_boot.bin"
dst = sys.argv[2] if len(sys.argv) > 2 else "build/rom_init.hex"
data = open(src, "rb").read()
data += b"\x00" * ((-len(data)) % 4)  # pad up to a word boundary
with open(dst, "w") as f:
    for i in range(0, len(data), 4):
        f.write("%02x%02x%02x%02x\n" % (data[i], data[i+1], data[i+2], data[i+3]))
print(f"{dst}: {len(data)//4} words from {src}")
