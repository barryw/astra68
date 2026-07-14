#!/usr/bin/env python3
"""Harte harness wire framing and protocol constants."""

from __future__ import annotations

from dataclasses import dataclass


SYNC_TX = 0x55
SYNC_RX = 0xAA

CMD_RUN = 0x01
CMD_PING = 0x02
CMD_ID = 0x03
CMD_INFO = 0x04

CMD_PONG = 0x80
CMD_RESULT = 0x81
CMD_IDR = 0x83
CMD_INFOR = 0x84
CMD_ERROR = 0xFF

ERROR_NAMES = {
    1: "bad length",
    2: "bad checksum",
    3: "bad command",
    4: "RX FIFO overrun",
}


@dataclass(frozen=True)
class DeviceInfo:
    protocol_major: int
    protocol_minor: int
    max_instruction_bytes: int
    features: int
    build_id: int
    baud: int


def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def frame(command: int, payload: bytes = b"") -> bytes:
    if not 0 <= command <= 0xFF:
        raise ValueError("command must fit in one byte")
    body = bytes((command,)) + bytes(payload)
    length = len(body) + 1
    if length > 0xFF:
        raise ValueError("frame body is too large for the one-byte length")
    return bytes((SYNC_TX, length)) + body + bytes((checksum(body),))


def parse(data: bytes) -> tuple[int, bytes] | None:
    if len(data) < 4 or data[0] != SYNC_RX:
        return None
    length = data[1]
    end = 2 + length
    if length < 2 or len(data) != end:
        return None
    body = data[2:end - 1]
    if not body or checksum(body) != data[end - 1]:
        return None
    return body[0], body[1:]


def parse_device_info(payload: bytes) -> DeviceInfo:
    if len(payload) != 12:
        raise ValueError(f"INFO payload is {len(payload)} bytes, expected 12")
    return DeviceInfo(
        protocol_major=payload[0],
        protocol_minor=payload[1],
        max_instruction_bytes=payload[2],
        features=payload[3],
        build_id=int.from_bytes(payload[4:8], "big"),
        baud=int.from_bytes(payload[8:12], "big"),
    )


def describe_error(payload: bytes) -> str:
    if len(payload) != 2:
        return f"malformed ERROR payload ({len(payload)} bytes)"
    name = ERROR_NAMES.get(payload[0], f"error {payload[0]}")
    return f"{name} for command 0x{payload[1]:02x}"
