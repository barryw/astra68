#!/usr/bin/env python3
"""Capture one raw RGB frame from Astra's loopback RFB service."""

import argparse
import hashlib
import json
import socket
import struct
import time


def receive_exact(connection, size):
    result = bytearray()
    while len(result) != size:
        block = connection.recv(size - len(result))
        if not block:
            raise RuntimeError("RFB server disconnected")
        result.extend(block)
    return bytes(result)


def connect(host, port):
    deadline = time.monotonic() + 10
    while True:
        try:
            connection = socket.create_connection((host, port), timeout=1)
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.1)
    version = receive_exact(connection, 12)
    if not version.startswith(b"RFB 003."):
        raise RuntimeError(f"invalid RFB version {version!r}")
    connection.sendall(b"RFB 003.008\n")
    security_count = receive_exact(connection, 1)[0]
    if security_count == 0:
        size = struct.unpack(">I", receive_exact(connection, 4))[0]
        raise RuntimeError(receive_exact(connection, size).decode("utf-8"))
    security = receive_exact(connection, security_count)
    if 1 not in security:
        raise RuntimeError(f"RFB None security unavailable: {security!r}")
    connection.sendall(b"\x01")
    result = struct.unpack(">I", receive_exact(connection, 4))[0]
    if result != 0:
        raise RuntimeError(f"RFB security failed: {result}")
    connection.sendall(b"\x01")
    header = receive_exact(connection, 24)
    values = struct.unpack(">HHBBBBHHHBBB3xI", header)
    width, height = values[:2]
    bits, depth, big_endian, true_colour = values[2:6]
    maxima = values[6:9]
    shifts = values[9:12]
    name = receive_exact(connection, values[12]).decode("utf-8")
    expected = (24, 24, 1, 1, (255, 255, 255), (16, 8, 0))
    actual = (bits, depth, big_endian, true_colour, maxima, shifts)
    if actual != expected:
        raise RuntimeError(f"unexpected RFB pixel format: {actual}")
    connection.sendall(struct.pack(">BBHi", 2, 0, 1, 0))
    return connection, width, height, name


def capture(connection, width, height):
    frame = bytearray(width * height * 3)
    connection.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, width, height))
    while True:
        message = receive_exact(connection, 1)[0]
        if message == 2:
            continue
        if message == 3:
            length = struct.unpack(">I", receive_exact(connection, 7)[3:])[0]
            receive_exact(connection, length)
            continue
        if message != 0:
            raise RuntimeError(f"unexpected RFB message {message}")
        rectangle_count = struct.unpack(">H", receive_exact(connection, 3)[1:])[0]
        for _ in range(rectangle_count):
            x, y, rect_width, rect_height, encoding = struct.unpack(
                ">HHHHi", receive_exact(connection, 12))
            if encoding != 0:
                raise RuntimeError(f"unexpected RFB encoding {encoding}")
            pixels = receive_exact(connection, rect_width * rect_height * 3)
            for row in range(rect_height):
                source = row * rect_width * 3
                target = ((y + row) * width + x) * 3
                frame[target:target + rect_width * 3] = \
                    pixels[source:source + rect_width * 3]
        return bytes(frame)


def qmp_execute(stream, command, arguments=None):
    request = {"execute": command}
    if arguments is not None:
        request["arguments"] = arguments
    stream.write(json.dumps(request) + "\n")
    stream.flush()
    while True:
        reply = json.loads(stream.readline())
        if "event" in reply:
            continue
        if "return" in reply:
            return reply["return"]
        raise RuntimeError(reply["error"])


def qmp_pointer_position(path):
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    connection.settimeout(10)
    connection.connect(path)
    with connection, connection.makefile("rw") as stream:
        json.loads(stream.readline())
        qmp_execute(stream, "qmp_capabilities")
        return tuple(qmp_execute(stream, "qom-get", {
            "path": "/machine", "property": name}) for name in (
                "astra-display-cursor-x", "astra-display-cursor-y"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", default=5900, type=int)
    parser.add_argument("--pointer", nargs=2, metavar=("X", "Y"), type=int)
    parser.add_argument("--qmp", default="/run/astra/qmp.sock")
    arguments = parser.parse_args()
    connection, width, height, name = connect(arguments.host, arguments.port)
    try:
        if arguments.pointer is not None:
            x, y = arguments.pointer
            if not 0 <= x < width or not 0 <= y < height:
                parser.error("pointer coordinates must be inside the display")
            connection.sendall(struct.pack(">BBHH", 5, 0, x, y))
        frame = capture(connection, width, height)
    finally:
        connection.close()
    if arguments.pointer is not None:
        actual_pointer = qmp_pointer_position(arguments.qmp)
        if actual_pointer != tuple(arguments.pointer):
            raise RuntimeError(
                f"remote pointer mismatch: {actual_pointer!r}")
    with open(arguments.output, "wb") as output:
        output.write(frame)
    print(f"ASTRA_REMOTE_DESKTOP_RFB PASS name={name!r} "
          f"size={width}x{height} bytes={len(frame)} "
          f"sha256={hashlib.sha256(frame).hexdigest()} "
          f"pointer={arguments.pointer!r}")


if __name__ == "__main__":
    main()
