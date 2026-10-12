#!/usr/bin/env python3
"""An audio buffer is returned only when the host mixer has consumed it."""

import importlib.util
import mmap
import os
from pathlib import Path
import struct
import sys
import tempfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("hostfs", HERE / "test-hostfs.py")
hostfs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hostfs)

CONFIG, BUFFER = 0x02010000, 0x02020000
STREAM_CONFIG, STREAM_RESULT, STREAM_KICK = (hostfs.VESTA + offset
                                           for offset in (0x8e0, 0x8e4, 0x8e8))
MAILBOX_BYTES = 4096 + 32 * 65536


def run(qtest, mailbox):
    qtest.detect_endian()
    generation = qtest.read32(hostfs.HACC_GENERATION)
    for count, period, start in ((3, 1024, 0), (2, 480, 0xffffff00)):
        size = period * 4
        struct.pack_into("=I", mailbox, 64 + 20, start)
        config = struct.pack(">IHH11I", 64, 1, 1, 0, 9, generation, 7,
                             BUFFER, 64 + count * size,
                             2 | 2 << 8 | 44100 << 12, period, count, 0, 0)
        config += bytes(12)
        assert len(config) == 64
        qtest.write(CONFIG, config)
        qtest.write32(STREAM_CONFIG, CONFIG)
        assert qtest.read32(STREAM_RESULT) == 0
        for index in range(count):
            qtest.write(BUFFER + 64 + index * size,
                        bytes([index + 1]) * size)
        qtest.write32(BUFFER + 36, count)
        qtest.write32(STREAM_KICK, 0)
        assert struct.unpack_from("=I", mailbox, 64 + 16)[0] == \
            (start + 2 * size) & 0xffffffff
        assert qtest.read32(BUFFER + 48) == 0, \
            "copying ahead returned buffers before playback"
        # A previous generation's late read cannot release current buffers.
        struct.pack_into("=I", mailbox, 64 + 20, (start - 4) & 0xffffffff)
        qtest.write32(STREAM_KICK, 0)
        assert qtest.read32(BUFFER + 48) == 0
        # A partial buffer does not return its application's slot.
        struct.pack_into("=I", mailbox, 64 + 20,
                         (start + size - 4) & 0xffffffff)
        qtest.write32(STREAM_KICK, 0)
        assert qtest.read32(BUFFER + 48) == 0
        # One complete buffer returns exactly one slot, even across wrap.
        struct.pack_into("=I", mailbox, 64 + 20,
                         (start + size) & 0xffffffff)
        qtest.write32(STREAM_KICK, 0)
        assert qtest.read32(BUFFER + 48) == 1
        assert struct.unpack_from("=I", mailbox, 64 + 16)[0] == \
            (start + min(count, 3) * size) & 0xffffffff
        if count == 3:
            at = (start + 2 * size) % 65536
            assert mailbox[4096 + at:4096 + at + 4] == bytes([3]) * 4
        struct.pack_into("=I", mailbox, 64 + 20,
                         (start + count * size) & 0xffffffff)
        qtest.write32(STREAM_KICK, 0)
        assert qtest.read32(BUFFER + 48) == count
        # Requeue after completion; the native group wraps independently.
        qtest.write32(BUFFER + 36, count + 1)
        qtest.write32(STREAM_KICK, 0)
        assert qtest.read32(BUFFER + 48) == count
        assert struct.unpack_from("=I", mailbox, 64 + 16)[0] == \
            (start + (count + 1) * size) & 0xffffffff
        # A producer cannot retract a buffer already copied into the ring.
        qtest.write32(BUFFER + 36, count)
        qtest.write32(STREAM_KICK, 0)
        assert qtest.read32(BUFFER + 52) == 2
        # Closing never leaves a producer referring to the old buffers.
        qtest.write(CONFIG + 6, struct.pack(">H", 2))
        qtest.write32(STREAM_CONFIG, CONFIG)
        assert qtest.read32(STREAM_RESULT) == 0
        assert struct.unpack_from("=I", mailbox, 64)[0] == 0
    print("ASTRA AUDIO STREAM PLAYBACK BACKPRESSURE PASS")


def main(qemu):
    with tempfile.TemporaryDirectory(prefix="astra-audio-stream-") as work:
        root = Path(work)
        for name in ("work", "sound"):
            (root / name).mkdir()
        rom = root / "test.rom"
        rom.write_bytes(struct.pack(">II", 0x02001000, 0xffe00008))
        path = root / "audio.mailbox"
        with path.open("w+b") as file:
            file.truncate(MAILBOX_BYTES)
            with mmap.mmap(file.fileno(), MAILBOX_BYTES) as mailbox:
                qtest_path = str(root / "qtest.sock")
                command = [qemu, "-machine", "astra68,accel=qtest", "-m", "32M",
                           "-bios", str(rom), "-S", "-display", "none", "-nodefaults",
                           "-qtest", "unix:" + qtest_path + ",server=on,wait=off"]
                environment = dict(os.environ, ASTRA_HOSTFS_ROOT=work,
                                   ASTRA_AUDIO_MAILBOX_PATH=str(path))
                hostfs.booted(command, environment, qtest_path,
                              lambda qtest: run(qtest, mailbox))


if __name__ == "__main__":
    main(sys.argv[1])
