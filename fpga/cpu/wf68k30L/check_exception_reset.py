#!/usr/bin/env python3
"""Check that pending exception FFs have deterministic reset values."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
RTL = ROOT / "wf68k30L_exception_handler.vhd"


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> None:
    text = RTL.read_text(encoding="latin-1")
    pending_match = re.search(
        r"PENDING:\s*process\b(?P<body>.*?)end\s+process\s+PENDING\s*;",
        text,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not pending_match:
        fail("could not locate PENDING process")

    pending = pending_match.group("body")
    reset_match = re.search(
        r"if\s+RESET\s*=\s*'1'\s+then(?P<body>.*?)(?:elsif|else)\b",
        pending,
        flags=re.IGNORECASE | re.DOTALL,
    )
    if not reset_match:
        fail("PENDING process has no top-level RESET branch")

    reset_body = reset_match.group("body")
    signals = sorted(set(re.findall(r"signal\s+(EX_P_[A-Za-z0-9_]+)\s*:\s*bit\s*;", text)))
    if not signals:
        fail("no EX_P_* pending exception signals found")

    missing = []
    wrong = []
    for sig in signals:
        expected = "'1'" if sig == "EX_P_RESET" else "'0'"
        if not re.search(rf"\b{sig}\s*<=", reset_body):
            missing.append(sig)
        elif not re.search(rf"\b{sig}\s*<=\s*{re.escape(expected)}\s*;", reset_body):
            wrong.append(f"{sig} should reset to {expected}")

    if missing:
        fail("missing RESET assignments: " + ", ".join(missing))
    if wrong:
        fail("; ".join(wrong))
    if not re.search(r'\bIRQ_PEND_I\s*<=\s*"111"\s*;', reset_body):
        fail('IRQ_PEND_I must reset to "111"')

    print(f"PASS: {len(signals)} pending exception FFs have explicit reset values")


if __name__ == "__main__":
    main()
