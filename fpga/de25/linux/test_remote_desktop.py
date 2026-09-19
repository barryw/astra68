#!/usr/bin/env python3
"""Capture one raw RGB frame from Astra's RFB service."""

import argparse
import hashlib
import json
import socket
import struct
import subprocess
import time


def receive_exact(connection, size):
    result = bytearray()
    while len(result) != size:
        block = connection.recv(size - len(result))
        if not block:
            raise RuntimeError("RFB server disconnected")
        result.extend(block)
    return bytes(result)


def vnc_response(challenge, password):
    try:
        raw = password.encode("latin-1")
    except UnicodeEncodeError as error:
        raise RuntimeError("VNC password must contain Latin-1 characters") \
            from error
    if not 1 <= len(raw) <= 8:
        raise RuntimeError("VNC password must contain 1 to 8 characters")
    key = bytes(int(f"{byte:08b}"[::-1], 2) for byte in raw.ljust(8, b"\0"))
    result = subprocess.run([
        "openssl", "enc", "-des-ecb", "-provider", "legacy",
        "-K", key.hex(), "-nopad"], input=challenge, capture_output=True,
        check=False)
    if result.returncode != 0 or len(result.stdout) != 16:
        raise RuntimeError("OpenSSL DES failed: " +
                           result.stderr.decode("utf-8").strip())
    return result.stdout


def connect(host, port, password=None, macos_format=False,
            pointer_position=False):
    deadline = time.monotonic() + 10
    while True:
        try:
            connection = socket.create_connection((host, port), timeout=1)
            break
        except OSError:
            if time.monotonic() >= deadline:
                raise
            time.sleep(0.1)
    connection.settimeout(None)
    version = receive_exact(connection, 12)
    if not version.startswith(b"RFB 003."):
        raise RuntimeError(f"invalid RFB version {version!r}")
    connection.sendall(b"RFB 003.008\n")
    security_count = receive_exact(connection, 1)[0]
    if security_count == 0:
        size = struct.unpack(">I", receive_exact(connection, 4))[0]
        raise RuntimeError(receive_exact(connection, size).decode("utf-8"))
    security = receive_exact(connection, security_count)
    wanted_security = 2 if password is not None else 1
    if wanted_security not in security:
        name = "VNC authentication" if password is not None else "None"
        raise RuntimeError(f"RFB {name} security unavailable: {security!r}")
    connection.sendall(bytes([wanted_security]))
    if password is not None:
        connection.sendall(vnc_response(receive_exact(connection, 16),
                                        password))
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
    expected = (24, 24, 0, 1, (255, 255, 255), (0, 8, 16))
    actual = (bits, depth, big_endian, true_colour, maxima, shifts)
    if actual != expected:
        raise RuntimeError(f"unexpected RFB pixel format: {actual}")
    bytes_per_pixel = 3
    if macos_format:
        connection.sendall(struct.pack(">B3xBBBBHHHBBB3x", 0,
                                       32, 32, 0, 1,
                                       255, 255, 255, 16, 8, 0))
        bytes_per_pixel = 4
    encodings = [-239, -232, 0] if pointer_position else [0]
    connection.sendall(struct.pack(">BBH", 2, 0, len(encodings)) +
                       b"".join(struct.pack(">i", value)
                                for value in encodings))
    return connection, width, height, name, bytes_per_pixel


def capture(connection, width, height, bytes_per_pixel, previous=None,
            incremental=False):
    frame = bytearray(previous or bytes(width * height * 3))
    pointer = None
    connection.sendall(struct.pack(">BBHHHH", 3, incremental,
                                   0, 0, width, height))
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
            if encoding == -232:
                pointer = (x, y)
                continue
            if encoding == -239 and rect_width == 0 and rect_height == 0:
                continue
            if encoding != 0:
                raise RuntimeError(f"unexpected RFB encoding {encoding}")
            pixels = receive_exact(
                connection, rect_width * rect_height * bytes_per_pixel)
            if bytes_per_pixel == 4:
                rgb = bytearray(rect_width * rect_height * 3)
                rgb[0::3] = pixels[2::4]
                rgb[1::3] = pixels[1::4]
                rgb[2::3] = pixels[0::4]
                pixels = rgb
            for row in range(rect_height):
                source = row * rect_width * 3
                target = ((y + row) * width + x) * 3
                frame[target:target + rect_width * 3] = \
                    pixels[source:source + rect_width * 3]
        return bytes(frame), pointer


