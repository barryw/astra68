from proto import frame, parse, CMD_PING
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
