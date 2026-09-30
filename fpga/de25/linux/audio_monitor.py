#!/usr/bin/env python3
"""Record the DE25 audio daemon's final mix from its monitor tap.

usage: audio_monitor.py SECONDS OUTPUT [SOCKET]

Writes raw 48 kHz stereo signed 16-bit little-endian frames, exactly as the
daemon fed the HDMI FIFO (tail silence after an underrun included). Stdlib
only: the board has Python but no numpy. The daemon drops monitor packets a
slow reader does not take, so the socket gets a large receive buffer and
the loop does nothing but read.
"""

import socket
import struct
import sys
import time

MAGIC, VERSION, MONITOR = 0x41554431, 2, 0x80000001
HEADER = struct.Struct("<3I")
MONITOR_FRAMES = 480


def main():
    seconds, output = float(sys.argv[1]), sys.argv[2]
    path = sys.argv[3] if len(sys.argv) > 3 else "/run/astra/audio.sock"
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    connection.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 << 20)
    connection.settimeout(1.0)
    connection.connect(path)
    connection.send(struct.pack("<6I", MAGIC, VERSION, MONITOR, 0, 0, 0))
    reply = struct.unpack("<8I", connection.recv(32))
    if reply[:2] != (MAGIC, 0):
        raise SystemExit("monitor refused: %r" % (reply,))
    frames = 0
    end = time.monotonic() + seconds
    with open(output, "wb") as handle:
        while time.monotonic() < end:
            try:
                packet = connection.recv(HEADER.size + MONITOR_FRAMES * 4)
            except socket.timeout:
                continue
            magic, version, count = HEADER.unpack_from(packet)
            if (magic, version) != (MAGIC, VERSION) or \
                    len(packet) != HEADER.size + count * 4:
                raise SystemExit("bad monitor packet")
            handle.write(packet[HEADER.size:])
            frames += count
    print("AUDIO_MONITOR frames=%d seconds=%.1f" % (frames, seconds))


if __name__ == "__main__":
    main()