def send_keys(connection, text):
    for character in text:
        if character == "\n":
            symbol = 0xff0d
        elif ord(character) <= 0xff:
            symbol = ord(character)
        else:
            raise RuntimeError(f"unsupported RFB test character {character!r}")
        for down in (1, 0):
            connection.sendall(struct.pack(">BBHI", 4, down, 0, symbol))


def send_pointer(connection, x, y, buttons=0):
    connection.sendall(struct.pack(">BBHH", 5, buttons, x, y))


def double_click(connection, x, y):
    send_pointer(connection, x, y)
    for _ in range(2):
        send_pointer(connection, x, y, 1)
        send_pointer(connection, x, y)


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
    parser.add_argument("--password-file")
    parser.add_argument("--macos-format", action="store_true")
    parser.add_argument("--verify-rfb-pointer", action="store_true")
    parser.add_argument("--keys", help="type ASCII text after connecting")
    pointer_action = parser.add_mutually_exclusive_group()
    pointer_action.add_argument("--pointer", nargs=2, metavar=("X", "Y"),
                                type=int)
    pointer_action.add_argument("--double-click", nargs=2,
                                metavar=("X", "Y"), type=int)
    parser.add_argument("--qmp", default="/run/astra/qmp.sock")
    arguments = parser.parse_args()
    if arguments.verify_rfb_pointer and arguments.pointer is None:
        parser.error("--verify-rfb-pointer requires --pointer")
    password = None
    if arguments.password_file is not None:
        with open(arguments.password_file, encoding="latin-1") as source:
            password = source.readline().rstrip("\r\n")
    connection, width, height, name, bytes_per_pixel = connect(
        arguments.host, arguments.port, password, arguments.macos_format,
        arguments.verify_rfb_pointer)
    mover = None
    try:
        coordinates = arguments.double_click or arguments.pointer
        frame = None
        if coordinates is not None:
            x, y = coordinates
            if not 0 <= x < width or not 0 <= y < height:
                parser.error("pointer coordinates must be inside the display")
            if arguments.verify_rfb_pointer:
                frame, _ = capture(connection, width, height,
                                   bytes_per_pixel)
                mover = connect(arguments.host, arguments.port, password)[0]
                send_pointer(mover, x, y)
            elif arguments.double_click:
                double_click(connection, x, y)
                time.sleep(2)
            else:
                send_pointer(connection, x, y)
        if arguments.keys is not None:
            send_keys(connection, arguments.keys)
        frame, rfb_pointer = capture(
            connection, width, height, bytes_per_pixel, frame,
            frame is not None)
    finally:
        if mover is not None:
            mover.close()
        connection.close()
    if arguments.pointer is not None and arguments.qmp:
        actual_pointer = qmp_pointer_position(arguments.qmp)
        if actual_pointer != tuple(arguments.pointer):
            raise RuntimeError(
                f"remote pointer mismatch: {actual_pointer!r}")
    if arguments.verify_rfb_pointer and \
            rfb_pointer != tuple(arguments.pointer or ()):
        raise RuntimeError(f"RFB pointer mismatch: {rfb_pointer!r}")
    with open(arguments.output, "wb") as output:
        output.write(frame)
    print(f"ASTRA_REMOTE_DESKTOP_RFB PASS name={name!r} "
          f"size={width}x{height} bytes={len(frame)} "
          f"sha256={hashlib.sha256(frame).hexdigest()} "
          f"pointer={arguments.pointer!r}")


if __name__ == "__main__":
    main()
