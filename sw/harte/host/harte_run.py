#!/usr/bin/env python3
"""Stream the supported Tom Harte register-only subset to an Astra board."""

from __future__ import annotations

import argparse
from collections import Counter
import glob
import os
from pathlib import Path
import struct
import sys
import time
from typing import Iterable

sys.path.insert(0, str(Path(__file__).resolve().parent))

from flags import mask_for_opcode
from harte_case import Case, CaseError, build_case, load, scope_reason
from proto import CMD_RUN, frame, parse


CMD_RESULT = 0x81
RESULT_PAYLOAD_BYTES = 61
DEFAULT_BAUD = 115200


def find_port() -> str:
    if os.environ.get("ASTRA_PORT"):
        return os.environ["ASTRA_PORT"]
    for pattern in ("/dev/ttyUSB*", "/dev/cu.usbserial*"):
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[0]
    return "/dev/ttyUSB0"


def encode_run(case: Case) -> bytes:
    payload = b"".join(struct.pack(">I", value) for value in case.d + case.a)
    payload += bytes((case.ccr, case.ilen)) + case.instr
    return frame(CMD_RUN, payload)


def decode_result(payload: bytes) -> tuple[tuple[int, ...], tuple[int, ...], int]:
    if len(payload) != RESULT_PAYLOAD_BYTES:
        raise ValueError(f"RESULT payload is {len(payload)} bytes, expected {RESULT_PAYLOAD_BYTES}")
    regs = tuple(struct.unpack_from(">I", payload, offset)[0]
                 for offset in range(0, 60, 4))
    return regs[:8], regs[8:], payload[60]


def compare_result(case: Case, payload: bytes) -> list[str]:
    actual_d, actual_a, actual_ccr = decode_result(payload)
    mismatches = []
    for index, (actual, expected) in enumerate(zip(actual_d, case.fd)):
        if actual != expected:
            mismatches.append(f"D{index}={actual:08x}, expected {expected:08x}")
    for index, (actual, expected) in enumerate(zip(actual_a, case.fa)):
        if actual != expected:
            mismatches.append(f"A{index}={actual:08x}, expected {expected:08x}")
    mask = mask_for_opcode(case.opcode)
    if actual_ccr & mask != case.fccr & mask:
        mismatches.append(
            f"CCR={actual_ccr & mask:02x}, expected {case.fccr & mask:02x}, mask {mask:02x}"
        )
    return mismatches


def _read_exact(serial_port, count: int, deadline: float) -> bytes:
    data = bytearray()
    while len(data) < count and time.monotonic() < deadline:
        chunk = serial_port.read(count - len(data))
        if chunk:
            data.extend(chunk)
    return bytes(data)


def read_result(serial_port, timeout: float) -> bytes | None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        sync = _read_exact(serial_port, 1, deadline)
        if not sync:
            return None
        if sync[0] != 0xAA:
            continue
        length_raw = _read_exact(serial_port, 1, deadline)
        if not length_raw:
            return None
        body = _read_exact(serial_port, length_raw[0], deadline)
        if len(body) != length_raw[0]:
            return None
        decoded = parse(sync + length_raw + body)
        if decoded is not None and decoded[0] == CMD_RESULT:
            return decoded[1]
    return None


def _write_frame(serial_port, encoded: bytes, pace_seconds: float) -> None:
    if pace_seconds <= 0:
        serial_port.write(encoded)
    else:
        for value in encoded:
            serial_port.write(bytes((value,)))
            time.sleep(pace_seconds)
    serial_port.flush()


def execute_case(serial_port, case: Case, retries: int, timeout: float,
                 pace_seconds: float) -> bytes | None:
    encoded = encode_run(case)
    for _ in range(retries + 1):
        serial_port.reset_input_buffer()
        _write_frame(serial_port, encoded, pace_seconds)
        payload = read_result(serial_port, timeout)
        if payload is not None:
            return payload
    return None


def iter_cases(paths: Iterable[str], limit: int | None = None):
    admitted = 0
    skipped = Counter()
    for path in paths:
        for raw in load(path):
            reason = scope_reason(raw)
            if reason is not None:
                skipped[reason] += 1
                continue
            try:
                case = build_case(raw)
            except CaseError as exc:
                skipped[str(exc)] += 1
                continue
            yield path, case, skipped
            admitted += 1
            if limit is not None and admitted >= limit:
                return


def dry_run(paths: list[str], limit: int | None) -> int:
    admitted = 0
    skipped = Counter()
    total = 0
    for path in paths:
        for raw in load(path):
            total += 1
            reason = scope_reason(raw)
            if reason is not None:
                skipped[reason] += 1
                continue
            try:
                build_case(raw)
            except CaseError as exc:
                skipped[str(exc)] += 1
                continue
            admitted += 1
            if limit is not None and admitted >= limit:
                break
        if limit is not None and admitted >= limit:
            break
    print(f"vectors={total} in_scope={admitted} skipped={sum(skipped.values())}")
    for reason, count in skipped.most_common():
        print(f"  skip {count:7d}  {reason}")
    return 0 if admitted else 1


def run(paths: list[str], serial_port, limit: int | None, retries: int,
        timeout: float, pace_seconds: float) -> int:
    passed = 0
    failed = 0
    skipped = Counter()
    for path in paths:
        for raw in load(path):
            reason = scope_reason(raw)
            if reason is not None:
                skipped[reason] += 1
                continue
            case = build_case(raw)
            payload = execute_case(serial_port, case, retries, timeout, pace_seconds)
            if payload is None:
                failed += 1
                print(f"FAIL {case.name}: no valid RESULT after {retries + 1} attempts")
            else:
                mismatches = compare_result(case, payload)
                if mismatches:
                    failed += 1
                    print(f"FAIL {case.name}: {'; '.join(mismatches)}")
                else:
                    passed += 1
            if (passed + failed) % 100 == 0:
                print(f"progress pass={passed} fail={failed} skipped={sum(skipped.values())}")
            if limit is not None and passed + failed >= limit:
                break
        if limit is not None and passed + failed >= limit:
            break
    print(f"PASS={passed} FAIL={failed} SKIP={sum(skipped.values())}")
    for reason, count in skipped.most_common():
        print(f"  skip {count:7d}  {reason}")
    return 0 if passed and failed == 0 else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vectors", nargs="+", help="Harte .json or .json.gz files")
    parser.add_argument("--limit", type=int, help="maximum admitted cases")
    parser.add_argument("--dry-run", action="store_true", help="parse and filter without serial I/O")
    parser.add_argument("--port", default=find_port())
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=0.75)
    parser.add_argument("--retries", type=int, default=2)
    parser.add_argument("--pace-ms", type=float, default=1.0,
                        help="delay between TX bytes; use 0 after the SoC has an RX FIFO")
    args = parser.parse_args(argv)

    if args.limit is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    if args.dry_run:
        return dry_run(args.vectors, args.limit)

    try:
        import serial
    except ImportError:
        print("pyserial is required for hardware runs", file=sys.stderr)
        return 2

    with serial.Serial(args.port, args.baud, timeout=min(args.timeout, 0.1)) as serial_port:
        return run(args.vectors, serial_port, args.limit, args.retries,
                   args.timeout, args.pace_ms / 1000.0)


if __name__ == "__main__":
    sys.exit(main())
