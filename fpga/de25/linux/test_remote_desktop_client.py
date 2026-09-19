#!/usr/bin/env python3
"""Check RFB input packet generation without a live server."""

import importlib.util
from pathlib import Path
import struct


source = Path(__file__).with_name("test_remote_desktop.py")
spec = importlib.util.spec_from_file_location("astra_rfb_client", source)
client = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client)

assert client.vnc_response(bytes(range(16)), "password").hex() == \
    "b866924125c8eebb9debc1db61c538e2"
try:
    client.vnc_response(bytes(16), "123456789")
except RuntimeError as error:
    assert "1 to 8 characters" in str(error)
else:
    raise AssertionError("overlong VNC password was accepted")


class Connection:
    def __init__(self):
        self.data = bytearray()

    def sendall(self, data):
        self.data.extend(data)


class ReceiveConnection(Connection):
    def __init__(self, incoming):
        super().__init__()
        self.incoming = bytearray(incoming)

    def recv(self, size):
        result = self.incoming[:size]
        del self.incoming[:size]
        return bytes(result)


connection = Connection()
client.send_keys(connection, "a\n")
assert connection.data == b"".join(
    struct.pack(">BBHI", 4, down, 0, symbol)
    for symbol in (ord("a"), 0xff0d) for down in (1, 0))

try:
    client.send_keys(Connection(), "€")
except RuntimeError as error:
    assert "unsupported RFB test character" in str(error)
else:
    raise AssertionError("non-Latin-1 input was accepted")

connection = Connection()
client.double_click(connection, 70, 90)
assert connection.data == b"".join(
    struct.pack(">BBHH", 5, buttons, 70, 90)
    for buttons in (0, 1, 0, 1, 0))

previous = b"abcdef"
update = struct.pack(">BBH", 0, 0, 2) + \
    struct.pack(">HHHHi", 0, 0, 0, 0, -239) + \
    struct.pack(">HHHHi", 1, 0, 0, 0, -232)
connection = ReceiveConnection(update)
frame, pointer = client.capture(connection, 2, 1, 3, previous, True)
assert frame == previous
assert pointer == (1, 0)
assert connection.data == struct.pack(">BBHHHH", 3, 1, 0, 0, 2, 1)

connection = ReceiveConnection(
    struct.pack(">BBH", 0, 0, 1) +
    struct.pack(">HHHHi", 0, 0, 0, 0, -999))
try:
    client.capture(connection, 2, 1, 3)
except RuntimeError as error:
    assert "unexpected RFB encoding" in str(error)
else:
    raise AssertionError("unknown RFB encoding was accepted")

print("ASTRA_REMOTE_DESKTOP_CLIENT PASS")
