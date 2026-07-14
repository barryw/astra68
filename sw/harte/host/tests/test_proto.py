import pytest

from sw.harte.host.proto import (
    CMD_PING,
    DeviceInfo,
    frame,
    parse,
    parse_device_info,
)


def test_frame_roundtrip():
    f = frame(CMD_PING, b"\xA3")
    assert f[0] == 0x55 and f[2] == CMD_PING
    # emulate device echo with RX sync
    rx = bytes([0xAA]) + f[1:2] + bytes([0x80]) + b"\xA3"
    rx += bytes([(sum(rx[2:]) & 0xFF)])
    cmd, payload = parse(rx)
    assert cmd == 0x80 and payload == b"\xA3"
def test_bad_checksum_rejected():
    assert parse(b"\xAA\x02\x80\xA3\x00") is None


def test_trailing_bytes_and_invalid_lengths_are_rejected():
    assert parse(b"\xAA\x03\x80\xA3\x23\x00") is None
    assert parse(b"\xAA\x01\x80") is None


def test_frame_rejects_oversize_payload():
    with pytest.raises(ValueError):
        frame(CMD_PING, bytes(254))


def test_info_payload_is_typed_and_strict():
    payload = bytes((2, 0, 30, 3)) + bytes.fromhex("12345678") + (460800).to_bytes(4, "big")
    assert parse_device_info(payload) == DeviceInfo(2, 0, 30, 3, 0x12345678, 460800)
    with pytest.raises(ValueError):
        parse_device_info(payload[:-1])
