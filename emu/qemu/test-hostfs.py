#!/usr/bin/env python3
"""Exercise the Astra generic host filesystem transport at its trust boundary."""

import argparse
import os
import socket
import struct
import subprocess
import tempfile
import time


VESTA = 0xFFF00000
HACC_ID = VESTA + 0x880
HACC_VERSION = VESTA + 0x884
HACC_CAPS = VESTA + 0x888
HACC_STATE = VESTA + 0x88C
HACC_GENERATION = VESTA + 0x890
HACC_MAX_TRANSFER = VESTA + 0x894
HACC_MAX_COMMANDS = VESTA + 0x898
HACC_REQ_BUFFER = VESTA + 0x89C
HACC_REQ_BYTES = VESTA + 0x8A0
HACC_REQ_COUNT = VESTA + 0x8A4
HACC_EXECUTE = VESTA + 0x8A8
HACC_STATUS = VESTA + 0x8AC
HACC_COMPLETED = VESTA + 0x8B0
HACC_RESET = VESTA + 0x8B4
HACC_OWNER = VESTA + 0x8B8
HACC_RELEASE_OWNER = VESTA + 0x8BC
HACC_SUBMIT = VESTA + 0x8C0
HACC_SUBMIT_RESULT = VESTA + 0x8C4
HACC_CHANNEL_CONFIG = VESTA + 0x8C8
HACC_CHANNEL_RESULT = VESTA + 0x8CC
HACC_CHANNEL_PENDING = VESTA + 0x8D0
HACC_CHANNEL_ACK = VESTA + 0x8D4
HACC_INFLIGHT = VESTA + 0x8D8
HACC_MAX_INFLIGHT = VESTA + 0x8DC

BUFFER = 0x02010000
SUBMISSION = 0x0200F000
CHANNEL_CONFIG = 0x0200F040
CHANNEL_APERTURE = 0xFFD00000
COMMAND_SIZE = 512
SUBMISSION_SIZE = 64
CHANNEL_HEADER_SIZE = 64
PATH_MAX = 192

FS_OPEN = 1
FS_CLOSE = 2
FS_READ = 3
FS_WRITE = 4
FS_TRUNCATE = 6
FS_STAT = 7
FS_READDIR = 8
FS_MKDIR = 9
FS_RENAME = 11
FS_READLINK = 13
FS_SYMLINK = 14

OPEN_READ = 1 << 0
OPEN_WRITE = 1 << 1
OPEN_CREATE = 1 << 2
OPEN_DIRECTORY = 1 << 4
OPEN_EXCLUSIVE = 1 << 5

STATUS_OK = 0
STATUS_INVALID = 8
STATUS_BAD_HANDLE = 9
STATUS_ACCESS = 6
STATUS_IS_DIR = 5
STATUS_LOOP = 18
KIND_SYMLINK = 3
SYSCALL_INVALID_ARGUMENT = 2


class QTest:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        deadline = time.monotonic() + 5.0
        while True:
            try:
                self.socket.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.01)
        self.socket.settimeout(5.0)
        self.file = self.socket.makefile("rwb", buffering=0)
        self.swap = False

    def send(self, line):
        self.file.write(line.encode("ascii") + b"\n")
        reply = self.file.readline().decode("ascii").strip()
        if not reply.startswith("OK"):
            raise RuntimeError(f"qtest command {line!r} failed: {reply}")
        return reply[2:].strip()

    def read32(self, address):
        value = int(self.send(f"readl 0x{address:x}"), 0)
        if self.swap:
            value = int.from_bytes(value.to_bytes(4, "little"), "big")
        return value

    def write32(self, address, value):
        if self.swap:
            value = int.from_bytes(value.to_bytes(4, "big"), "little")
        self.send(f"writel 0x{address:x} 0x{value:x}")

    def read(self, address, size):
        value = self.send(f"read 0x{address:x} 0x{size:x}")
        return bytes.fromhex(value[2:] if value.startswith("0x") else value)

    def write(self, address, data):
        self.send(f"write 0x{address:x} 0x{len(data):x} 0x{data.hex()}")

    def wait32(self, address, expected):
        deadline = time.monotonic() + 5.0
        while self.read32(address) != expected:
            if time.monotonic() >= deadline:
                raise TimeoutError(
                    f"0x{address:x} did not become 0x{expected:x}")
            time.sleep(0.001)

    def detect_endian(self):
        value = self.read32(HACC_ID)
        if value == 0x43434148:
            self.swap = True
            value = self.read32(HACC_ID)
        assert value == 0x48414343, f"HACC ID mismatch: 0x{value:08x}"

    def close(self):
        self.file.close()
        self.socket.close()


