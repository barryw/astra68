from sw.harte.host.proto import CMD_PONG, SYNC_RX, checksum
from sw.harte.host.transport import read_frame


class FakeSerial:
    def __init__(self, data: bytes):
        self.data = bytearray(data)

    def read(self, count: int) -> bytes:
        chunk = bytes(self.data[:count])
        del self.data[:count]
        return chunk


def response(command: int, payload: bytes) -> bytes:
    body = bytes((command,)) + payload
    return bytes((SYNC_RX, len(body) + 1)) + body + bytes((checksum(body),))


def test_read_frame_skips_noise_and_decodes_response():
    serial_port = FakeSerial(b"R\x00" + response(CMD_PONG, b"\xa3"))
    assert read_frame(serial_port, 0.1) == (CMD_PONG, b"\xa3")


def test_read_frame_rejects_bad_checksum():
    encoded = bytearray(response(CMD_PONG, b"\xa3"))
    encoded[-1] ^= 1
    assert read_frame(FakeSerial(bytes(encoded)), 0.01) is None


def test_read_frame_resynchronizes_after_corrupt_frame():
    corrupt = bytearray(response(CMD_PONG, b"\x01"))
    corrupt[-1] ^= 1
    serial_port = FakeSerial(bytes(corrupt) + response(CMD_PONG, b"\x02"))
    assert read_frame(serial_port, 0.1) == (CMD_PONG, b"\x02")
