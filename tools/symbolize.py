#!/usr/bin/env python3
"""Turn the addresses in a panic or a fault report into functions and lines.

The machine is three separate ELFs at fixed addresses -- the ROM, the kernel
and the initial user image -- so an address alone does not say which file can
explain it. That is the whole reason this exists: everyone who has read a fault
report so far has done this routing in their head and then run addr2line by
hand.

Feed it a report, or a list of addresses:

    tools/symbolize.py 0x00100176 0x0205a29c
    ... | tools/symbolize.py            # every 0x... in the stream

Addresses that fall in no image are left alone rather than guessed at.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

ADDRESS = re.compile(r"0x[0-9a-fA-F]{4,8}")

# Where each image lives. The ranges are the machine's memory map, not
# anything read out of the files, so a report from a build whose ELFs are gone
# still routes correctly.
IMAGES = (
    ("user", 0x00100000, 0x00200000,
     "sw/userspace/supervisor/build/m68k/astra_supervisor.elf"),
    ("kernel", 0x02044000, 0x02100000, "sw/kernel/build/astra_kernel.elf"),
    ("rom", 0xFFE00000, 0x100000000, "sw/boot/build/astra_boot.elf"),
)

ADDR2LINE = os.environ.get("ADDR2LINE", "m68k-linux-gnu-addr2line")


def select_image(address: int):
    """The image whose address range contains `address`, or None."""
    for name, start, end, path in IMAGES:
        if start <= address < end:
            return name, start, end, path
    return None


def parse_addr2line(output: str) -> tuple[str, str]:
    """addr2line -f -e prints the function on one line and file:line on the next."""
    lines = [line.strip() for line in output.splitlines() if line.strip()]
    if not lines:
        return "??", "??"
    if len(lines) == 1:
        return lines[0], "??"
    return lines[0], lines[1]


def describe(address: int, root: str) -> str:
    selected = select_image(address)
    if selected is None:
        return f"0x{address:08x}  (no image covers this address)"
    name, _, _, relative = selected
    path = os.path.join(root, relative)
    if not os.path.exists(path):
        return f"0x{address:08x}  {name}: {relative} is not built"
    try:
        output = subprocess.run(
            [ADDR2LINE, "-f", "-e", path, f"0x{address:x}"],
            capture_output=True, text=True, check=False).stdout
    except FileNotFoundError:
        return f"0x{address:08x}  {name}: {ADDR2LINE} not found"
    function, location = parse_addr2line(output)
    return f"0x{address:08x}  {name}: {function} at {location}"


def addresses_in(text: str) -> list[int]:
    return [int(match, 16) for match in ADDRESS.findall(text)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("address", nargs="*",
                        help="addresses; omit to read a report on stdin")
    parser.add_argument("--root", default=os.path.join(
        os.path.dirname(os.path.abspath(__file__)), ".."),
        help="repository root holding the ELFs")
    arguments = parser.parse_args()

    if arguments.address:
        values = [int(item, 0) for item in arguments.address]
    else:
        values = addresses_in(sys.stdin.read())
    if not values:
        print("no addresses found", file=sys.stderr)
        return 1
    for value in values:
        print(describe(value, arguments.root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
