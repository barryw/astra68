#!/usr/bin/env python3
"""Check that CPU reset deterministically flushes the opcode pipe."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CONTROL = ROOT / "wf68k30L_control.vhd"
DECODER = ROOT / "wf68k30L_opcode_decoder.vhd"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    control = CONTROL.read_text(encoding="latin-1")
    decoder = DECODER.read_text(encoding="latin-1")

    flush_match = re.search(
        r"\bIPIPE_FLUSH_I\s*<=\s*(?P<body>.*?);",
        control,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not flush_match:
        fail("could not locate IPIPE_FLUSH_I assignment")
    flush_expr = re.sub(r"\s+", " ", flush_match.group("body")).upper()
    if "RESET_CPU = '1'" not in flush_expr:
        fail("IPIPE_FLUSH_I is not asserted during RESET_CPU")

    pipe_match = re.search(
        r"INSTRUCTION_PIPE:\s*process\b(?P<body>.*?)end\s+process\s+INSTRUCTION_PIPE\s*;",
        decoder,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not pipe_match:
        fail("could not locate INSTRUCTION_PIPE process")
    pipe_body = pipe_match.group("body").upper()
    required_resets = [
        "IPIPE.D",
        "IPIPE.C",
        "IPIPE.B",
        "IPIPE_PNTR",
        "IPIPE_D_FAULT",
        "IPIPE_C_FAULT",
        "IPIPE_B_FAULT",
        "BKPT_REQ",
    ]
    flush_branch = re.search(
        r"IF\s+IPIPE_FLUSH\s*=\s*'1'\s+THEN(?P<body>.*?)(?:ELSIF|ELSE)\b",
        pipe_body,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not flush_branch:
        fail("INSTRUCTION_PIPE has no IPIPE_FLUSH branch")
    missing = [name for name in required_resets if not re.search(rf"\b{re.escape(name)}\s*<=", flush_branch.group("body"))]
    if missing:
        fail("IPIPE_FLUSH branch does not reset: " + ", ".join(missing))

    print("PASS: opcode pipe reset/flush contract is explicit")


if __name__ == "__main__":
    main()
