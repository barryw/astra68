#!/usr/bin/env python3
"""Query and identify the Harte harness currently loaded on the board."""

from __future__ import annotations

import os
from pathlib import Path
import sys
import time

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from proto import CMD_ID, CMD_IDR, CMD_INFO, CMD_INFOR, parse_device_info
    from transport import find_port, transact
else:
    from .proto import CMD_ID, CMD_IDR, CMD_INFO, CMD_INFOR, parse_device_info
    from .transport import find_port, transact


LOG = Path(__file__).resolve().parents[1] / "BUILD_LOG.md"
DEFAULT_BAUD = int(os.environ.get("ASTRA_BAUD", "460800"))


def query_build_id(serial_port, tries: int = 5) -> tuple[int, str] | None:
    for _ in range(tries):
        response = transact(serial_port, CMD_INFO, b"", 0.5)
        if response and response[0] == CMD_INFOR:
            info = parse_device_info(response[1])
            return info.build_id, f"protocol={info.protocol_major}.{info.protocol_minor} baud={info.baud}"

        response = transact(serial_port, CMD_ID, b"", 0.5)
        if response and response[0] == CMD_IDR and len(response[1]) == 4:
            return int.from_bytes(response[1], "big"), "legacy ID response"
    return None


def log_line(build_id: int) -> str | None:
    try:
        for line in LOG.read_text().splitlines():
            if f"0x{build_id:08x}" in line.lower():
                return line.strip()
    except FileNotFoundError:
        pass
    return None


def main() -> int:
    try:
        import serial
    except ImportError:
        print("pyserial is required", file=sys.stderr)
        return 2

    try:
        with serial.Serial(find_port(), DEFAULT_BAUD, timeout=0.1) as serial_port:
            time.sleep(0.25)
            result = query_build_id(serial_port)
    except OSError as exc:
        print(f"device: serial failure: {exc}", file=sys.stderr)
        return 2

    if result is None:
        print("device: NO RESPONSE (wrong baud, non-harness image, or wedged)")
        return 2

    build_id, details = result
    print(f"device BUILD_ID = 0x{build_id:08x} ({details})")
    line = log_line(build_id)
    print("  " + line if line else "  (not in BUILD_LOG.md - unlogged build)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
