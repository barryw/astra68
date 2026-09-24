#!/usr/bin/env python3
"""Exercise the Linux audio socket with real PCM and rejected requests."""

import argparse
import os
import socket
import struct
import subprocess
import tempfile
import time

MAGIC = 0x41554431
OPEN, WRITE, GAIN, STATUS, CLOSE, FINISH, PAUSE, CLEAR = range(1, 9)
OK, PROTOCOL, INVALID, BAD_HANDLE, BUSY = 0, 1, 8, 9, 14
REQUEST = struct.Struct("=6I")
REPLY = struct.Struct("=8I")
FRAME_BYTES = 6
FRAMES_PER_PACKET = 1024


def submit(connection, operation, handle=0, value=0, data=b"", magic=MAGIC):
    packet = REQUEST.pack(magic, 2, operation, handle, value, len(data)) + data
    if connection.send(packet) != len(packet):
        raise RuntimeError("short audio request")
    answer = connection.recv(REPLY.size)
    if len(answer) != REPLY.size:
        raise RuntimeError("short audio reply")
    result = REPLY.unpack(answer)
    if result[0] != MAGIC:
        raise RuntimeError("bad audio reply magic")
    return result


def require_status(answer, expected):
    if answer[1] != expected:
        raise RuntimeError(f"audio status {answer[1]}, expected {expected}")
    return answer


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--socket", required=True)
    parser.add_argument("--pcm", required=True)
    parser.add_argument("--voices", type=int, choices=(1, 2, 16), default=1)
    parser.add_argument("--owner-binary")
    arguments = parser.parse_args()
    with open(arguments.pcm, "rb") as source:
        pcm = source.read()
    if not pcm or len(pcm) % FRAME_BYTES:
        raise RuntimeError("PCM must contain complete 48 kHz stereo s24le frames")
    if arguments.owner_binary:
        with tempfile.TemporaryDirectory(prefix="astra-audio-owner-") as work:
            second = os.path.join(work, "second.sock")
            attempt = subprocess.run(
                [arguments.owner_binary, "--socket", second],
                capture_output=True, text=True, timeout=5, check=False)
            if attempt.returncode == 0 or "Device or resource busy" not in attempt.stderr:
                raise RuntimeError("second audio owner was not rejected: " +
                                   attempt.stderr)
            if os.path.exists(second):
                raise RuntimeError("rejected owner created a socket")

    connections = []
    try:
        for _ in range(arguments.voices):
            connection = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
            connection.settimeout(2.0)
            connection.connect(arguments.socket)
            connections.append(connection)
        connection = connections[0]
        require_status(submit(connection, STATUS, magic=0), PROTOCOL)
        require_status(submit(connection, WRITE, 999, data=b"12345"), INVALID)
        require_status(submit(connection, WRITE, 999, data=b"123456"),
                       BAD_HANDLE)
        require_status(submit(connection, OPEN, value=0), INVALID)
        handles = []
        for index, client in enumerate(connections):
            handle = require_status(submit(client, OPEN, value=1), OK)[2]
            if not handle:
                raise RuntimeError("audio provider returned a zero voice handle")
            handles.append(handle)
            require_status(submit(client, GAIN, handle,
                                  65536 // (index + 1)), OK)
        if len(connections) >= 2:
            require_status(submit(connections[1], STATUS, handles[0]),
                           BAD_HANDLE)
        require_status(submit(connection, PAUSE, handles[0], value=2),
                       INVALID)
        require_status(submit(connection, PAUSE, handles[0], value=1), OK)
        require_status(submit(connection, CLEAR, handles[0], value=1),
                       INVALID)
        require_status(submit(connection, CLEAR, handles[0]), OK)
        require_status(submit(connection, PAUSE, handles[0], value=0), OK)

        started = time.monotonic()
        for offset in range(0, len(pcm), FRAMES_PER_PACKET * FRAME_BYTES):
            chunk = pcm[offset:offset + FRAMES_PER_PACKET * FRAME_BYTES]
            for client, handle in zip(connections, handles):
                while True:
                    answer = submit(client, WRITE, handle, data=chunk)
                    if answer[1] == OK:
                        break
                    if answer[1] != BUSY:
                        raise RuntimeError(f"audio write failed: {answer[1]}")
                    time.sleep(0.001)
        for client, handle in zip(connections, handles):
            require_status(submit(client, FINISH, handle), OK)
            require_status(submit(client, WRITE, handle, data=pcm[:6]),
                           INVALID)
        deadline = time.monotonic() + 5.0
        while time.monotonic() < deadline:
            answers = [require_status(submit(client, STATUS, handle), OK)
                       for client, handle in zip(connections, handles)]
            if all(answer[3] == 0 and answer[4] == 0 for answer in answers):
                break
            time.sleep(0.002)
        else:
            raise RuntimeError("audio voice did not drain")
        elapsed = time.monotonic() - started
        for answer in answers:
            if answer[5:] != (0, 0, 0):
                raise RuntimeError(f"audio continuity failure: {answer[5:]}")
        for client, handle in zip(connections, handles):
            require_status(submit(client, CLOSE, handle), OK)
            require_status(submit(client, STATUS, handle), BAD_HANDLE)
        print(f"ASTRA AUDIO SOCKET PASS voices={arguments.voices} "
              f"frames={len(pcm) // FRAME_BYTES} "
              f"elapsed={elapsed:.3f}s underruns=0 overflows=0 gaps=0")
    finally:
        for connection in connections:
            connection.close()


if __name__ == "__main__":
    main()