def put32(command, offset, value):
    struct.pack_into(">I", command, offset, value & 0xFFFFFFFF)


def get32(command, offset):
    return struct.unpack_from(">I", command, offset)[0]


def get64(command, offset):
    return struct.unpack_from(">Q", command, offset)[0]


def make_command(generation, operation, path="", path2="", flags=0,
                 handle=0, value=0, offset=0, data_offset=0,
                 data_length=0, data_capacity=0):
    command = bytearray(COMMAND_SIZE)
    struct.pack_into(">IHHHH", command, 0, COMMAND_SIZE, 1, 1,
                     operation, flags)
    put32(command, 16, handle)
    put32(command, 20, generation)
    put32(command, 24, offset >> 32)
    put32(command, 28, offset)
    put32(command, 32, value >> 32)
    put32(command, 36, value)
    put32(command, 40, data_offset)
    put32(command, 44, data_length)
    put32(command, 48, data_capacity)
    for field, start in ((path, 96), (path2, 288)):
        encoded = field.encode("utf-8")
        assert len(encoded) < PATH_MAX
        command[start:start + len(encoded)] = encoded
    return command


def execute(qtest, commands, data=b"", bytes_override=None, owner=0x1001):
    payload = b"".join(commands) + data
    byte_size = len(payload) if bytes_override is None else bytes_override
    submission = bytearray(SUBMISSION_SIZE)

    struct.pack_into(">IHHIIIII", submission, 0, SUBMISSION_SIZE, 1, 0,
                     owner, get32(commands[0], 20), BUFFER, byte_size,
                     len(commands))
    qtest.write(BUFFER, payload)
    qtest.write(SUBMISSION, submission)
    qtest.write32(HACC_SUBMIT, SUBMISSION)
    result_word = qtest.read32(HACC_SUBMIT_RESULT)
    assert result_word & 0xFFFF == 0
    assert result_word >> 16 == len(commands)
    result = qtest.read(BUFFER, len(commands) * COMMAND_SIZE)
    return [result[index:index + COMMAND_SIZE]
            for index in range(0, len(result), COMMAND_SIZE)]


def status(command):
    return get32(command, 12)


def configure_channel(qtest, generation, operation, slot=3, owner=0x1001,
                      channel_generation=7, buffer=BUFFER, byte_size=4096,
                      capacity=1):
    config = bytearray(64)
    physical = buffer if operation == 1 else 0
    byte_size = byte_size if operation == 1 else 0
    capacity = capacity if operation == 1 else 0
    struct.pack_into(">IHHIIIIIII", config, 0, 64, 1, operation, slot,
                     owner, generation, channel_generation, physical,
                     byte_size, capacity)
    qtest.write(CHANNEL_CONFIG, config)
    qtest.write32(HACC_CHANNEL_CONFIG, CHANNEL_CONFIG)
    return qtest.read32(HACC_CHANNEL_RESULT)


