#!/usr/bin/env python3
"""Verify the DE25's mixed PCM reaches a QEMU-audio RFB viewer."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import socket
import struct
import sys
import time

from test_remote_desktop import connect, receive_exact


MAGIC = 0x41554431
VERSION = 2


def select_marker(samples):
    for offset in range(4800 * 4, len(samples) - 128 + 1, 128):
        marker = samples[offset:offset + 128]
        if any(marker) and samples.find(marker) == offset:
            return offset, marker
    raise ValueError("PCM sample has no distinctive non-silent marker")


def test_select_marker():
    sample = bytearray(4800 * 4 + 256)
    sample[4800 * 4 + 128:4800 * 4 + 256] = bytes(range(128))
    offset, marker = select_marker(sample)
    assert offset == 4800 * 4 + 128 and marker == bytes(range(128))
    try:
        select_marker(bytearray(4800 * 4 + 256))
    except ValueError:
        pass
    else:
        raise AssertionError("silent PCM was accepted as a sync marker")


def receive_audio(connection):
    while True:
        kind = receive_exact(connection, 1)[0]
        if kind == 0:
            rectangles = struct.unpack(">H", receive_exact(connection, 3)[1:])[0]
            for _ in range(rectangles):
                rectangle = receive_exact(connection, 12)
                x, y, width, height, encoding = struct.unpack(">HHHHi", rectangle)
                if encoding != -259 or (x, y, width, height) != (0, 0, 0, 0):
                    raise AssertionError((x, y, width, height, encoding))
            return "advertised", b""
        if kind != 255:
            raise AssertionError(f"unexpected RFB message {kind}")
        audio_kind, operation = struct.unpack(">BH", receive_exact(connection, 3))
        if audio_kind != 1:
            raise AssertionError(f"unexpected QEMU message {audio_kind}")
        if operation == 2:
            size = struct.unpack(">I", receive_exact(connection, 4))[0]
            if size == 0 or size > 480 * 4 or size % 4:
                raise AssertionError(f"invalid PCM length {size}")
            return "data", receive_exact(connection, size)
        if operation == 1:
            return "begin", b""
        if operation == 0:
            return "end", b""
        raise AssertionError(f"unexpected audio operation {operation}")


def host_request(connection, operation, handle=0, value=0, data=b""):
    connection.sendall(struct.pack("<6I", MAGIC, VERSION, operation,
                                   handle, value, len(data)) + data)
    reply = struct.unpack("<8I", receive_exact(connection, 32))
    if reply[:2] != (MAGIC, 0):
        raise AssertionError(f"audio host rejected operation {operation}: {reply}")
    return reply


def play_pcm(path, samples):
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    connection.settimeout(5)
    connection.connect(path)
    try:
        handle = host_request(connection, 1, value=2)[2]
        if not handle:
            raise AssertionError("audio host returned a zero handle")
        queued = 0
        deadline = time.monotonic() + len(samples) / (48000 * 4) + 5
        for offset in range(0, len(samples), 1024 * 4):
            data = samples[offset:offset + 1024 * 4]
            frames = len(data) // 4
            while queued + frames > 4096:
                if time.monotonic() >= deadline:
                    raise AssertionError("audio host queue did not drain")
                time.sleep(0.002)
                queued = host_request(connection, 4, handle=handle)[3]
            queued = host_request(connection, 2, handle=handle, data=data)[3]
        host_request(connection, 6, handle=handle)
        while host_request(connection, 4, handle=handle)[3] != 0:
            if time.monotonic() >= deadline:
                raise AssertionError("audio host voice did not finish")
            time.sleep(0.002)
        host_request(connection, 5, handle=handle)
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=5900)
    parser.add_argument("--password-file")
    parser.add_argument("--audio-socket", default="/run/astra/audio.sock")
    parser.add_argument("--pcm-file", required=True,
                        help="raw 48 kHz stereo S16BE sample, e.g. startup.pcm")
    arguments = parser.parse_args()
    with open(arguments.pcm_file, "rb") as source:
        pcm = source.read()
    if not pcm or len(pcm) % 4:
        parser.error("--pcm-file must be nonempty stereo S16BE")
    expected = bytearray(len(pcm))
    expected[0::2] = pcm[1::2]
    expected[1::2] = pcm[0::2]
    password = None
    if arguments.password_file:
        with open(arguments.password_file, encoding="latin-1") as source:
            password = source.readline().rstrip("\r\n")

    connection = connect(arguments.host, arguments.port, password)[0]
    connection.settimeout(5)
    try:
        # A client that did not negotiate audio must not control the stream.
        connection.sendall(b"\xff\x01\x00\x00")
        if connection.recv(1) != b"":
            raise AssertionError("unadvertised audio was accepted")
    finally:
        connection.close()

    connection = connect(arguments.host, arguments.port, password)[0]
    connection.settimeout(5)
    try:
        connection.sendall(struct.pack(">BBHii", 2, 0, 2, -259, 0))
        assert receive_audio(connection)[0] == "advertised"
        connection.sendall(b"\xff\x01\x00\x02\x03\x01\x00\x00\xbb\x80")
        if connection.recv(1) != b"":
            raise AssertionError("invalid mono audio format was accepted")
    finally:
        connection.close()

    connection = connect(arguments.host, arguments.port, password)[0]
    connection.settimeout(5)
    try:
        connection.sendall(struct.pack(">BBHii", 2, 0, 2, -259, 0))
        assert receive_audio(connection)[0] == "advertised"
        connection.sendall(b"\xff\x01\x00\x02\x03\x02\x00\x00\xbb\x80")
        connection.sendall(b"\xff\x01\x00\x00")
        assert receive_audio(connection)[0] == "begin"
        with ThreadPoolExecutor(max_workers=1) as executor:
            playback = executor.submit(play_pcm, arguments.audio_socket, pcm)
            received = bytearray()
            marker_offset, marker = select_marker(expected)
            start = -1
            deadline = time.monotonic() + len(pcm) / (48000 * 4) + 5
            while time.monotonic() < deadline:
                kind, data = receive_audio(connection)
                if kind == "data":
                    received.extend(data)
                    if start < 0:
                        marker_at = received.find(marker)
                        if marker_at >= 0:
                            start = marker_at - marker_offset
                            if start < 0:
                                raise AssertionError("PCM prefix was dropped")
                    if start >= 0 and len(received) >= start + len(expected):
                        break
            playback.result(timeout=5)
        if start < 0 or received[start:start + len(expected)] != expected:
            mismatch = next((index for index, (actual, wanted) in enumerate(
                zip(received[start:], expected)) if actual != wanted), -1)
            after_marker = next((index for index, (actual, wanted) in enumerate(
                zip(received[start + marker_offset:],
                    expected[marker_offset:]), marker_offset)
                if actual != wanted), -1)
            raise AssertionError(
                f"mixed PCM corrupted or dropped: received={len(received)} "
                f"expected={len(expected)} start={start} mismatch={mismatch} "
                f"after_marker={after_marker} "
                f"actual={received[start:start + 24].hex()} "
                f"wanted={expected[:24].hex()}")
        connection.sendall(b"\xff\x01\x00\x01")
        # Audio packets already queued may precede END.
        for _ in range(200):
            if receive_audio(connection)[0] == "end":
                break
        else:
            raise AssertionError("audio END was not delivered")
    finally:
        connection.close()
    print(f"ASTRA_REMOTE_DESKTOP_AUDIO PASS bytes={len(expected)}")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        test_select_marker()
        print("ASTRA_REMOTE_DESKTOP_AUDIO_MARKER PASS")
    else:
        main()
