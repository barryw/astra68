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

print("ASTRA_REMOTE_DESKTOP_CLIENT PASS")
