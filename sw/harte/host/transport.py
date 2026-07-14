#!/usr/bin/env python3
"""Serial transport helpers for the Harte harness."""

from __future__ import annotations

import glob
import os
import time

try:
    from .proto import SYNC_RX, frame, parse
except ImportError:  # Direct script execution from sw/harte/host.
    from proto import SYNC_RX, frame, parse


def find_port() -> str:
    configured = os.environ.get("ASTRA_PORT")
    if configured:
        return configured
    for pattern in ("/dev/ttyUSB*", "/dev/cu.usbserial*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return "/dev/ttyUSB0"


def read_exact(serial_port, count: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < count and time.monotonic() < deadline:
        chunk = serial_port.read(count - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_frame(serial_port, timeout: float) -> tuple[int, bytes] | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sync = read_exact(serial_port, 1, deadline)
        if not sync:
            return None
        if sync[0] != SYNC_RX:
            continue
        length_raw = read_exact(serial_port, 1, deadline)
        if not length_raw:
            return None
        body = read_exact(serial_port, length_raw[0], deadline)
        if len(body) != length_raw[0]:
            return None
        decoded = parse(sync + length_raw + body)
        if decoded is not None:
            return decoded
    return None


def write_frame(serial_port, command: int, payload: bytes = b"",
                pace_seconds: float = 0.0) -> None:
    encoded = frame(command, payload)
    if pace_seconds <= 0:
        serial_port.write(encoded)
    else:
        for value in encoded:
            serial_port.write(bytes((value,)))
            time.sleep(pace_seconds)
    serial_port.flush()


def transact(serial_port, command: int, payload: bytes, timeout: float,
             pace_seconds: float = 0.0) -> tuple[int, bytes] | None:
    serial_port.reset_input_buffer()
    write_frame(serial_port, command, payload, pace_seconds)
    return read_frame(serial_port, timeout)