def run(qtest, root, outside):
    qtest.detect_endian()
    assert qtest.read32(HACC_VERSION) == 0x00010004
    assert qtest.read32(HACC_CAPS) == 31
    assert qtest.read32(HACC_STATE) == 1
    assert qtest.read32(HACC_MAX_TRANSFER) == 2 * 1024 * 1024
    assert qtest.read32(HACC_MAX_COMMANDS) == 4096
    generation = qtest.read32(HACC_GENERATION)
    assert generation != 0

    assert configure_channel(qtest, generation, 1) == 0
    assert qtest.read32(CHANNEL_APERTURE + 3 * 4096) == 0x41484348
    assert qtest.read32(CHANNEL_APERTURE + 3 * 4096 + 4) == 1
    assert qtest.read32(CHANNEL_APERTURE + 3 * 4096 + 8) == 1
    header = qtest.read(BUFFER, CHANNEL_HEADER_SIZE)
    assert get32(header, 0) == 0x41484348
    assert struct.unpack_from(">HH", header, 4) == (1, CHANNEL_HEADER_SIZE)
    assert get32(header, 12) == COMMAND_SIZE
    assert get32(header, 16) == 1
    assert get32(header, 20) == CHANNEL_HEADER_SIZE
    assert get32(header, 24) == CHANNEL_HEADER_SIZE + COMMAND_SIZE
    direct = make_command(generation, FS_MKDIR, "/direct", value=0o755)
    qtest.write(BUFFER + CHANNEL_HEADER_SIZE, direct)
    qtest.write32(BUFFER + 36, 1)
    qtest.write32(CHANNEL_APERTURE + 3 * 4096 + 0x20, 1)
    qtest.wait32(BUFFER + 48, 1)
    assert qtest.read32(BUFFER + 48) == 1
    assert qtest.read32(BUFFER + 52) == 0
    assert status(qtest.read(BUFFER + CHANNEL_HEADER_SIZE, COMMAND_SIZE)) == 0
    assert qtest.read32(HACC_CHANNEL_PENDING) == 0
    qtest.write32(CHANNEL_APERTURE + 3 * 4096 + 0x24, 1)
    assert qtest.read32(HACC_CHANNEL_PENDING) == 1
    qtest.write32(CHANNEL_APERTURE + 3 * 4096 + 0x28, 1)
    assert qtest.read32(HACC_CHANNEL_PENDING) == 0
    second_buffer = BUFFER + 4096
    assert configure_channel(qtest, generation, 1, slot=4,
                             channel_generation=8,
                             buffer=second_buffer) == 0
    for buffer, slot, producer, path in (
            (BUFFER, 3, 2, "/parallel-a"),
            (second_buffer, 4, 1, "/parallel-b")):
        qtest.write(buffer + CHANNEL_HEADER_SIZE,
                    make_command(generation, FS_MKDIR, path, value=0o755))
        qtest.write32(buffer + 36, producer)
        qtest.write32(CHANNEL_APERTURE + slot * 4096 + 0x24, producer)
        qtest.write32(CHANNEL_APERTURE + slot * 4096 + 0x20, producer)
    qtest.wait32(BUFFER + 48, 2)
    qtest.wait32(second_buffer + 48, 1)
    assert status(qtest.read(BUFFER + CHANNEL_HEADER_SIZE, COMMAND_SIZE)) == 0
    assert status(qtest.read(second_buffer + CHANNEL_HEADER_SIZE,
                             COMMAND_SIZE)) == 0
    assert qtest.read32(HACC_CHANNEL_PENDING) == 1
    qtest.write32(HACC_CHANNEL_ACK, 1)
    assert qtest.read32(HACC_CHANNEL_PENDING) == 0
    assert configure_channel(qtest, generation, 2, slot=4,
                             channel_generation=8) == 0
    assert configure_channel(qtest, generation, 2) == 0
    assert qtest.read32(CHANNEL_APERTURE + 3 * 4096 + 8) == 0

    concurrent_buffer = BUFFER + 0x8000
    concurrent_capacity = 8
    concurrent_bytes = (CHANNEL_HEADER_SIZE +
                        concurrent_capacity * COMMAND_SIZE + 64)
    assert configure_channel(qtest, generation, 1, slot=5,
                             channel_generation=9,
                             buffer=concurrent_buffer,
                             byte_size=concurrent_bytes,
                             capacity=concurrent_capacity) == 0
    concurrent_commands = b"".join(
        make_command(generation, FS_MKDIR, f"/concurrent-{index}",
                     value=0o755)
        for index in range(concurrent_capacity))
    qtest.write(concurrent_buffer + CHANNEL_HEADER_SIZE,
                concurrent_commands)
    qtest.write32(concurrent_buffer + 36, concurrent_capacity)
    qtest.write32(CHANNEL_APERTURE + 5 * 4096 + 0x20,
                  concurrent_capacity)
    qtest.wait32(concurrent_buffer + 48, concurrent_capacity)
    assert qtest.read32(HACC_INFLIGHT) == 0
    assert qtest.read32(HACC_MAX_INFLIGHT) >= concurrent_capacity
    for index in range(concurrent_capacity):
        command = qtest.read(
            concurrent_buffer + CHANNEL_HEADER_SIZE + index * COMMAND_SIZE,
            COMMAND_SIZE)
        assert status(command) == STATUS_OK
    assert configure_channel(qtest, generation, 2, slot=5,
                             channel_generation=9) == 0

    overlap_buffer = BUFFER + 0xC000
    overlap_capacity = 2
    overlap_data = CHANNEL_HEADER_SIZE + overlap_capacity * COMMAND_SIZE
    assert configure_channel(qtest, generation, 1, slot=5,
                             channel_generation=10,
                             buffer=overlap_buffer,
                             byte_size=overlap_data + 64,
                             capacity=overlap_capacity) == 0
    overlap_commands = b"".join(
        make_command(generation, FS_READ, handle=1,
                     data_offset=overlap_data, data_capacity=64)
        for _ in range(overlap_capacity))
    qtest.write(overlap_buffer + CHANNEL_HEADER_SIZE, overlap_commands)
    qtest.write32(overlap_buffer + 36, overlap_capacity)
    qtest.write32(CHANNEL_APERTURE + 5 * 4096 + 0x20,
                  overlap_capacity)
    assert qtest.read32(CHANNEL_APERTURE + 5 * 4096 + 0x14) == \
        SYSCALL_INVALID_ARGUMENT
    assert qtest.read32(overlap_buffer + 48) == 0
    assert qtest.read32(HACC_INFLIGHT) == 0
    assert configure_channel(qtest, generation, 2, slot=5,
                             channel_generation=10) == 0

    drain_buffer = BUFFER + 0x10000
    drain_capacity = 256
    drain_bytes = (CHANNEL_HEADER_SIZE + drain_capacity * COMMAND_SIZE + 64)
    assert configure_channel(qtest, generation, 1, slot=5,
                             channel_generation=11, buffer=drain_buffer,
                             byte_size=drain_bytes,
                             capacity=drain_capacity) == 0
    drain_commands = b"".join(
        make_command(generation, FS_MKDIR, f"/drain-{index}", value=0o755)
        for index in range(drain_capacity))
    qtest.write(drain_buffer + CHANNEL_HEADER_SIZE, drain_commands)
    qtest.write32(drain_buffer + 36, drain_capacity)
    qtest.write32(CHANNEL_APERTURE + 5 * 4096 + 0x20, drain_capacity)
    assert configure_channel(qtest, generation, 2, slot=5,
                             channel_generation=11) == 0
    last_status = (drain_buffer + CHANNEL_HEADER_SIZE +
                   (drain_capacity - 1) * COMMAND_SIZE + 12)
    qtest.write32(last_status, 0xFEEDFACE)
    time.sleep(0.05)
    assert qtest.read32(last_status) == 0xFEEDFACE

    rejected = bytearray(SUBMISSION_SIZE)
    struct.pack_into(">IHHIIIII", rejected, 0, SUBMISSION_SIZE, 1, 0,
                     0x1001, generation + 1, BUFFER, COMMAND_SIZE, 1)
    qtest.write(SUBMISSION, rejected)
    qtest.write32(HACC_SUBMIT, SUBMISSION)
    assert qtest.read32(HACC_SUBMIT_RESULT) == 8
    struct.pack_into(">I", rejected, 28, 1)
    qtest.write(SUBMISSION, rejected)
    qtest.write32(HACC_SUBMIT, SUBMISSION)
    assert qtest.read32(HACC_SUBMIT_RESULT) == 2

    commands = [make_command(generation, FS_MKDIR, f"/batch-{index}",
                             value=0o755) for index in range(8)]
    results = execute(qtest, commands)
    assert all(status(result) == STATUS_OK for result in results)

    opened = execute(qtest, [make_command(
        generation, FS_OPEN, "/data", flags=OPEN_READ | OPEN_WRITE |
        OPEN_CREATE | OPEN_EXCLUSIVE, value=0o600)])[0]
    assert status(opened) == STATUS_OK
    handle = get32(opened, 16)
    assert handle != 0

    reopened = execute(qtest, [make_command(
        generation, FS_OPEN, "/data", flags=OPEN_READ, value=0o600)])[0]
    assert status(reopened) == STATUS_OK, status(reopened)
    reopened_handle = get32(reopened, 16)
    assert reopened_handle != 0
    reopened_close = execute(qtest, [make_command(
        generation, FS_CLOSE, handle=reopened_handle)])[0]
    assert status(reopened_close) == STATUS_OK

    write_only = execute(qtest, [make_command(
        generation, FS_OPEN, "/write-only", flags=OPEN_WRITE | OPEN_CREATE |
        OPEN_EXCLUSIVE, value=0o600)])[0]
    assert status(write_only) == STATUS_OK
    write_only_handle = get32(write_only, 16)
    denied_read = execute(qtest, [make_command(
        generation, FS_READ, handle=write_only_handle,
        data_offset=COMMAND_SIZE, data_capacity=1)], b"\xA5")[0]
    assert status(denied_read) == STATUS_ACCESS
    assert status(execute(qtest, [make_command(
        generation, FS_CLOSE, handle=write_only_handle)])[0]) == STATUS_OK

    read_only = execute(qtest, [make_command(
        generation, FS_OPEN, "/write-only", flags=OPEN_READ)])[0]
    assert status(read_only) == STATUS_OK
    read_only_handle = get32(read_only, 16)
    denied_write = execute(qtest, [make_command(
        generation, FS_WRITE, handle=read_only_handle,
        data_offset=COMMAND_SIZE, data_length=1)], b"x")[0]
    assert status(denied_write) == STATUS_ACCESS
    denied_truncate = execute(qtest, [make_command(
        generation, FS_TRUNCATE, handle=read_only_handle, value=0)])[0]
    assert status(denied_truncate) == STATUS_ACCESS
    assert status(execute(qtest, [make_command(
        generation, FS_CLOSE, handle=read_only_handle)])[0]) == STATUS_OK

    payload = b"host transport data integrity\x00\xff"
    written = execute(qtest, [make_command(
        generation, FS_WRITE, handle=handle, data_offset=COMMAND_SIZE,
        data_length=len(payload))], payload)[0]
    assert status(written) == STATUS_OK
    assert get32(written, 52) == len(payload)

    capacity = len(payload)
    read = execute(qtest, [make_command(
        generation, FS_READ, handle=handle, data_offset=COMMAND_SIZE,
        data_capacity=capacity)], b"\xA5" * capacity)[0]
    assert status(read) == STATUS_OK
    assert get32(read, 52) == capacity
    assert qtest.read(BUFFER + COMMAND_SIZE, capacity) == payload

    malformed = execute(qtest, [make_command(
        generation, FS_READ, handle=handle, data_offset=0,
        data_capacity=1)], b"\xA5")[0]
    assert status(malformed) == STATUS_INVALID

    made_link = execute(qtest, [make_command(
        generation, FS_SYMLINK, "/data", "/data-link")])[0]
    assert status(made_link) == STATUS_OK
    opened_link = execute(qtest, [make_command(
        generation, FS_OPEN, "/data-link", flags=OPEN_READ)])[0]
    assert status(opened_link) == STATUS_LOOP, status(opened_link)
    linked = execute(qtest, [make_command(
        generation, FS_STAT, "/data-link")])[0]
    assert status(linked) == STATUS_OK
    assert struct.unpack_from(">H", linked, 88)[0] == KIND_SYMLINK

    target = b"/data"
    exact = execute(qtest, [make_command(
        generation, FS_READLINK, "/data-link", data_offset=COMMAND_SIZE,
        data_capacity=len(target))], b"\xA5" * len(target))[0]
    assert status(exact) == STATUS_OK
    assert get32(exact, 52) == len(target)
    assert qtest.read(BUFFER + COMMAND_SIZE, len(target)) == target

    os.symlink(outside, os.path.join(root, "escape"))
    escaped = execute(qtest, [make_command(
        generation, FS_STAT, "/escape/sentinel")])[0]
    assert status(escaped) == STATUS_LOOP, status(escaped)
    traversed = execute(qtest, [make_command(
        generation, FS_STAT, "/../sentinel")])[0]
    assert status(traversed) == STATUS_INVALID
    for malformed_path in ("//data", "/./data", "/batch-0/../data"):
        malformed_path_result = execute(qtest, [make_command(
            generation, FS_STAT, malformed_path)])[0]
        assert status(malformed_path_result) == STATUS_INVALID, (
            malformed_path, status(malformed_path_result))
    renamed = execute(qtest, [make_command(
        generation, FS_RENAME, "/data", "/escape/stolen")])[0]
    assert status(renamed) == STATUS_LOOP, status(renamed)
    with open(os.path.join(outside, "sentinel"), "rb") as source:
        assert source.read() == b"outside"
    assert not os.path.exists(os.path.join(outside, "stolen"))

    opened_directory = execute(qtest, [make_command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY)])[0]
    assert status(opened_directory) == STATUS_OK
    directory = get32(opened_directory, 16)
    denied_directory_read = execute(qtest, [make_command(
        generation, FS_READ, handle=directory,
        data_offset=COMMAND_SIZE, data_capacity=1)], b"\xA5")[0]
    assert status(denied_directory_read) == STATUS_IS_DIR
    first = execute(qtest, [make_command(
        generation, FS_READDIR, "/", handle=directory,
        data_offset=COMMAND_SIZE, data_capacity=PATH_MAX)],
        b"\xA5" * PATH_MAX)[0]
    assert status(first) == STATUS_OK
    first_name = qtest.read(BUFFER + COMMAND_SIZE, get32(first, 52))
    cursor = get64(first, 32)
    assert first_name not in (b".", b"..") and cursor != 0
    second = execute(qtest, [make_command(
        generation, FS_READDIR, "/", handle=directory, offset=cursor,
        data_offset=COMMAND_SIZE, data_capacity=PATH_MAX)],
        b"\xA5" * PATH_MAX)[0]
    assert status(second) == STATUS_OK, status(second)
    second_name = qtest.read(BUFFER + COMMAND_SIZE, get32(second, 52))
    assert second_name not in (b".", b"..", first_name)
    closed_directory = execute(qtest, [make_command(
        generation, FS_CLOSE, handle=directory)])[0]
    assert status(closed_directory) == STATUS_OK

    crossed = execute(qtest, [make_command(
        generation, FS_CLOSE, handle=handle)], owner=0x1002)[0]
    assert status(crossed) == STATUS_BAD_HANDLE
    qtest.write32(HACC_RELEASE_OWNER, 0x1001)
    released = execute(qtest, [make_command(
        generation, FS_CLOSE, handle=handle)])[0]
    assert status(released) == STATUS_BAD_HANDLE

    qtest.write32(HACC_RESET, 1)
    next_generation = qtest.read32(HACC_GENERATION)
    assert next_generation != generation
    stale = execute(qtest, [make_command(
        next_generation, FS_CLOSE, handle=handle)])[0]
    assert status(stale) == STATUS_BAD_HANDLE


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="Astra QEMU system emulator")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-hostfs-") as temp:
        root = os.path.join(temp, "root")
        outside = os.path.join(temp, "outside")
        os.mkdir(root)
        os.mkdir(outside)
        with open(os.path.join(outside, "sentinel"), "wb") as output:
            output.write(b"outside")
        rom = os.path.join(temp, "hostfs-test.rom")
        qtest_path = os.path.join(temp, "qtest.sock")
        with open(rom, "wb") as output:
            output.write(struct.pack(">II", 0x02001000, 0xFFE00008))
        command = [
            args.qemu, "-machine", "astra68,accel=qtest", "-m", "32M",
            "-bios", rom, "-S", "-display", "none", "-nodefaults",
            "-qtest", f"unix:{qtest_path},server=on,wait=off",
        ]
        environment = os.environ.copy()
        environment["ASTRA_HOSTFS_ROOT"] = os.path.join(temp, "missing")
        refused = subprocess.run(command, stdout=subprocess.DEVNULL,
                                 stderr=subprocess.PIPE, text=True,
                                 env=environment, timeout=5)
        assert refused.returncode != 0
        assert "cannot open Astra host filesystem root" in refused.stderr
        environment["ASTRA_HOSTFS_ROOT"] = root
        with tempfile.TemporaryFile(mode="w+") as errors:
            process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                       stderr=errors, text=True,
                                       env=environment)
            qtest = None
            try:
                qtest = QTest(qtest_path)
                run(qtest, root, outside)
                print("ASTRA QEMU HOSTFS PASS")
            finally:
                if qtest is not None:
                    qtest.close()
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                if process.returncode not in (-15, 0):
                    errors.seek(0)
                    raise SystemExit(errors.read())


if __name__ == "__main__":
    main()
