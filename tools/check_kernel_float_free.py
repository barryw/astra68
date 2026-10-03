#!/usr/bin/env python3
"""Fail if the kernel executes FPU instructions outside the user-state switch.

The kernel is built -msoft-float and links no libgcc, so its C cannot emit
FPU code; user FPU registers survive kernel execution only because of that
(docs/USERSPACE_FPU.md 2.4 rule 2). This keeps it true: every coprocessor-1
instruction (first word 0xF2xx or 0xF3xx) must sit in one of the two
symbols that save and load a user thread's FPU state, and both must have
some, or a rename would pass vacuously. Coprocessor-2 words
(0xF4xx-0xF6xx) are the 68040's CINV, CPUSH, PFLUSH, PTEST and MOVE16, and
are allowed anywhere.

usage: check_kernel_float_free.py OBJDUMP KERNEL_ELF
"""

import re
import subprocess
import sys

# kernel_fpu_invalidate touches only kernel_fpu_owner, never the FPU.
ALLOWED = {"_kernel_restore_user_context", "kernel_fpu_flush"}
SYMBOL = re.compile(r"^[0-9a-f]+ <([^>]+)>:$")
# An instruction's first line carries its mnemonic after a second tab; a
# long instruction's extension words wrap onto lines without one, and their
# values (an immediate such as 0x6719f361) must not be read as opcodes.
INSTRUCTION = re.compile(r"^\s*([0-9a-f]+):\t([0-9a-f]{4})[0-9a-f ]*\t\S")


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    listing = subprocess.run([sys.argv[1], "-d", sys.argv[2]], check=True,
                             stdout=subprocess.PIPE, text=True).stdout
    symbol = None
    allowed_seen = set()
    violations = []
    for line in listing.splitlines():
        match = SYMBOL.match(line)
        if match:
            symbol = match.group(1)
            continue
        match = INSTRUCTION.match(line)
        if not match:
            continue
        word = int(match.group(2), 16)
        if word & 0xfe00 != 0xf200:
            continue
        if symbol in ALLOWED:
            allowed_seen.add(symbol)
        else:
            violations.append("%s in %s: %s" % (match.group(1), symbol,
                                                line.strip()))
    if violations:
        print("kernel executes FPU instructions outside the user-state "
              "switch:", file=sys.stderr)
        for violation in violations[:20]:
            print("    " + violation, file=sys.stderr)
        return 1
    if allowed_seen != ALLOWED:
        # A rename would make this check pass vacuously.
        print("FPU switch symbols missing from the kernel: %s"
              % ", ".join(sorted(ALLOWED - allowed_seen)), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
