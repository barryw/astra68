#!/usr/bin/env python3
"""Exercise the Astra generic host filesystem transport at its trust boundary."""

import argparse
import glob
import json
import os
import runpy
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
# Bound channels live apart from BUFFER, which every execute() rewrites.
SCOPED_BUFFER = 0x02050000
# A claim's channel, and the release-race channel.
CLAIM_BUFFER = 0x02060000
RACE_BUFFER = 0x02070000
SUBMISSION = 0x0200F000
CHANNEL_CONFIG = 0x0200F040
CHANNEL_APERTURE = 0xFFD00000
COMMAND_SIZE = 512
COMMAND_VERSION = 5
SUBMISSION_SIZE = 64
CHANNEL_HEADER_SIZE = 64
SESSION_FIELD = 60
PATH_OFFSET_FIELD = 64
NODE_FIELD = 72
NODE_KIND = NODE_FIELD + 4
NODE_MODE = NODE_FIELD + 6
NODE_DEVICE = NODE_FIELD + 24
NODE_INODE = NODE_FIELD + 32
NODE_SIZE = NODE_FIELD + 40
RESERVED_FIELD = 184
PATH_MAX = 4096
# Room the operands of a channel's commands take in its data span.
OPERAND_ROOM = 4096
NAME_CAPACITY = 256

FS_OPEN = 1
FS_CLOSE = 2
FS_READ = 3
FS_WRITE = 4
FS_SYNC = 5
FS_TRUNCATE = 6
FS_STAT = 7
FS_READDIR = 8
FS_MKDIR = 9
FS_UNLINK = 10
FS_RENAME = 11
FS_CHMOD = 12
FS_READLINK = 13
FS_SYMLINK = 14
FS_LINK = 15
FS_FILESYSTEM_INFO = 16
FS_SESSION_OPEN = 17
FS_SESSION_CLOSE = 18
FS_CLAIM = 19
FS_SEEK = 20
FS_SHARE = 21
FS_ADOPT = 22
FS_FILE_FLAGS = 23
FS_PATH = 24
IO_SEQUENTIAL = 1 << 14
ADOPT_DISCARD = 1
UNLINK_DIRECTORY = 1
SESSION_READ_ONLY = 1
CLAIM_RELEASE = 1
SERVICE_METRICS = 2
METRICS_SNAPSHOT = 1
SERVICE_ENTROPY = 4
ENTROPY_FILL = 1
ENTROPY_MAX = 256
METRICS_CAPACITY = 16 + 8 * (12 + 2 * 25)
METRIC_HOST_COMMANDS = 8

OPEN_READ = 1 << 0
OPEN_WRITE = 1 << 1
OPEN_CREATE = 1 << 2
OPEN_TRUNCATE = 1 << 3
OPEN_DIRECTORY = 1 << 4
OPEN_EXCLUSIVE = 1 << 5
OPEN_NOFOLLOW = 1 << 7
OPEN_APPEND = 1 << 6
OPEN_NONBLOCK = 1 << 8
STAT_FOLLOW = 1
STAT_MISSING = 2
STAT_BUDGET = 4

STATUS_OK = 0
STATUS_NOT_FOUND = 2
STATUS_EXISTS = 3
STATUS_INVALID = 8
STATUS_BAD_HANDLE = 9
STATUS_ACCESS = 6
STATUS_IS_DIR = 5
STATUS_NOT_DIR = 4
STATUS_LOOP = 18
STATUS_READ_ONLY = 19
STATUS_ESCAPED = 20
STATUS_LOST = 21
STATUS_BUSY = 14
STATUS_FILE_TOO_LARGE = 22
STATUS_LIMIT = 10
KIND_SYMLINK = 3
SYSCALL_INVALID_ARGUMENT = 2
CONFIG_VERSION = 5
CONFIG_OPEN = 1
CONFIG_CLOSE = 2
CONFIG_ATTACH = 3
CONFIG_DETACH = 4
SCOPE_BOUND = 1
SCOPE_READ_ONLY = 2
# Kernel-written on an unbound OPEN: the opener holds HOST_SCOPE.
CHANNEL_PROVIDER = 1 << 3
CAP_FS_SCOPE = 1 << 10
CAP_FS_SESSIONS = 1 << 11
CAP_FS_RESOLVES = 1 << 12
CAP_FS_DESCRIPTIONS = 1 << 13
CAP_FS_DIRECTORY_BATCH = 1 << 14
CAP_FS_PATHS = 1 << 15
CAP_VOLUME_CATALOG = 1 << 16
CURSOR_CURRENT = (1 << 64) - 1
NODE_INODE_BIT = 1 << 11
NODE_DEVICE_BIT = 1 << 12
# The claimed filesystem owner (HostFS) and a client process on a lane.
PROVIDER = 0x1001
CLIENT = 0x3003


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




class Command:
    """One record-4 command and its operands; place() lays them out."""

    def __init__(self, generation, operation, path=b"", path2=b"", flags=0,
                 handle=0, value=None, offset=0, data=b"", capacity=0,
                 service=1, session=0):
        self.raw = bytearray(COMMAND_SIZE)
        if value is None:
            value = (1 << 64) - 1 if operation == FS_WRITE else \
                1 if operation == FS_READDIR else 0
        struct.pack_into(">IHHHH", self.raw, 0, COMMAND_SIZE, COMMAND_VERSION,
                         service, operation, flags)
        put32(self.raw, 16, handle)
        put32(self.raw, 20, generation)
        put32(self.raw, 24, offset >> 32)
        put32(self.raw, 28, offset)
        put32(self.raw, 32, value >> 32)
        put32(self.raw, 36, value)
        put32(self.raw, SESSION_FIELD, session)
        self.path = path.encode("utf-8") if isinstance(path, str) else path
        self.path2 = path2.encode("utf-8") if isinstance(path2, str) else path2
        self.data = data
        self.capacity = capacity

    def place(self, at):
        """Fills the operand and data fields for bytes laid out at buffer
        offset `at`; returns (bytes, data offset)."""
        operands = self.path + self.path2
        struct.pack_into(">IHH", self.raw, PATH_OFFSET_FIELD,
                         at if operands else 0, len(self.path),
                         len(self.path2))
        span = max(len(self.data), self.capacity)
        data_at = at + len(operands)
        if span:
            put32(self.raw, 40, data_at)
            put32(self.raw, 44, len(self.data))
            put32(self.raw, 48, self.capacity)
        tail = self.data + b"\xA5" * (span - len(self.data))
        return bytes(operands) + tail, data_at


class Answer:
    def __init__(self, raw, data_at):
        self.raw = raw
        self.data_at = data_at


def lay_out(commands, start):
    arena = bytearray()
    offsets = []
    for command in commands:
        chunk, data_at = command.place(start + len(arena))
        arena += chunk
        arena += b"\0" * (-len(arena) % 8)
        offsets.append(data_at)
    return bytes(arena), offsets


def execute(qtest, commands, owner=PROVIDER):
    head = len(commands) * COMMAND_SIZE
    arena, offsets = lay_out(commands, head)
    payload = b"".join(bytes(command.raw) for command in commands) + arena
    submission = bytearray(SUBMISSION_SIZE)
    struct.pack_into(">IHHIIIII", submission, 0, SUBMISSION_SIZE, 1, 0,
                     owner, get32(commands[0].raw, 20), BUFFER, len(payload),
                     len(commands))
    qtest.write(BUFFER, payload)
    qtest.write(SUBMISSION, submission)
    qtest.write32(HACC_SUBMIT, SUBMISSION)
    result_word = qtest.read32(HACC_SUBMIT_RESULT)
    assert result_word & 0xFFFF == 0
    assert result_word >> 16 == len(commands)
    result = qtest.read(BUFFER, head)
    return [Answer(result[index * COMMAND_SIZE:(index + 1) * COMMAND_SIZE],
                   offsets[index]) for index in range(len(commands))]


def channel_write(qtest, buffer, capacity, commands, first=0):
    """Writes commands at producer positions first.. of a channel at
    `buffer` with `capacity` slots, operands in its data span."""
    data_start = CHANNEL_HEADER_SIZE + capacity * COMMAND_SIZE
    arena, offsets = lay_out(commands, data_start)
    for index, command in enumerate(commands):
        slot = (first + index) % capacity
        qtest.write(buffer + CHANNEL_HEADER_SIZE + slot * COMMAND_SIZE,
                    bytes(command.raw))
    if arena:
        qtest.write(buffer + data_start, arena)
    return offsets


def status(answer):
    return get32(answer.raw if isinstance(answer, Answer) else answer, 12)


def output(qtest, answer, length):
    return qtest.read(BUFFER + answer.data_at, length)


def directory_names(data):
    names = []
    while data:
        assert len(data) >= 33 and data[7] == 0
        length = data[6]
        assert length and len(data) >= 32 + length
        names.append(data[32:32 + length])
        data = data[32 + length:]
    return names


claim_generations = [100]


def claim(qtest, generation, owner=PROVIDER, release=False, provider=True, epoch=False):
    """CLAIM (or RELEASE) on an unbound channel of `owner`: one the kernel
    stamped PROVIDER, as it does for a device handle carrying HOST_SCOPE,
    or with provider=False an ordinary one (every other HOST_DEVICE
    holder's)."""
    claim_generations[0] += 1
    channel_generation = claim_generations[0]
    slot = 30
    assert configure_channel(
        qtest, generation, CONFIG_OPEN, slot=slot, owner=owner,
        channel_generation=channel_generation, buffer=CLAIM_BUFFER,
        scope_flags=CHANNEL_PROVIDER if provider else 0) == 0
    channel_write(qtest, CLAIM_BUFFER, 1, [Command(
        generation, FS_CLAIM, flags=CLAIM_RELEASE if release else 0)])
    qtest.write32(CLAIM_BUFFER + 36, 1)
    qtest.write32(CHANNEL_APERTURE + slot * 4096 + 0x20, 1)
    qtest.wait32(CLAIM_BUFFER + 48, 1)
    record = qtest.read(CLAIM_BUFFER + CHANNEL_HEADER_SIZE, COMMAND_SIZE)
    answer = (status(record), get32(record, 56)) if epoch else status(record)
    assert configure_channel(qtest, generation, CONFIG_CLOSE, slot=slot,
                             owner=owner,
                             channel_generation=channel_generation) == 0
    return answer


def executed_claim(qtest, generation, owner=PROVIDER):
    """CLAIM through HOST_EXECUTE, which the kernel never stamps."""
    return status(execute(qtest, [Command(generation, FS_CLAIM)],
                          owner=owner)[0])


def volume_page(qtest, generation, epoch=0, sequence=0, after=0):
    query = struct.pack(">IIQQQ", 1, 32, epoch, sequence, after)
    return execute(qtest, [Command(generation, 1, service=6,
        data=query, capacity=8104)], owner=0x2002)[0]


def volume_reference(qtest, generation, name):
    """Resolve the current identity from public metadata, never a slot number."""
    epoch = sequence = after = 0
    found = None
    while True:
        answer = volume_page(qtest, generation, epoch, sequence, after)
        assert status(answer) == STATUS_OK, status(answer)
        page = output(qtest, answer, get32(answer.raw, 52))
        version, size, epoch, sequence, after, count, reserved = struct.unpack_from(">IIQQQII", page)
        assert version == 1 and size == 8104 and len(page) == size
        assert epoch and sequence and reserved == 0 and count <= 16
        for index in range(count):
            row = page[40 + index * 504:40 + (index + 1) * 504]
            kind, _, state = struct.unpack_from(">III", row, 56)
            if kind in (4, 5) and state == 5 and \
                    row[88:120].split(b"\0", 1)[0].decode() == name:
                assert found is None, ("duplicate mounted name", name)
                found = struct.pack(">QQQ", epoch, *struct.unpack_from(">QQ", row))
        if after == 0:
            assert found is not None, ("missing mounted volume", name)
            return found


def open_session(qtest, generation, volume="/work", root="/", read_only=False,
                 parent=0, owner=PROVIDER):
    answer = execute(qtest, [Command(
        generation, FS_SESSION_OPEN, path=root,
        data=(volume_reference(qtest, generation, volume) if parent == 0 else bytes(24)) + struct.pack(">II", owner, 0),
        flags=SESSION_READ_ONLY if read_only else 0, session=parent)],
        owner=owner)[0]
    assert status(answer) == STATUS_OK, status(answer)
    return get32(answer.raw, 56)


def provider_holds(path):
    """Observe the held inode, including its private bind-mount spelling."""
    target = os.stat(path)
    for fd in glob.glob("/proc/[0-9]*/fd/*"):
        try:
            held = os.stat(fd)
            if (held.st_dev, held.st_ino) == (target.st_dev, target.st_ino):
                return True
        except OSError:
            pass
    return False


def configure_channel(qtest, generation, operation, slot=3, owner=PROVIDER,
                      channel_generation=7, buffer=BUFFER, byte_size=4096,
                      capacity=1, scope_flags=0, process=None):
    config = bytearray(68)
    physical = buffer if operation == 1 else 0
    byte_size = byte_size if operation == 1 else 0
    capacity = capacity if operation == 1 else 0
    struct.pack_into(">IHHIIIIIIIII", config, 0, 68, CONFIG_VERSION,
                     operation, slot,
                     owner, generation, channel_generation, physical,
                     byte_size, capacity, 0,
                     scope_flags if operation == 1 else 0)
    struct.pack_into(">I", config, 52, (owner if process is None else process) if operation == CONFIG_OPEN else 0)
    qtest.write(CHANNEL_CONFIG, config)
    qtest.write32(HACC_CHANNEL_CONFIG, CHANNEL_CONFIG)
    return qtest.read32(HACC_CHANNEL_RESULT)


def configure_scope(qtest, generation, operation, slot, channel_generation,
                    scope=0, scope_flags=0, scope_owner=0, owner=CLIENT,
                    buffer=SCOPED_BUFFER, byte_size=8192 + OPERAND_ROOM,
                    capacity=2, scope_files=0, process=None):
    """The kernel's channel configuration with scope words. The
    channel belongs to a client; the scope's creator is the provider."""
    config = bytearray(68)
    opening = operation == CONFIG_OPEN
    struct.pack_into(">IHHIIIIIIIIII", config, 0, 68, CONFIG_VERSION,
                     operation, slot, owner, generation, channel_generation,
                     buffer if opening else 0, byte_size if opening else 0,
                     capacity if opening else 0, scope, scope_flags,
                     scope_owner)
    struct.pack_into(">I", config, 48, scope_files)
    struct.pack_into(">I", config, 52, (owner if process is None else process) if opening else 0)
    qtest.write(CHANNEL_CONFIG, config)
    qtest.write32(HACC_CHANNEL_CONFIG, CHANNEL_CONFIG)
    return qtest.read32(HACC_CHANNEL_RESULT)


def scoped_post(qtest, slot, producer, command, buffer=SCOPED_BUFFER,
                capacity=2):
    """Puts `command` (and its operands) at producer position `producer`
    and kicks."""
    at = buffer + CHANNEL_HEADER_SIZE + \
        ((producer - 1) % capacity) * COMMAND_SIZE
    channel_write(qtest, buffer, capacity, [command], first=producer - 1)
    qtest.write32(buffer + 36, producer)
    qtest.write32(CHANNEL_APERTURE + slot * 4096 + 0x20, producer)
    return at


def scoped_wait(qtest, producer, at, buffer=SCOPED_BUFFER):
    """Waits until the consumer has passed `producer` (a later command may
    already have completed too) and answers the command's slot."""
    deadline = time.monotonic() + 5.0
    while qtest.read32(buffer + 48) < producer:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"consumer did not reach {producer}")
        time.sleep(0.001)
    return qtest.read(at, COMMAND_SIZE)


def scoped(qtest, slot, producer, command):
    return scoped_wait(qtest, producer, scoped_post(qtest, slot, producer,
                                                    command))


def scopes(qtest, root):
    """A bound channel acts only through the scopes the kernel attached: the
    command's session picks one and an unattached session is refused, other
    services are refused, a read-only scope mutates nothing, a handle answers
    only in the session it was opened in, malformed scope words and a scope
    whose creator is not the claimed owner are refused at configuration,
    and a session's handles die with it."""
    generation = qtest.read32(HACC_GENERATION)
    produced = [0]

    def step(command):
        """The next command on bound slot 7, answered."""
        produced[0] += 1
        return scoped(qtest, 7, produced[0], command)

    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    sound = open_session(qtest, generation, volume="/sound")
    # Unbound OPEN carries no scope words; a bound one names a live session
    # of the claimed owner, and the claimed owner as its creator.
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           scope=sound) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           0xDEAD, SCOPE_BOUND, PROVIDER) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           work, SCOPE_BOUND, 0) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           work, SCOPE_BOUND | 4, PROVIDER) != 0
    # The creator check: a scope whose creator is not the claimed owner is
    # refused at the binding, even over a live session.
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21, work,
                           SCOPE_BOUND, 0x2002) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           work, SCOPE_BOUND, PROVIDER, process=0) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           work, SCOPE_BOUND, PROVIDER, process=65536) != 0
    assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
                           work, SCOPE_BOUND, PROVIDER) == 0
    made = step(Command(
        generation, FS_MKDIR, "/scoped-dir", value=0o755, session=work))
    assert status(made) == STATUS_OK
    assert os.path.isdir(os.path.join(root, "work", "scoped-dir"))
    # A forged session: SOUND's is not attached to this channel.
    forged = step(Command(
        generation, FS_MKDIR, "/forged-dir", value=0o755, session=sound))
    assert status(forged) == STATUS_ACCESS
    assert not os.path.exists(os.path.join(root, "sound", "forged-dir"))
    entropy = step(Command(
        generation, ENTROPY_FILL, service=SERVICE_ENTROPY, capacity=32))
    assert status(entropy) == STATUS_ACCESS
    # A lane cannot claim the service or open and close sessions.
    for operation, session in ((FS_CLAIM, 0), (FS_SESSION_OPEN, work),
                               (FS_SESSION_CLOSE, work)):
        refused = step(Command(
            generation, operation, "/" if operation == FS_SESSION_OPEN
            else "", session=session))
        assert status(refused) == STATUS_ACCESS, (operation, status(refused))
    # Attach a read-only SOUND scope: the same channel now reaches it, and
    # only to read.
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21,
                           sound, SCOPE_BOUND | SCOPE_READ_ONLY,
                           PROVIDER) == 0
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21,
                           sound, SCOPE_BOUND, PROVIDER) != 0
    refused = step(Command(
        generation, FS_MKDIR, "/ro-dir", value=0o755, session=sound))
    assert status(refused) == STATUS_READ_ONLY
    assert not os.path.exists(os.path.join(root, "sound", "ro-dir"))
    allowed = step(Command(
        generation, FS_STAT, "/", session=sound))
    assert status(allowed) == STATUS_OK
    # A handle belongs to the session it was opened in.
    with open(os.path.join(root, "work", "scoped-file"), "wb") as out:
        out.write(b"scoped")
    opened = step(Command(
        generation, FS_OPEN, "/scoped-file", flags=OPEN_READ, session=work))
    assert status(opened) == STATUS_OK
    crossed = step(Command(
        generation, FS_CLOSE, handle=get32(opened, 16), session=sound))
    assert status(crossed) == STATUS_BAD_HANDLE
    closed = step(Command(
        generation, FS_CLOSE, handle=get32(opened, 16), session=work))
    assert status(closed) == STATUS_OK
    assert configure_scope(qtest, generation, CONFIG_DETACH, 7, 21,
                           sound) == 0
    detached = step(Command(
        generation, FS_STAT, "/", session=sound))
    assert status(detached) == STATUS_LOST

    # A session's handles die with it, whoever opened them.
    with open(os.path.join(root, "work", "held-open"), "wb") as out:
        out.write(b"held")
    sub = open_session(qtest, generation, root="/", parent=work)
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21, sub,
                           SCOPE_BOUND, PROVIDER) == 0
    held = step(Command(generation, FS_OPEN, "/held-open",
                                        flags=OPEN_READ, session=sub))
    assert status(held) == STATUS_OK
    target = os.path.join(root, "work", "held-open")
    assert provider_holds(target)
    assert status(execute(qtest, [Command(
        generation, FS_SESSION_CLOSE, session=sub)])[0]) == STATUS_OK
    assert not provider_holds(target)
    assert status(step(Command(
        generation, FS_CLOSE, handle=get32(held, 16), session=sub))) == \
        STATUS_OK

    # The claimed owner dies (HostFS exits): every session it opened and
    # every handle in them go, and a handle from before never answers
    # again -- not once the owner claims again, not under a new session
    # attached in its place, not under the old word (review M2).
    with open(os.path.join(root, "work", "survivor"), "wb") as out:
        out.write(b"survivor")
    survivor = step(Command(
        generation, FS_OPEN, "/survivor", flags=OPEN_READ, session=work))
    assert status(survivor) == STATUS_OK
    old_handle = get32(survivor, 16)
    qtest.write32(HACC_RELEASE_OWNER, PROVIDER)
    assert not provider_holds(os.path.join(root, "work", "survivor"))
    orphan = step(Command(
        generation, FS_STAT, "/", session=work))
    assert status(orphan) == STATUS_LOST
    # The window while HostFS restarts: the claim is free, and another
    # HOST_DEVICE holder still cannot take it (review I1).
    assert claim(qtest, generation, owner=0x2002, provider=False) == \
        STATUS_ACCESS
    assert executed_claim(qtest, generation, owner=0x2002) == STATUS_ACCESS
    assert claim(qtest, generation) == STATUS_OK
    renewed = open_session(qtest, generation)
    assert renewed not in (work, sound, sub)
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21,
                           renewed, SCOPE_BOUND, PROVIDER) == 0
    for session in (renewed, work):
        stale = step(Command(
            generation, FS_READ, handle=old_handle, capacity=4,
            session=session))
        assert status(stale) == STATUS_LOST, (session, status(stale))
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 7, 21) == 0
    # A device channel takes no scopes.
    assert configure_channel(qtest, generation, CONFIG_OPEN, slot=8,
                             channel_generation=22) == 0
    assert configure_scope(qtest, generation, CONFIG_ATTACH, 8, 22,
                           renewed, SCOPE_BOUND, PROVIDER,
                           owner=PROVIDER) != 0
    assert configure_channel(qtest, generation, CONFIG_CLOSE, slot=8,
                             channel_generation=22) == 0
    print("PASS scopes")


def scope_revocation(qtest, root):
    """What has started when the scope is detached finishes; what has not
    is refused. The machine runs one thread-pool worker
    (-object main-loop,thread-pool-max=1), so the second command waits
    behind the first, which a FIFO holds open."""
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    open_session(qtest, generation, volume="/sound")
    fifo = os.path.join(root, "work", "held")
    os.mkfifo(fifo)
    assert configure_scope(qtest, generation, CONFIG_OPEN, 9, 31,
                           work, SCOPE_BOUND, PROVIDER) == 0
    first = scoped_post(qtest, 9, 1, Command(
        generation, FS_OPEN, "/held", flags=OPEN_READ, session=work))
    time.sleep(0.5)
    assert qtest.read32(SCOPED_BUFFER + 48) == 0, \
        "the FIFO open did not hold the only worker"
    second = scoped_post(qtest, 9, 2, Command(
        generation, FS_MKDIR, "/after-revoke", value=0o755, session=work))
    assert configure_scope(qtest, generation, CONFIG_DETACH, 9, 31,
                           work) == 0
    writer = os.open(fifo, os.O_WRONLY)
    try:
        assert status(scoped_wait(qtest, 1, first)) == STATUS_OK
        assert status(scoped_wait(qtest, 2, second)) == STATUS_LOST
    finally:
        os.close(writer)
    assert not os.path.exists(os.path.join(root, "work", "after-revoke"))
    later = scoped(qtest, 9, 3, Command(generation, FS_STAT, "/",
                                        session=work))
    assert status(later) == STATUS_LOST
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 9, 31) == 0
    print("PASS scope-revocation")


def orphan_open(qtest, root):
    """An OPEN in flight when its session closes leaves no handle behind
    (review M1): the FIFO holds the open in Linux while the session closes,
    and the handle it would have made is closed instead of inserted."""
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    sub = open_session(qtest, generation, root="/", parent=work)
    fifo = os.path.join(root, "work", "orphan")
    os.mkfifo(fifo)
    assert configure_scope(qtest, generation, CONFIG_OPEN, 10, 41,
                           sub, SCOPE_BOUND, PROVIDER) == 0
    pending = scoped_post(qtest, 10, 1, Command(
        generation, FS_OPEN, "/orphan", flags=OPEN_READ, session=sub))
    time.sleep(0.5)
    assert qtest.read32(SCOPED_BUFFER + 48) == 0, \
        "the FIFO open did not hold the worker"
    assert status(execute(qtest, [Command(
        generation, FS_SESSION_CLOSE, session=sub)])[0]) == STATUS_OK
    writer = os.open(fifo, os.O_WRONLY)
    try:
        answer = scoped_wait(qtest, 1, pending)
    finally:
        os.close(writer)
    assert status(answer) == STATUS_BAD_HANDLE, status(answer)
    assert get32(answer, 16) == 0
    assert not provider_holds(fifo)
    assert configure_scope(qtest, generation, CONFIG_CLOSE, 10, 41) == 0
    print("PASS orphan-open")


def claim_race(qtest):
    """A RELEASE racing SESSION_OPENs of the same claimant leaves no session
    behind (review M2): sixty-three opens and one release go to the worker
    pool at once, and afterwards every session any of them opened is gone."""
    generation = qtest.read32(HACC_GENERATION)
    capacity = 64
    slot = 31
    assert claim(qtest, generation) == STATUS_OK
    assert configure_channel(
        qtest, generation, CONFIG_OPEN, slot=slot, channel_generation=51,
        buffer=RACE_BUFFER,
        byte_size=CHANNEL_HEADER_SIZE + capacity * COMMAND_SIZE +
        OPERAND_ROOM,
        capacity=capacity, scope_flags=CHANNEL_PROVIDER) == 0
    opened = refused = 0
    for round_index in range(40):
        assert claim(qtest, generation) == STATUS_OK
        # Establish a real retained session before releasing under load. Slow
        # rooted I/O may let RELEASE win every racing open; that is a valid
        # outcome, not proof that publication or retirement was exercised.
        baseline = open_session(qtest, generation)
        target = volume_reference(qtest, generation, "/work") + struct.pack(">II", PROVIDER, 0)
        commands = [Command(generation, FS_SESSION_OPEN, "/", data=target)
                    for _ in range(capacity)]
        commands[capacity // 2] = Command(generation, FS_CLAIM,
                                          flags=CLAIM_RELEASE)
        channel_write(qtest, RACE_BUFFER, capacity, commands,
                      first=round_index * capacity)
        producer = (round_index + 1) * capacity
        qtest.write32(RACE_BUFFER + 36, producer)
        qtest.write32(CHANNEL_APERTURE + slot * 4096 + 0x20, producer)
        qtest.wait32(RACE_BUFFER + 48, producer)
        sessions = [baseline]
        for index in range(capacity):
            answer = qtest.read(RACE_BUFFER + CHANNEL_HEADER_SIZE +
                                index * COMMAND_SIZE, COMMAND_SIZE)
            if index == capacity // 2:
                assert status(answer) == STATUS_OK
            elif status(answer) == STATUS_OK:
                sessions.append(get32(answer, 56))
            else:
                assert status(answer) == STATUS_ACCESS, status(answer)
                refused += 1
        opened += len(sessions)
        assert claim(qtest, generation) == STATUS_OK
        for session in sessions:
            survivor = execute(qtest, [Command(generation, FS_STAT, "/",
                                               session=session)])[0]
            assert status(survivor) == STATUS_BAD_HANDLE, \
                ("session %d outlived the release in round %d" %
                 (session, round_index))
    assert configure_channel(qtest, generation, CONFIG_CLOSE, slot=slot,
                             channel_generation=51) == 0
    # Both real issued identities and racing refusals must be observed. Every
    # issued identity, including the controlled baseline, must be retired.
    assert opened > 0 and refused > 0, (opened, refused)
    print("PASS claim-race opened=%d refused=%d" % (opened, refused))


def booted(command, environment, qtest_path, body):
    if os.path.exists(qtest_path):
        os.unlink(qtest_path)
    with tempfile.TemporaryFile(mode="w+") as errors:
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=errors, text=True,
                                   env=environment)
        qtest = None
        try:
            qtest = QTest(qtest_path)
            body(qtest)
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


def volumes(qtest, root):
    """Catalog identities select roots; paths cannot cross them or use stale identities."""
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    sound = open_session(qtest, generation, volume="/sound")
    created = execute(qtest, [Command(
        generation, FS_MKDIR, "/fonts", value=0o755, session=sound)])[0]
    assert status(created) == STATUS_OK
    assert os.path.isdir(os.path.join(root, "sound", "fonts"))
    assert not os.path.exists(os.path.join(root, "work", "fonts"))
    missing = execute(qtest, [Command(
        generation, FS_STAT, "/fonts", session=work)])[0]
    assert status(missing) != STATUS_OK
    escaped = execute(qtest, [Command(
        generation, FS_STAT, "/../work", session=sound)])[0]
    assert status(escaped) != STATUS_OK
    target = volume_reference(qtest, generation, "/work")
    epoch, object_id, object_generation = struct.unpack(">QQQ", target)
    for reference in ((epoch, (1 << 64) - 1, object_generation),
                      (epoch, object_id, object_generation + 1),
                      (epoch ^ 1, object_id, object_generation)):
        unknown = execute(qtest, [Command(generation, FS_SESSION_OPEN, "/",
            data=struct.pack(">QQQII", *reference, PROVIDER, 0))])[0]
        assert status(unknown) == STATUS_LOST, status(unknown)
    # The obsolete numeric-volume spelling carries no identity and is refused.
    obsolete = execute(qtest, [Command(generation, FS_SESSION_OPEN, "/", value=1)])[0]
    assert status(obsolete) == STATUS_INVALID
    zero = execute(qtest, [Command(generation, FS_SESSION_OPEN, "/",
        data=struct.pack(">QQQII", epoch, 0, object_generation, PROVIDER, 0))])[0]
    assert status(zero) == STATUS_INVALID
    stale = Command(generation, FS_STAT, "/fonts", session=sound)
    struct.pack_into(">H", stale.raw, 4, COMMAND_VERSION - 1)
    assert status(execute(qtest, [stale])[0]) == STATUS_INVALID


def claims_and_sessions(qtest, root):
    """Record 4: one claimed owner, rooted sessions, read-only sessions,
    handles bound to their session, path operands."""
    generation = qtest.read32(HACC_GENERATION)
    stray = execute(qtest, [Command(generation, FS_STAT, "/", session=1)],
                    owner=0x2002)[0]
    assert status(stray) == STATUS_ACCESS
    # Nobody holds the claim (boot, a reset, HostFS restarting), and still
    # only a channel the kernel stamped PROVIDER takes one (review I1):
    # not HOST_EXECUTE, not an ordinary channel, whoever the owner.
    assert executed_claim(qtest, generation, owner=0x2002) == STATUS_ACCESS
    assert executed_claim(qtest, generation) == STATUS_ACCESS
    assert claim(qtest, generation, owner=0x2002, provider=False) == \
        STATUS_ACCESS
    assert claim(qtest, generation, provider=False) == STATUS_ACCESS
    assert claim(qtest, generation) == STATUS_OK
    assert claim(qtest, generation) == STATUS_OK
    assert claim(qtest, generation, owner=0x2002) == STATUS_ACCESS
    work = open_session(qtest, generation)
    # Claimed, a HOST_DEVICE holder is refused still (review M1).
    stray = execute(qtest, [Command(generation, FS_STAT, "/",
                                    session=work)], owner=0x2002)[0]
    assert status(stray) == STATUS_ACCESS
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=0)])[0]) == STATUS_BAD_HANDLE
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=0xDEAD)])[0]) == STATUS_BAD_HANDLE

    # A read-only session refuses every mutation in the provider itself.
    readonly = open_session(qtest, generation, read_only=True)
    for command in (
            Command(generation, FS_MKDIR, "/ro-made", value=0o755,
                    session=readonly),
            Command(generation, FS_OPEN, "/ro-file",
                    flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, value=0o600,
                    session=readonly)):
        assert status(execute(qtest, [command])[0]) == STATUS_READ_ONLY
    assert not os.path.exists(os.path.join(root, "ro-made"))

    # A session derived beneath "/sub" sees only "/sub", and cannot shed
    # its parent's read-only flag.
    assert status(execute(qtest, [Command(
        generation, FS_MKDIR, "/sub", value=0o755, session=work)])[0]) == \
        STATUS_OK
    with open(os.path.join(root, "sub", "inside"), "wb") as handle:
        handle.write(b"in")
    sub = open_session(qtest, generation, root="/sub", parent=work)
    inside = execute(qtest, [Command(generation, FS_STAT, "/inside",
                                     session=sub)])[0]
    assert status(inside) == STATUS_OK
    assert get64(inside.raw, NODE_INODE) == \
        os.stat(os.path.join(root, "sub", "inside")).st_ino
    assert get64(inside.raw, NODE_DEVICE) == \
        struct.unpack(">QQQ", volume_reference(qtest, generation, "/work"))[1]
    assert get32(inside.raw, NODE_FIELD) & (NODE_INODE_BIT | NODE_DEVICE_BIT) \
        == NODE_INODE_BIT | NODE_DEVICE_BIT
    assert get64(inside.raw, NODE_SIZE) == 2
    sub_readonly = open_session(qtest, generation, root="/sub",
                                parent=readonly)
    assert status(execute(qtest, [Command(
        generation, FS_MKDIR, "/x", value=0o755,
        session=sub_readonly)])[0]) == STATUS_READ_ONLY
    # The derived root is a directory: the session root opens as one.
    listed = execute(qtest, [Command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY,
        session=sub)])[0]
    assert status(listed) == STATUS_OK, status(listed)
    entry = execute(qtest, [Command(
        generation, FS_READDIR, handle=get32(listed.raw, 16),
        capacity=NAME_CAPACITY, session=sub)])[0]
    assert status(entry) == STATUS_OK
    assert directory_names(output(qtest, entry, get32(entry.raw, 52))) == [b"inside"]
    assert status(execute(qtest, [Command(
        generation, FS_CLOSE, handle=get32(listed.raw, 16),
        session=sub)])[0]) == STATUS_OK

    # A handle belongs to its session, even for the same owner.
    opened = execute(qtest, [Command(generation, FS_OPEN, "/inside",
                                     flags=OPEN_READ, session=sub)])[0]
    assert status(opened) == STATUS_OK
    crossed = execute(qtest, [Command(
        generation, FS_READ, handle=get32(opened.raw, 16), capacity=2,
        session=work)])[0]
    assert status(crossed) == STATUS_BAD_HANDLE
    # ... and so does a directory handle, as either base of a RENAME or a
    # LINK: another session's directory is never a destination or a source
    # (review M3).
    sub_directory = execute(qtest, [Command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY,
        session=sub)])[0]
    assert status(sub_directory) == STATUS_OK
    other = get32(sub_directory.raw, 16)
    with open(os.path.join(root, "loose"), "wb") as handle:
        handle.write(b"loose")
    for operation in (FS_RENAME, FS_LINK):
        into = execute(qtest, [Command(
            generation, operation, "/loose", "moved", value=other,
            session=work)])[0]
        assert status(into) == STATUS_BAD_HANDLE, (operation, status(into))
        out_of = execute(qtest, [Command(
            generation, operation, "inside", "/taken", handle=other,
            session=work)])[0]
        assert status(out_of) == STATUS_BAD_HANDLE, \
            (operation, status(out_of))
    assert not os.path.exists(os.path.join(root, "sub", "moved"))
    assert not os.path.exists(os.path.join(root, "taken"))
    assert os.path.exists(os.path.join(root, "loose"))

    # A derived session cannot be left: not by "..", not through a host
    # symlink (relative or absolute), not by an absolute path given with a
    # directory handle (review M3).
    os.symlink("..", os.path.join(root, "sub", "up"))
    os.symlink("/etc", os.path.join(root, "sub", "abs"))
    for path, expected in (("/up/loose", STATUS_ESCAPED),
                           ("/up/sub/inside", STATUS_ESCAPED),
                           ("/abs/passwd", STATUS_ESCAPED),
                           ("/../loose", STATUS_ESCAPED),
                           ("/inside/../../loose", STATUS_NOT_DIR)):
        escaped = execute(qtest, [Command(
            generation, FS_OPEN, path, flags=OPEN_READ, capacity=NAME_CAPACITY,
            session=sub)])[0]
        assert status(escaped) == expected, (path, status(escaped))
    for path in ("../loose", "/loose", "up/loose"):
        escaped = execute(qtest, [Command(
            generation, FS_OPEN, path, handle=other, flags=OPEN_READ,
            capacity=NAME_CAPACITY, session=sub)])[0]
        assert status(escaped) == (STATUS_INVALID if path[0] == "/" else STATUS_ESCAPED), \
            (path, status(escaped))
    listed_up = execute(qtest, [Command(
        generation, FS_READDIR, "/up", capacity=NAME_CAPACITY,
        session=sub)])[0]
    # The provider reports the namespace continuation without listing outside.
    assert status(listed_up) == STATUS_ESCAPED, \
        status(listed_up)
    stolen = execute(qtest, [Command(
        generation, FS_RENAME, "/inside", "/up/stolen", capacity=NAME_CAPACITY,
        session=sub)])[0]
    assert status(stolen) == STATUS_ESCAPED, status(stolen)
    assert not os.path.exists(os.path.join(root, "stolen"))
    for operation in (FS_RENAME, FS_LINK):
        for left, right in (("/abs/passwd", "/untouched"),
                            ("/inside", "/abs/untouched"),
                            ("abs/passwd", "untouched")):
            escaped = execute(qtest, [Command(
                generation, operation, left, right,
                handle=other if left[0] != "/" else 0,
                value=other if right[0] != "/" else 0,
                capacity=NAME_CAPACITY, session=sub)])[0]
            assert status(escaped) == STATUS_ESCAPED, (operation, left, right, status(escaped))
            assert output(qtest, escaped, get32(escaped.raw, 52)).startswith(b"/etc/")
    assert os.path.exists(os.path.join(root, "sub", "inside"))
    assert not os.path.exists(os.path.join(root, "sub", "untouched"))
    assert status(execute(qtest, [Command(
        generation, FS_CLOSE, handle=other, session=sub)])[0]) == STATUS_OK

    # Operands: a 4095-byte path to a real file works, one byte more is
    # refused before Linux sees it, and so is an embedded NUL. Eighteen
    # 200-byte directories and one of 220 make 3839 bytes; a 255-byte leaf
    # (Linux NAME_MAX) brings the path to exactly 4095.
    path = ""
    for length in [200] * 18 + [220]:
        path += "/" + "d" * length
        assert status(execute(qtest, [Command(
            generation, FS_MKDIR, path, value=0o755, session=work)])[0]) == \
            STATUS_OK
    leaf = path + "/" + "f" * (PATH_MAX - 2 - len(path))
    assert len(leaf) == PATH_MAX - 1
    deep = execute(qtest, [Command(
        generation, FS_OPEN, leaf,
        flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, value=0o600,
        session=work)])[0]
    assert status(deep) == STATUS_OK, status(deep)
    assert status(execute(qtest, [Command(
        generation, FS_STAT, leaf + "x", session=work)])[0]) == \
        STATUS_INVALID
    assert status(execute(qtest, [Command(
        generation, FS_STAT, b"/in\0side", session=sub)])[0]) == \
        STATUS_INVALID

    # Every service rejects a nonzero byte past its fields.
    entropy = Command(generation, 1, capacity=32, service=SERVICE_ENTROPY)
    entropy.raw[RESERVED_FIELD] = 1
    assert status(execute(qtest, [entropy])[0]) == STATUS_INVALID
    stat = Command(generation, FS_STAT, "/", session=work)
    stat.raw[RESERVED_FIELD + 40] = 1
    assert status(execute(qtest, [stat])[0]) == STATUS_INVALID
    assert status(execute(qtest, [Command(
        generation, FS_SESSION_CLOSE, session=sub)])[0]) == STATUS_OK
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/inside", session=sub)])[0]) == STATUS_BAD_HANDLE
    # The claim is the owner's to give up, with its sessions.
    assert claim(qtest, generation, owner=0x2002, release=True) == \
        STATUS_ACCESS
    assert claim(qtest, generation, release=True) == STATUS_OK
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=work)])[0]) == STATUS_ACCESS
    # Released, the claim is free -- and still only an owner the kernel
    # authenticated takes it (review I1).
    assert claim(qtest, generation, owner=0x2002, provider=False) == \
        STATUS_ACCESS
    assert executed_claim(qtest, generation, owner=0x2002) == STATUS_ACCESS
    assert claim(qtest, generation, owner=0x2002) == STATUS_OK
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/", session=work)], owner=0x2002)[0]) == \
        STATUS_BAD_HANDLE
    assert claim(qtest, generation, owner=0x2002, release=True) == STATUS_OK
    print("PASS claims-and-sessions")


def run(qtest, root, outside):
    qtest.detect_endian()
    assert qtest.read32(HACC_VERSION) == 0x00030000
    assert qtest.read32(HACC_CAPS) == (511 | CAP_FS_SCOPE | CAP_FS_SESSIONS |
                                    CAP_FS_RESOLVES | CAP_FS_DESCRIPTIONS |
                                    CAP_FS_DIRECTORY_BATCH | CAP_FS_PATHS | CAP_VOLUME_CATALOG)
    assert qtest.read32(HACC_STATE) == 1
    assert qtest.read32(HACC_MAX_TRANSFER) == 2 * 1024 * 1024
    assert qtest.read32(HACC_MAX_COMMANDS) == 4096
    generation = qtest.read32(HACC_GENERATION)
    assert generation != 0

    metrics = execute(qtest, [Command(
        generation, METRICS_SNAPSHOT, capacity=METRICS_CAPACITY,
        service=SERVICE_METRICS)])[0]
    assert status(metrics) == STATUS_OK
    metrics_size = get32(metrics.raw, 52)
    assert 16 < metrics_size <= METRICS_CAPACITY
    snapshot = output(qtest, metrics, metrics_size)
    size, version, count = struct.unpack_from(">IHH", snapshot, 0)
    assert size == metrics_size
    assert version == 5
    assert count == (metrics_size - 16) // 8
    assert count > METRIC_HOST_COMMANDS
    assert get64(snapshot, 16) != 0
    assert get64(snapshot, 16 + METRIC_HOST_COMMANDS * 8) == 0
    too_small = execute(qtest, [Command(
        generation, METRICS_SNAPSHOT, capacity=metrics_size - 1,
        service=SERVICE_METRICS)])[0]
    assert status(too_small) == STATUS_INVALID

    entropy = execute(qtest, [Command(
        generation, ENTROPY_FILL, capacity=32, service=SERVICE_ENTROPY)])[0]
    assert status(entropy) == STATUS_OK
    assert get32(entropy.raw, 52) == 32
    assert output(qtest, entropy, 32) != b"\xA5" * 32
    entropy_too_large = execute(qtest, [Command(
        generation, ENTROPY_FILL, capacity=ENTROPY_MAX + 1,
        service=SERVICE_ENTROPY)])[0]
    assert status(entropy_too_large) == STATUS_INVALID

    # The provider's commands (after the metrics: they count as commands).
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)

    direct_bytes = 4096 + OPERAND_ROOM
    assert configure_channel(qtest, generation, 1,
                             byte_size=direct_bytes) == 0
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
    channel_write(qtest, BUFFER, 1, [Command(
        generation, FS_MKDIR, "/direct", value=0o755, session=work)])
    qtest.write32(BUFFER + 36, 1)
    qtest.write32(CHANNEL_APERTURE + 3 * 4096 + 0x20, 1)
    qtest.wait32(BUFFER + 48, 1)
    assert qtest.read32(BUFFER + 48) == 1
    assert qtest.read32(BUFFER + 52) == 0
    assert status(qtest.read(BUFFER + CHANNEL_HEADER_SIZE, COMMAND_SIZE)) == 0
    assert os.path.isdir(os.path.join(root, "direct"))
    assert qtest.read32(HACC_MAX_INFLIGHT) >= 1
    assert qtest.read32(HACC_CHANNEL_PENDING) == 0
    qtest.write32(CHANNEL_APERTURE + 3 * 4096 + 0x24, 1)
    assert qtest.read32(HACC_CHANNEL_PENDING) == 1
    qtest.write32(CHANNEL_APERTURE + 3 * 4096 + 0x28, 1)
    assert qtest.read32(HACC_CHANNEL_PENDING) == 0
    second_buffer = BUFFER + direct_bytes
    assert configure_channel(qtest, generation, 1, slot=4,
                             channel_generation=8,
                             buffer=second_buffer) == 0
    for buffer, slot, producer, path in (
            (BUFFER, 3, 2, "/parallel-a"),
            (second_buffer, 4, 1, "/parallel-b")):
        channel_write(qtest, buffer, 1, [Command(
            generation, FS_MKDIR, path, value=0o755, session=work)])
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
                        concurrent_capacity * COMMAND_SIZE + 64 +
                        OPERAND_ROOM)
    assert configure_channel(qtest, generation, 1, slot=5,
                             channel_generation=9,
                             buffer=concurrent_buffer,
                             byte_size=concurrent_bytes,
                             capacity=concurrent_capacity) == 0
    channel_write(qtest, concurrent_buffer, concurrent_capacity, [
        Command(generation, FS_MKDIR, f"/concurrent-{index}", value=0o755,
                session=work)
        for index in range(concurrent_capacity)])
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
    # Independent descriptions enter the worker pool together; one generic
    # bound must cover EOF selection and the write, not a preflight seek.
    race_path = os.path.join(root, "bounded-append-race")
    for round_index in range(16):
        with open(race_path, "wb"):
            pass
        opened = execute(qtest, [Command(generation, FS_OPEN,
            "/bounded-append-race", flags=OPEN_WRITE | OPEN_APPEND,
            session=work) for _ in range(concurrent_capacity)])
        assert all(status(answer) == STATUS_OK for answer in opened)
        handles = [get32(answer.raw, 16) for answer in opened]
        payloads = [bytes([index + 1]) * 128 for index in range(concurrent_capacity)]
        channel_write(qtest, concurrent_buffer, concurrent_capacity, [
            Command(generation, FS_WRITE, handle=handle, flags=IO_SEQUENTIAL,
                value=100, data=payload, session=work)
            for handle, payload in zip(handles, payloads)],
            first=(round_index + 1) * concurrent_capacity)
        producer = (round_index + 2) * concurrent_capacity
        qtest.write32(concurrent_buffer + 36, producer)
        qtest.write32(CHANNEL_APERTURE + 5 * 4096 + 0x20, producer)
        qtest.wait32(concurrent_buffer + 48, producer)
        winners = []
        for index in range(concurrent_capacity):
            answer = qtest.read(concurrent_buffer + CHANNEL_HEADER_SIZE +
                index * COMMAND_SIZE, COMMAND_SIZE)
            if status(answer) == STATUS_OK:
                assert get32(answer, 52) == 100
                winners.append(index)
            else:
                assert status(answer) == STATUS_FILE_TOO_LARGE
                assert get32(answer, 52) == 0
        assert len(winners) == 1, winners
        with open(race_path, "rb") as fixture:
            assert fixture.read() == payloads[winners[0]][:100]
        assert all(status(answer) == STATUS_OK for answer in execute(qtest, [
            Command(generation, FS_CLOSE, handle=handle, session=work)
            for handle in handles]))
    assert configure_channel(qtest, generation, 2, slot=5,
                             channel_generation=9) == 0

    handle_buffer = BUFFER + 0xA000
    handle_capacity = 64
    handle_bytes = (CHANNEL_HEADER_SIZE + handle_capacity * COMMAND_SIZE +
                    64 + OPERAND_ROOM)
    assert configure_channel(qtest, generation, 1, slot=5,
                             channel_generation=10,
                             buffer=handle_buffer,
                             byte_size=handle_bytes,
                             capacity=handle_capacity) == 0
    channel_write(qtest, handle_buffer, handle_capacity, [
        Command(generation, FS_OPEN, f"/handle-{index}",
                flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE | OPEN_EXCLUSIVE,
                value=0o600, session=work)
        for index in range(handle_capacity)])
    qtest.write32(handle_buffer + 36, handle_capacity)
    qtest.write32(CHANNEL_APERTURE + 5 * 4096 + 0x20, handle_capacity)
    qtest.wait32(handle_buffer + 48, handle_capacity)
    handles = []
    for index in range(handle_capacity):
        command = qtest.read(
            handle_buffer + CHANNEL_HEADER_SIZE + index * COMMAND_SIZE,
            COMMAND_SIZE)
        assert status(command) == STATUS_OK
        handles.append(get32(command, 16))
    assert 0 not in handles and len(set(handles)) == handle_capacity
    for round_index, (operation, extra) in enumerate((
            (FS_WRITE, {"data": b"x"}), (FS_SYNC, {}), (FS_CLOSE, {})),
            start=2):
        channel_write(qtest, handle_buffer, handle_capacity, [
            Command(generation, operation, handle=handle, session=work,
                    **extra)
            for handle in handles])
        qtest.write32(handle_buffer + 36, round_index * handle_capacity)
        qtest.write32(CHANNEL_APERTURE + 5 * 4096 + 0x20,
                      round_index * handle_capacity)
        qtest.wait32(handle_buffer + 48, round_index * handle_capacity)
        for index in range(handle_capacity):
            command = qtest.read(
                handle_buffer + CHANNEL_HEADER_SIZE + index * COMMAND_SIZE,
                COMMAND_SIZE)
            assert status(command) == STATUS_OK, (operation, status(command))
    assert configure_channel(qtest, generation, 2, slot=5,
                             channel_generation=10) == 0

    # Two reads into one writable range: the kick is refused whole.
    overlap_buffer = BUFFER + 0xC000
    overlap_capacity = 2
    overlap_data = CHANNEL_HEADER_SIZE + overlap_capacity * COMMAND_SIZE
    assert configure_channel(qtest, generation, 1, slot=5,
                             channel_generation=10,
                             buffer=overlap_buffer,
                             byte_size=overlap_data + 64 + OPERAND_ROOM,
                             capacity=overlap_capacity) == 0
    overlapping = []
    for _ in range(overlap_capacity):
        command = Command(generation, FS_READ, handle=1, session=work)
        put32(command.raw, 40, overlap_data)
        put32(command.raw, 48, 64)
        overlapping.append(command)
    channel_write(qtest, overlap_buffer, overlap_capacity, overlapping)
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
    drain_bytes = (CHANNEL_HEADER_SIZE + drain_capacity * COMMAND_SIZE + 64 +
                   OPERAND_ROOM)
    assert configure_channel(qtest, generation, 1, slot=5,
                             channel_generation=11, buffer=drain_buffer,
                             byte_size=drain_bytes,
                             capacity=drain_capacity) == 0
    channel_write(qtest, drain_buffer, drain_capacity, [
        Command(generation, FS_MKDIR, f"/drain-{index}", value=0o755,
                session=work)
        for index in range(drain_capacity)])
    qtest.write32(drain_buffer + 36, drain_capacity)
    qtest.write32(CHANNEL_APERTURE + 5 * 4096 + 0x20, drain_capacity)
    assert configure_channel(qtest, generation, 2, slot=5,
                             channel_generation=11) == 0
    last_status = (drain_buffer + CHANNEL_HEADER_SIZE +
                   (drain_capacity - 1) * COMMAND_SIZE + 12)
    qtest.write32(last_status, 0xFEEDFACE)
    time.sleep(0.05)
    assert qtest.read32(last_status) == 0xFEEDFACE

    command_only_bytes = CHANNEL_HEADER_SIZE + COMMAND_SIZE
    assert configure_channel(qtest, generation, 1, slot=6,
                             channel_generation=12,
                             byte_size=command_only_bytes) == 0
    assert get32(qtest.read(BUFFER, CHANNEL_HEADER_SIZE), 28) == \
        command_only_bytes
    assert configure_channel(qtest, generation, 2, slot=6,
                             channel_generation=12) == 0
    assert configure_channel(qtest, generation, 1, slot=6,
                             channel_generation=13,
                             byte_size=command_only_bytes - 1) == \
        SYSCALL_INVALID_ARGUMENT

    rejected = bytearray(SUBMISSION_SIZE)
    struct.pack_into(">IHHIIIII", rejected, 0, SUBMISSION_SIZE, 1, 0,
                     PROVIDER, generation + 1, BUFFER, COMMAND_SIZE, 1)
    qtest.write(SUBMISSION, rejected)
    qtest.write32(HACC_SUBMIT, SUBMISSION)
    assert qtest.read32(HACC_SUBMIT_RESULT) == 8
    struct.pack_into(">I", rejected, 28, 1)
    qtest.write(SUBMISSION, rejected)
    qtest.write32(HACC_SUBMIT, SUBMISSION)
    assert qtest.read32(HACC_SUBMIT_RESULT) == 2

    results = execute(qtest, [
        Command(generation, FS_MKDIR, f"/batch-{index}", value=0o755,
                session=work) for index in range(8)])
    assert all(status(result) == STATUS_OK for result in results)

    opened = execute(qtest, [Command(
        generation, FS_OPEN, "/data", flags=OPEN_READ | OPEN_WRITE |
        OPEN_CREATE | OPEN_EXCLUSIVE, value=0o600, session=work)])[0]
    assert status(opened) == STATUS_OK
    handle = get32(opened.raw, 16)
    assert handle != 0
    opened_stat = execute(qtest, [Command(
        generation, FS_STAT, handle=handle, session=work)])[0]
    assert status(opened_stat) == STATUS_OK
    actual_mode = struct.unpack_from(">H", opened_stat.raw, NODE_MODE)[0]
    assert actual_mode & 0o7777 == 0o600, oct(actual_mode)
    assert actual_mode & 0o170000 == 0o100000, oct(actual_mode)
    missing_stat = execute(qtest, [Command(
        generation, FS_STAT, handle=0xFFFFFFFF, session=work)])[0]
    assert status(missing_stat) == STATUS_BAD_HANDLE

    reopened = execute(qtest, [Command(
        generation, FS_OPEN, "/data", flags=OPEN_READ, value=0o600,
        session=work)])[0]
    assert status(reopened) == STATUS_OK, status(reopened)
    reopened_handle = get32(reopened.raw, 16)
    assert reopened_handle != 0
    reopened_close = execute(qtest, [Command(
        generation, FS_CLOSE, handle=reopened_handle, session=work)])[0]
    assert status(reopened_close) == STATUS_OK

    write_only = execute(qtest, [Command(
        generation, FS_OPEN, "/write-only", flags=OPEN_WRITE | OPEN_CREATE |
        OPEN_EXCLUSIVE, value=0o600, session=work)])[0]
    assert status(write_only) == STATUS_OK
    write_only_handle = get32(write_only.raw, 16)
    denied_read = execute(qtest, [Command(
        generation, FS_READ, handle=write_only_handle, capacity=1,
        session=work)])[0]
    assert status(denied_read) == STATUS_ACCESS
    assert status(execute(qtest, [Command(
        generation, FS_CLOSE, handle=write_only_handle,
        session=work)])[0]) == STATUS_OK

    read_only = execute(qtest, [Command(
        generation, FS_OPEN, "/write-only", flags=OPEN_READ,
        session=work)])[0]
    assert status(read_only) == STATUS_OK
    read_only_handle = get32(read_only.raw, 16)
    denied_write = execute(qtest, [Command(
        generation, FS_WRITE, handle=read_only_handle, data=b"x",
        session=work)])[0]
    assert status(denied_write) == STATUS_ACCESS
    denied_truncate = execute(qtest, [Command(
        generation, FS_TRUNCATE, handle=read_only_handle, value=0,
        session=work)])[0]
    assert status(denied_truncate) == STATUS_ACCESS
    assert status(execute(qtest, [Command(
        generation, FS_CLOSE, handle=read_only_handle,
        session=work)])[0]) == STATUS_OK

    payload = b"host transport data integrity\x00\xff"
    written = execute(qtest, [Command(
        generation, FS_WRITE, handle=handle, data=payload,
        session=work)])[0]
    assert status(written) == STATUS_OK
    assert get32(written.raw, 52) == len(payload)

    capacity = len(payload)
    read = execute(qtest, [Command(
        generation, FS_READ, handle=handle, capacity=capacity,
        session=work)])[0]
    assert status(read) == STATUS_OK
    assert get32(read.raw, 52) == capacity
    assert output(qtest, read, capacity) == payload

    hard_linked = execute(qtest, [Command(
        generation, FS_LINK, "/data", "/data-hard-link", session=work)])[0]
    assert status(hard_linked) == STATUS_OK
    assert os.path.samefile(os.path.join(root, "data"),
                            os.path.join(root, "data-hard-link"))
    for operation in (FS_RENAME, FS_LINK):
        refused = execute(qtest, [Command(generation, operation, "/data",
            "/absent/", session=work)])[0]
        assert status(refused) != STATUS_OK
        assert os.path.exists(os.path.join(root, "data"))
        assert not os.path.exists(os.path.join(root, "absent"))
    os.mkdir(os.path.join(root, "dir-target"))
    os.symlink("dir-target", os.path.join(root, "dir-link"))
    made = execute(qtest, [Command(generation, FS_MKDIR, "/dir-link/",
        value=0o755, session=work)])[0]
    assert status(made) == STATUS_EXISTS
    read_link = execute(qtest, [Command(generation, FS_READLINK, "/dir-link/",
        capacity=NAME_CAPACITY, session=work)])[0]
    assert status(read_link) == STATUS_INVALID, ("trailing-slash READLINK", status(read_link))
    for operation, path2 in ((FS_UNLINK, ""), (FS_RENAME, "/stolen-dir"),
                             (FS_LINK, "/stolen-dir")):
        refused = execute(qtest, [Command(generation, operation, "/dir-link/",
            path2, session=work)])[0]
        assert status(refused) == STATUS_NOT_DIR
        assert os.path.islink(os.path.join(root, "dir-link"))
        assert os.path.isdir(os.path.join(root, "dir-target"))

    malformed = Command(generation, FS_READ, handle=handle, session=work)
    put32(malformed.raw, 48, 1)
    assert status(execute(qtest, [malformed])[0]) == STATUS_INVALID

    made_link = execute(qtest, [Command(
        generation, FS_SYMLINK, "/data", "/data-link", session=work)])[0]
    assert status(made_link) == STATUS_OK
    opened_link = execute(qtest, [Command(
        generation, FS_OPEN, "/data-link", flags=OPEN_READ,
        capacity=NAME_CAPACITY, session=work)])[0]
    assert status(opened_link) == STATUS_ESCAPED, status(opened_link)
    assert output(qtest, opened_link, get32(opened_link.raw, 52)) == b"/data"
    assert get32(opened_link.raw, 56) == len("/data-link")
    assert get32(opened_link.raw, 36) == 2 and get32(opened_link.raw, 32) == 1
    refused_link = execute(qtest, [Command(
        generation, FS_OPEN, "/data-link", flags=OPEN_READ | OPEN_NOFOLLOW,
        session=work)])[0]
    assert status(refused_link) == STATUS_LOOP
    os.symlink("data", os.path.join(root, "relative-link"))
    relative = execute(qtest, [Command(
        generation, FS_OPEN, "/relative-link", flags=OPEN_READ, session=work)])[0]
    assert status(relative) == STATUS_OK
    assert status(execute(qtest, [Command(generation, FS_CLOSE,
        handle=get32(relative.raw, 16), session=work)])[0]) == STATUS_OK
    followed = execute(qtest, [Command(generation, FS_STAT, "/relative-link",
        flags=STAT_FOLLOW | STAT_BUDGET, offset=1, session=work)])[0]
    assert status(followed) == STATUS_OK and get32(followed.raw, 32) == 1
    limited = execute(qtest, [Command(generation, FS_STAT, "/relative-link",
        flags=STAT_FOLLOW | STAT_BUDGET, offset=0, session=work)])[0]
    assert status(limited) == STATUS_LOOP
    missing = execute(qtest, [Command(generation, FS_STAT, "/new-leaf",
        flags=STAT_FOLLOW | STAT_MISSING | STAT_BUDGET, offset=40,
        session=work)])[0]
    assert status(missing) == STATUS_OK
    assert struct.unpack_from(">H", missing.raw, NODE_KIND)[0] == 0
    missing_parent = execute(qtest, [Command(generation, FS_STAT,
        "/new-dir/../new-leaf", flags=STAT_FOLLOW | STAT_MISSING | STAT_BUDGET,
        offset=40, session=work)])[0]
    assert status(missing_parent) == 2
    linked = execute(qtest, [Command(
        generation, FS_STAT, "/data-link", session=work)])[0]
    assert status(linked) == STATUS_OK
    assert struct.unpack_from(">H", linked.raw, NODE_KIND)[0] == KIND_SYMLINK

    target = b"/data"
    exact = execute(qtest, [Command(
        generation, FS_READLINK, "/data-link", capacity=len(target),
        session=work)])[0]
    assert status(exact) == STATUS_OK
    assert get32(exact.raw, 52) == len(target)
    assert output(qtest, exact, len(target)) == target

    os.symlink(outside, os.path.join(root, "escape"))
    escaped = execute(qtest, [Command(
        generation, FS_STAT, "/escape/sentinel", capacity=PATH_MAX,
        session=work)])[0]
    assert status(escaped) == STATUS_ESCAPED, status(escaped)
    assert output(qtest, escaped, get32(escaped.raw, 52)) == (outside + "/sentinel").encode()
    assert get32(escaped.raw, 56) == len("/escape") and get32(escaped.raw, 36) == 2
    traversed = execute(qtest, [Command(
        generation, FS_STAT, "/../sentinel", capacity=NAME_CAPACITY,
        session=work)])[0]
    assert status(traversed) == STATUS_ESCAPED
    assert output(qtest, traversed, get32(traversed.raw, 52)) == b"/sentinel"
    assert get32(traversed.raw, 56) == 3 and get32(traversed.raw, 36) == 1
    for malformed_path in ("//data", "/./data", "/batch-0/../data"):
        malformed_path_result = execute(qtest, [Command(
            generation, FS_STAT, malformed_path, session=work)])[0]
        assert status(malformed_path_result) == STATUS_OK, (
            malformed_path, status(malformed_path_result))
    renamed = execute(qtest, [Command(
        generation, FS_RENAME, "/data", "/escape/stolen",
        capacity=NAME_CAPACITY, session=work)])[0]
    assert status(renamed) == STATUS_ESCAPED, status(renamed)
    assert output(qtest, renamed, get32(renamed.raw, 52)) == (outside + "/stolen").encode()
    with open(os.path.join(outside, "sentinel"), "rb") as source:
        assert source.read() == b"outside"
    assert not os.path.exists(os.path.join(outside, "stolen"))

    opened_directory = execute(qtest, [Command(
        generation, FS_OPEN, "/", flags=OPEN_READ | OPEN_DIRECTORY,
        session=work)])[0]
    assert status(opened_directory) == STATUS_OK
    directory = get32(opened_directory.raw, 16)
    denied_directory_read = execute(qtest, [Command(
        generation, FS_READ, handle=directory, capacity=1,
        session=work)])[0]
    assert status(denied_directory_read) == STATUS_IS_DIR
    first = execute(qtest, [Command(
        generation, FS_READDIR, handle=directory, capacity=NAME_CAPACITY,
        session=work)])[0]
    assert status(first) == STATUS_OK
    first_name, = directory_names(output(qtest, first, get32(first.raw, 52)))
    cursor = get64(first.raw, 32)
    assert first_name not in (b".", b"..") and cursor != 0
    second = execute(qtest, [Command(
        generation, FS_READDIR, handle=directory, offset=cursor,
        capacity=NAME_CAPACITY, session=work)])[0]
    assert status(second) == STATUS_OK, status(second)
    second_name, = directory_names(output(qtest, second, get32(second.raw, 52)))
    assert second_name not in (b".", b"..", first_name)
    # With a handle, READDIR takes no path.
    assert status(execute(qtest, [Command(
        generation, FS_READDIR, "/", handle=directory,
        capacity=NAME_CAPACITY, session=work)])[0]) == STATUS_INVALID

    relative_open = execute(qtest, [Command(
        generation, FS_OPEN, "relative-child", handle=directory,
        flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE | OPEN_EXCLUSIVE,
        value=0o600, session=work)])[0]
    assert status(relative_open) == STATUS_OK
    relative_handle = get32(relative_open.raw, 16)
    assert relative_handle != 0
    changed_file = execute(qtest, [Command(
        generation, FS_CHMOD, handle=relative_handle, value=0o640,
        session=work)])[0]
    assert status(changed_file) == STATUS_OK
    assert (os.stat(os.path.join(root, "relative-child")).st_mode & 0o777) == \
        0o640
    changed_at = execute(qtest, [Command(
        generation, FS_CHMOD, "relative-child", handle=directory,
        value=0o600, session=work)])[0]
    assert status(changed_at) == STATUS_OK
    assert (os.stat(os.path.join(root, "relative-child")).st_mode & 0o777) == \
        0o600
    relative_stat = execute(qtest, [Command(
        generation, FS_STAT, "relative-child", handle=directory,
        session=work)])[0]
    assert status(relative_stat) == STATUS_OK
    assert get64(relative_stat.raw, NODE_INODE) == \
        os.stat(os.path.join(root, "relative-child")).st_ino
    os.mkdir(os.path.join(root, "moving"))
    with open(os.path.join(root, "moving", "child"), "wb") as fixture:
        fixture.write(b"kept")
    held = execute(qtest, [Command(generation, FS_OPEN, "/moving",
        flags=OPEN_READ | OPEN_DIRECTORY, session=work)])[0]
    assert status(held) == STATUS_OK
    moving_handle = get32(held.raw, 16)
    moved_path = os.path.join(outside, "moving")
    os.rename(os.path.join(root, "moving"), moved_path)
    for operation, name, flags, capacity in (
            (FS_STAT, "", 0, 0), (FS_STAT, "child", 0, 0),
            (FS_OPEN, "child", OPEN_WRITE | OPEN_TRUNCATE, 0),
            (FS_READDIR, "", 0, NAME_CAPACITY), (FS_CHMOD, "", 0, 0)):
        refused = execute(qtest, [Command(generation, operation, name,
            handle=moving_handle, flags=flags, capacity=capacity,
            value=0o700, session=work)])[0]
        assert status(refused) != STATUS_OK
    with open(os.path.join(moved_path, "child"), "rb") as fixture:
        assert fixture.read() == b"kept"
    os.rename(moved_path, os.path.join(root, "moving"))
    restored = execute(qtest, [Command(generation, FS_STAT, "child",
        handle=moving_handle, session=work)])[0]
    assert status(restored) == STATUS_OK
    assert status(execute(qtest, [Command(generation, FS_CLOSE,
        handle=moving_handle, session=work)])[0]) == STATUS_OK
    # A relative path needs a directory handle, an absolute one none.
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "relative-child", handle=relative_handle,
        session=work)])[0]) != STATUS_OK
    assert status(execute(qtest, [Command(
        generation, FS_STAT, "/relative-child", handle=directory,
        session=work)])[0]) == STATUS_INVALID
    filesystem_info = execute(qtest, [Command(
        generation, FS_FILESYSTEM_INFO, "/", capacity=64,
        session=work)])[0]
    assert status(filesystem_info) == STATUS_OK
    assert get32(filesystem_info.raw, 52) == 64
    filesystem_payload = output(qtest, filesystem_info, 64)
    assert struct.unpack_from(">I", filesystem_payload, 0)[0] == 64
    assert struct.unpack_from(">I", filesystem_payload, 8)[0] > 0
    assert struct.unpack_from(">Q", filesystem_payload, 16)[0] > 0
    assert status(execute(qtest, [Command(
        generation, FS_CLOSE, handle=relative_handle,
        session=work)])[0]) == STATUS_OK
    # Both bases of a rename are directories of the session.
    renamed_at = execute(qtest, [Command(
        generation, FS_RENAME, "relative-child", "renamed-child",
        handle=directory, value=directory, session=work)])[0]
    assert status(renamed_at) == STATUS_OK
    assert os.path.exists(os.path.join(root, "renamed-child"))
    removed_at = execute(qtest, [Command(
        generation, FS_UNLINK, "renamed-child", handle=directory,
        session=work)])[0]
    assert status(removed_at) == STATUS_OK
    assert not os.path.exists(os.path.join(root, "renamed-child"))
    # Removal checks the required kind; zero flags must not remove a directory.
    assert status(execute(qtest, [Command(
        generation, FS_UNLINK, "/batch-7", session=work)])[0]) == STATUS_IS_DIR
    assert os.path.isdir(os.path.join(root, "batch-7"))
    assert status(execute(qtest, [Command(
        generation, FS_UNLINK, "/write-only", flags=UNLINK_DIRECTORY,
        session=work)])[0]) == STATUS_NOT_DIR
    assert os.path.exists(os.path.join(root, "write-only"))
    assert status(execute(qtest, [Command(
        generation, FS_UNLINK, "/write-only", flags=2,
        session=work)])[0]) == STATUS_INVALID
    assert status(execute(qtest, [Command(
        generation, FS_UNLINK, "/write-only", flags=3,
        session=work)])[0]) == STATUS_INVALID
    assert status(execute(qtest, [Command(
        generation, FS_UNLINK, "/batch-7", flags=UNLINK_DIRECTORY,
        session=work)])[0]) == STATUS_OK

    closed_directory = execute(qtest, [Command(
        generation, FS_CLOSE, handle=directory, session=work)])[0]
    assert status(closed_directory) == STATUS_OK

    # Another HOST_DEVICE holder is refused before any handle is looked up.
    crossed = execute(qtest, [Command(
        generation, FS_CLOSE, handle=handle, session=work)],
        owner=0x1002)[0]
    assert status(crossed) == STATUS_ACCESS
    qtest.write32(HACC_RELEASE_OWNER, PROVIDER)
    released = execute(qtest, [Command(
        generation, FS_CLOSE, handle=handle, session=work)])[0]
    assert status(released) == STATUS_ACCESS

    qtest.write32(HACC_RESET, 1)
    next_generation = qtest.read32(HACC_GENERATION)
    assert next_generation != generation
    assert claim(qtest, next_generation) == STATUS_OK
    fresh = open_session(qtest, next_generation)
    stale = execute(qtest, [Command(
        next_generation, FS_CLOSE, handle=handle, session=fresh)])[0]
    assert status(stale) == STATUS_BAD_HANDLE
    print("PASS run")


def descriptions(qtest, root):
    """Position and mutable flags belong to one description across owners."""
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    session = open_session(qtest, generation)
    other = open_session(qtest, generation)
    path = os.path.join(root, "work", "shared-description")
    with open(path, "wb") as fixture:
        fixture.write(b"abcdef")

    def parent(op, **kwargs):
        return execute(qtest, [Command(generation, op, session=session,
                                        **kwargs)])[0]

    opened = parent(FS_OPEN, path="/shared-description",
                    flags=OPEN_READ | OPEN_WRITE | OPEN_APPEND)
    assert status(opened) == STATUS_OK
    handle = get32(opened.raw, 16)
    first = parent(FS_READ, handle=handle, flags=IO_SEQUENTIAL, capacity=2)
    assert status(first) == STATUS_OK and get64(first.raw, 32) == 2
    assert output(qtest, first, 2) == b"ab"
    shared = parent(FS_SHARE, handle=handle)
    assert status(shared) == STATUS_OK
    token = get64(shared.raw, 32)
    assert token != 0

    buffer = 0x02080000
    assert configure_scope(qtest, generation, CONFIG_OPEN, 25, 61,
                           session, SCOPE_BOUND, PROVIDER, buffer=buffer) == 0
    producer = 0

    def child(op, session_id=session, **kwargs):
        nonlocal producer
        producer += 1
        command = Command(generation, op, session=session_id, **kwargs)
        at = scoped_post(qtest, 25, producer, command, buffer=buffer)
        return scoped_wait(qtest, producer, at, buffer=buffer)

    assert configure_scope(qtest, generation, CONFIG_ATTACH, 25, 61,
                           other, SCOPE_BOUND, PROVIDER) == 0
    assert status(child(FS_ADOPT, session_id=other, offset=token)) == STATUS_ACCESS
    adopted = child(FS_ADOPT, offset=token)
    assert status(adopted) == STATUS_OK
    alias = get32(adopted, 16)
    assert alias != 0 and alias != handle
    assert status(child(FS_ADOPT, offset=token)) == STATUS_BAD_HANDLE
    positioned = child(FS_READ, handle=alias, offset=0, capacity=2)
    assert status(positioned) == STATUS_OK
    assert qtest.read(buffer + get32(positioned, 40), 2) == b"ab"
    sequential = child(FS_READ, handle=alias, flags=IO_SEQUENTIAL,
                       offset=99, capacity=2)
    assert status(sequential) == STATUS_OK and get64(sequential, 32) == 4
    assert qtest.read(buffer + get32(sequential, 40), 2) == b"cd"
    assert get64(parent(FS_SEEK, handle=handle, flags=1).raw, 32) == 4

    appended = child(FS_WRITE, handle=alias, flags=IO_SEQUENTIAL, value=7, data=b"XYZ")
    assert status(appended) == STATUS_OK and get64(appended, 32) == 7
    assert get32(appended, 52) == 1
    assert status(child(FS_WRITE, handle=alias, flags=IO_SEQUENTIAL,
                        value=7, data=b"x")) == STATUS_FILE_TOO_LARGE
    assert status(child(FS_WRITE, handle=alias, offset=7, value=7, data=b"x")) == STATUS_FILE_TOO_LARGE
    assert status(child(FS_WRITE, handle=alias, offset=6, value=7, data=b"XZ")) == STATUS_OK
    assert get64(parent(FS_SEEK, handle=handle, flags=1).raw, 32) == 7
    assert status(child(FS_SEEK, handle=alias, offset=1)) == STATUS_OK
    empty = child(FS_WRITE, handle=alias, flags=IO_SEQUENTIAL, value=0)
    assert status(empty) == STATUS_OK and get32(empty, 52) == 0
    assert get64(parent(FS_SEEK, handle=handle, flags=1).raw, 32) == 1
    assert status(child(FS_SEEK, handle=alias, offset=7)) == STATUS_OK
    overwritten = child(FS_WRITE, handle=alias, offset=0, data=b"Z")
    assert status(overwritten) == STATUS_OK
    assert get64(parent(FS_SEEK, handle=handle, flags=1).raw, 32) == 7
    assert open(path, "rb").read() == b"ZbcdefX"
    cleared = child(FS_FILE_FLAGS, handle=alias,
                    flags=OPEN_APPEND | OPEN_NONBLOCK, value=OPEN_NONBLOCK)
    assert status(cleared) == STATUS_OK and not get32(cleared, 36) & OPEN_APPEND
    assert get32(parent(FS_FILE_FLAGS, handle=handle).raw, 36) & \
        (OPEN_APPEND | OPEN_NONBLOCK) == OPEN_NONBLOCK
    assert status(child(FS_SEEK, handle=alias, offset=1)) == STATUS_OK
    ordinary = parent(FS_WRITE, handle=handle, flags=IO_SEQUENTIAL, data=b"Y")
    assert status(ordinary) == STATUS_OK and get64(ordinary.raw, 32) == 2
    assert open(path, "rb").read() == b"ZYcdefX"
    assert status(child(FS_SEEK, handle=alias, flags=3)) == STATUS_INVALID
    assert status(child(FS_SEEK, handle=alias, offset=(1 << 64) - 1)) != STATUS_OK
    assert status(child(FS_FILE_FLAGS, handle=alias, flags=OPEN_WRITE,
                        value=OPEN_WRITE)) == STATUS_INVALID

    pending = child(FS_SHARE, handle=alias)
    pending_token = get64(pending, 32)
    assert status(pending) == STATUS_OK and pending_token != 0
    assert status(parent(FS_ADOPT, flags=ADOPT_DISCARD,
                         offset=pending_token)) == STATUS_ACCESS
    assert status(child(FS_CLOSE, handle=alias)) == STATUS_OK
    held = parent(FS_ADOPT, offset=pending_token)
    assert status(held) == STATUS_OK
    held_handle = get32(held.raw, 16)
    withdrawn = parent(FS_SHARE, handle=handle)
    assert status(withdrawn) == STATUS_OK
    withdrawn_token = get64(withdrawn.raw, 32)
    assert status(parent(FS_ADOPT, flags=ADOPT_DISCARD,
                         offset=withdrawn_token)) == STATUS_OK
    assert status(parent(FS_ADOPT, offset=withdrawn_token)) == STATUS_BAD_HANDLE
    assert status(parent(FS_CLOSE, handle=handle)) == STATUS_OK
    assert status(parent(FS_READ, handle=held_handle, flags=IO_SEQUENTIAL,
                         capacity=1)) == STATUS_OK
    assert status(parent(FS_CLOSE, handle=held_handle)) == STATUS_OK
    assert not provider_holds(path)
    # A dead sharer loses pending references; already adopted owners retain theirs.
    directory_path = os.path.join(root, "work", "shared-directory")
    os.mkdir(directory_path)
    expected = {b"alpha", b"bravo", b"charlie"}
    for name in expected:
        open(os.path.join(directory_path, name.decode()), "wb").close()
    opened = parent(FS_OPEN, path="/shared-directory",
                    flags=OPEN_READ | OPEN_DIRECTORY)
    assert status(opened) == STATUS_OK
    directory = get32(opened.raw, 16)
    receipt = parent(FS_SHARE, handle=directory)
    adopted = child(FS_ADOPT, offset=get64(receipt.raw, 32))
    assert status(receipt) == STATUS_OK and status(adopted) == STATUS_OK
    child_directory = get32(adopted, 16)

    def parent_names(capacity=128, limit=1):
        answer = parent(FS_READDIR, handle=directory, offset=CURSOR_CURRENT,
                        value=limit, capacity=capacity)
        assert status(answer) == STATUS_OK, status(answer)
        return directory_names(output(qtest, answer, get32(answer.raw, 52)))

    first, = parent_names()
    answer = child(FS_READDIR, handle=child_directory, offset=CURSOR_CURRENT,
                   capacity=128)
    assert status(answer) == STATUS_OK
    second, = directory_names(qtest.read(buffer + get32(answer, 40),
                                         get32(answer, 52)))
    third, = parent_names()
    assert {first, second, third} == expected
    assert status(child(FS_SEEK, handle=child_directory, offset=0)) == STATUS_OK
    too_small = parent(FS_READDIR, handle=directory, offset=CURSOR_CURRENT,
                       capacity=33)
    assert status(too_small) == 15
    assert parent_names() == [first], "small buffer consumed directory entry"
    assert status(parent(FS_SEEK, handle=directory, offset=0)) == STATUS_OK
    assert parent_names(capacity=32 + len(first) + 32, limit=2) == [first]
    assert parent_names() == [second], "partial batch consumed next entry"
    assert parent_names() == [third]
    assert status(parent(FS_CLOSE, handle=directory)) == STATUS_OK
    assert status(child(FS_CLOSE, handle=child_directory)) == STATUS_OK
    assert not provider_holds(directory_path)
    opened = child(FS_OPEN, path="/shared-description", flags=OPEN_READ)
    assert status(opened) == STATUS_OK
    dying_handle = get32(opened, 16)
    adopted_token = get64(child(FS_SHARE, handle=dying_handle), 32)
    survivor = parent(FS_ADOPT, offset=adopted_token)
    assert status(survivor) == STATUS_OK
    survivor_handle = get32(survivor.raw, 16)
    abandoned = get64(child(FS_SHARE, handle=dying_handle), 32)
    qtest.write32(HACC_RELEASE_OWNER, CLIENT)
    assert status(parent(FS_ADOPT, offset=abandoned)) == STATUS_BAD_HANDLE
    alive = parent(FS_READ, handle=survivor_handle, flags=IO_SEQUENTIAL, capacity=1)
    assert status(alive) == STATUS_OK and output(qtest, alive, 1) == b"Z"
    assert status(parent(FS_CLOSE, handle=survivor_handle)) == STATUS_OK
    assert not provider_holds(path)
    print("PASS shared-descriptions")


def paths(qtest, root):
    """Directory identity survives rename; its spelling must remain confined."""
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    session = open_session(qtest, generation)
    before = os.path.join(root, "work", "cwd-before")
    after = os.path.join(root, "work", "cwd-after")
    os.mkdir(before)

    def call(operation, **kwargs):
        return execute(qtest, [Command(generation, operation, session=session,
                                        **kwargs)])[0]

    opened = call(FS_OPEN, path="/cwd-before", flags=OPEN_READ | OPEN_DIRECTORY)
    assert status(opened) == STATUS_OK
    handle = get32(opened.raw, 16)

    def where(expected):
        answer = call(FS_PATH, handle=handle, capacity=PATH_MAX)
        assert status(answer) == STATUS_OK, status(answer)
        assert output(qtest, answer, get32(answer.raw, 52)) == expected

    where(b"/cwd-before")
    assert status(call(FS_PATH, handle=handle, capacity=1)) == 15
    assert status(call(FS_PATH, handle=handle, path="x", capacity=128)) == STATUS_INVALID
    os.rename(before, after)
    os.mkdir(before)
    where(b"/cwd-after")
    os.rmdir(after)
    os.mkdir(after)
    assert status(call(FS_PATH, handle=handle, capacity=128)) == STATUS_NOT_FOUND
    assert status(call(FS_CLOSE, handle=handle)) == STATUS_OK
    opened = call(FS_OPEN, path="/cwd-before", flags=OPEN_READ | OPEN_DIRECTORY)
    assert status(opened) == STATUS_OK
    handle = get32(opened.raw, 16)
    os.rename(before, os.path.join(root, "cwd-outside"))
    assert status(call(FS_PATH, handle=handle, capacity=128)) == STATUS_NOT_FOUND
    assert status(call(FS_CLOSE, handle=handle)) == STATUS_OK
    assert not provider_holds(os.path.join(root, "cwd-outside"))
    print("PASS directory-paths")


def provider_loss(qtest, root, qmp_path):
    """Reset only the Linux filesystem provider; unrelated channels stay live."""
    QmpClient = runpy.run_path(os.path.join(os.path.dirname(__file__),
        "astra-input-hotplug.py"))["QmpClient"]
    qmp = QmpClient(qmp_path)
    try:
        qtest.write32(HACC_RESET, 1)
        generation = qtest.read32(HACC_GENERATION)
        result, epoch = claim(qtest, generation, epoch=True)
        assert result == STATUS_OK and epoch != 0
        session = open_session(qtest, generation)
        assert configure_scope(qtest, generation, CONFIG_OPEN, 7, 21,
            session, SCOPE_BOUND, PROVIDER) == 0
        produced = 0
        def call(operation, **kwargs):
            nonlocal produced
            produced += 1
            return scoped(qtest, 7, produced, Command(generation, operation,
                session=session, **kwargs))
        for attempt in range(12):
            opened = call(FS_OPEN, path="/lost.bin", flags=OPEN_READ | OPEN_WRITE | OPEN_CREATE, value=0o644)
            assert status(opened) == STATUS_OK, (attempt, status(opened))
            handle = get32(opened, 16)
            host_path = os.path.join(root, "work", "lost.bin")
            assert provider_holds(host_path)
            qmp.execute("qom-set", {"path": "/machine",
                "property": "astra-host-fs-reset", "value": True})
            assert qtest.read32(HACC_GENERATION) == generation
            assert not provider_holds(host_path)
            assert status(call(FS_READ, handle=handle, capacity=8)) == STATUS_LOST
            assert status(execute(qtest, [Command(generation, FS_STAT,
                path="/", session=session)], owner=0xDEAD)[0]) == STATUS_ACCESS
            result, next_epoch = claim(qtest, generation, epoch=True)
            assert result == STATUS_OK and next_epoch != epoch
            epoch = next_epoch
            old = session
            assert configure_scope(qtest, generation, CONFIG_DETACH, 7, 21, old) == 0
            session = open_session(qtest, generation)
            assert configure_scope(qtest, generation, CONFIG_ATTACH, 7, 21,
                session, SCOPE_BOUND, PROVIDER) == 0
            # An old FD never binds to a new file, even after a fresh session.
            assert status(call(FS_STAT, path="/lost.bin")) == STATUS_OK
            assert status(call(FS_READ, handle=handle, capacity=8)) == STATUS_LOST
            shared = call(FS_SHARE, handle=handle)
            assert status(shared) == STATUS_OK
            token = get64(shared, 32)
            child = CLIENT + 1
            assert configure_scope(qtest, generation, CONFIG_OPEN, 8, 22,
                session, SCOPE_BOUND, PROVIDER, owner=child, buffer=RACE_BUFFER) == 0
            child_produced = 0
            def child_call(operation, **kwargs):
                nonlocal child_produced
                child_produced += 1
                at = scoped_post(qtest, 8, child_produced, Command(generation,
                    operation, session=session, **kwargs), buffer=RACE_BUFFER)
                return scoped_wait(qtest, child_produced, at, buffer=RACE_BUFFER)
            assert status(child_call(FS_ADOPT, offset=token, flags=ADOPT_DISCARD)) == STATUS_ACCESS
            adopted = child_call(FS_ADOPT, offset=token)
            assert status(adopted) == STATUS_OK
            inherited = get32(adopted, 16)
            assert inherited != handle
            assert status(child_call(FS_READ, handle=handle, capacity=8)) == STATUS_BAD_HANDLE
            assert status(child_call(FS_READ, handle=inherited, capacity=8)) == STATUS_LOST
            assert status(child_call(FS_CLOSE, handle=inherited)) == STATUS_OK
            assert configure_scope(qtest, generation, CONFIG_CLOSE, 8, 22, owner=child) == 0
            assert status(call(FS_CLOSE, handle=handle)) == STATUS_OK
            assert status(call(FS_READ, handle=handle, capacity=8)) == STATUS_BAD_HANDLE
        assert qmp.execute("qom-get", {"path": "/machine",
            "property": "astra-host-fs-resets"}) == 12
        assert configure_scope(qtest, generation, CONFIG_CLOSE, 7, 21) == 0
        print("PASS provider-loss resets=12 old-fds-lost inherited-fds-lost fresh-paths-ok")
    finally:
        qmp.close()


def budgets(qtest, root, qmp_path):
    """Admission precedes mutation; aliases, tokens and lost IDs stay bounded."""
    QmpClient = runpy.run_path(os.path.join(os.path.dirname(__file__),
        "astra-input-hotplug.py"))["QmpClient"]
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    session = open_session(qtest, generation)
    fixture = os.path.join(root, "work", "quota-file")
    with open(fixture, "wb") as stream:
        stream.write(b"unchanged")
    qmp = QmpClient(qmp_path)
    produced = {7: 0, 8: 0}
    buffers = {7: SCOPED_BUFFER, 8: RACE_BUFFER}

    def gauge(name):
        return qmp.execute("qom-get", {"path": "/machine",
            "property": "astra-host-" + name})

    def call(slot, operation, **kwargs):
        produced[slot] += 1
        at = scoped_post(qtest, slot, produced[slot], Command(generation,
            operation, session=session, **kwargs), buffer=buffers[slot])
        return scoped_wait(qtest, produced[slot], at, buffer=buffers[slot])

    def direct(operation, **kwargs):
        return execute(qtest, [Command(generation, operation,
            session=session, **kwargs)])[0]

    try:
        for slot, owner in ((7, CLIENT), (8, CLIENT + 1)):
            assert configure_scope(qtest, generation, CONFIG_OPEN, slot,
                21 + slot, session, SCOPE_BOUND, PROVIDER, owner=owner,
                buffer=buffers[slot], scope_files=4) == 0
        handles = [get32(call(7, FS_OPEN, path="/quota-file", flags=OPEN_READ), 16)
                   for _ in range(2)]
        assert all(handles)
        shares = [call(7, FS_SHARE, handle=handles[0]) for _ in range(2)]
        assert all(status(reply) == STATUS_OK for reply in shares)
        tokens = [get64(reply, 32) for reply in shares]
        assert gauge("fs-descriptors") == 2
        assert gauge("fs-objects") == 5
        for path, flags in (("/quota-new", OPEN_WRITE | OPEN_CREATE),
                            ("/quota-file", OPEN_WRITE | OPEN_TRUNCATE)):
            assert status(call(7, FS_OPEN, path=path, flags=flags,
                               value=0o644)) == STATUS_LIMIT
        assert not os.path.exists(os.path.join(root, "work", "quota-new"))
        assert open(fixture, "rb").read() == b"unchanged"
        other = [get32(call(8, FS_OPEN, path="/quota-file", flags=OPEN_READ), 16)
                 for _ in range(4)]
        assert all(other), other
        assert status(call(8, FS_ADOPT, offset=tokens[0])) == STATUS_LIMIT
        assert status(call(8, FS_CLOSE, handle=other.pop())) == STATUS_OK
        before = gauge("fs-descriptors")
        alias = call(8, FS_ADOPT, offset=tokens[0])
        assert status(alias) == STATUS_OK
        other.append(get32(alias, 16))
        assert gauge("fs-descriptors") == before
        # Same-owner adoption replaces its token; no additional owner slot.
        own = call(7, FS_ADOPT, offset=tokens[1])
        assert status(own) == STATUS_OK
        handles.append(get32(own, 16))
        for handle in handles:
            assert status(call(7, FS_CLOSE, handle=handle)) == STATUS_OK
        for handle in other:
            assert status(call(8, FS_CLOSE, handle=handle)) == STATUS_OK
        assert gauge("fs-descriptors") == 0 and gauge("fs-objects") == 1
        directories = [get32(call(7, FS_OPEN, path="/",
            flags=OPEN_READ | OPEN_DIRECTORY), 16) for _ in range(2)]
        assert all(directories) and gauge("fs-directories") == 2
        overflow = call(7, FS_OPEN, path="/", flags=OPEN_READ | OPEN_DIRECTORY)
        assert status(overflow) == STATUS_LIMIT, status(overflow)
        for handle in directories:
            assert status(call(7, FS_READDIR, handle=handle,
                capacity=512, value=8)) == STATUS_OK
            assert status(call(7, FS_CLOSE, handle=handle)) == STATUS_OK
        assert gauge("fs-directories") == 0
        assert status(direct(FS_OPEN, path="/missing", flags=OPEN_READ)) == STATUS_NOT_FOUND
        assert gauge("fs-descriptors") == 0 and gauge("fs-objects") == 1
        # Provider opens have no owner quota; global descriptor/object cap is 12.
        held = []
        for _ in range(11):
            answer = direct(FS_OPEN, path="/quota-file", flags=OPEN_READ)
            assert status(answer) == STATUS_OK
            held.append(get32(answer.raw, 16))
        assert gauge("fs-descriptors") == 11 and gauge("fs-objects") == 12
        assert status(direct(FS_SHARE, handle=held[0])) == STATUS_LIMIT
        assert status(direct(FS_OPEN, path="/quota-file",
            flags=OPEN_WRITE | OPEN_TRUNCATE)) == STATUS_LIMIT
        assert open(fixture, "rb").read() == b"unchanged"
        qmp.execute("qom-set", {"path": "/machine",
            "property": "astra-host-fs-reset", "value": True})
        assert gauge("fs-descriptors") == 0
        assert gauge("fs-objects") == 11
        assert claim(qtest, generation) == STATUS_OK
        session = open_session(qtest, generation)
        assert gauge("fs-objects") == 12
        assert status(direct(FS_OPEN, path="/quota-file", flags=OPEN_READ)) == STATUS_LIMIT
        assert status(direct(FS_CLOSE, handle=held.pop())) == STATUS_OK
        fresh = direct(FS_OPEN, path="/quota-file", flags=OPEN_READ)
        assert status(fresh) == STATUS_OK
        assert status(direct(FS_CLOSE, handle=get32(fresh.raw, 16))) == STATUS_OK
        for handle in held:
            assert status(direct(FS_CLOSE, handle=handle)) == STATUS_OK
        for slot, owner in ((7, CLIENT), (8, CLIENT + 1)):
            assert configure_scope(qtest, generation, CONFIG_CLOSE, slot,
                21 + slot, owner=owner) == 0
        assert status(direct(FS_SESSION_CLOSE)) == STATUS_OK
        assert gauge("fs-descriptors") == gauge("fs-directories") == gauge("fs-objects") == 0
        print("PASS budgets scope=4 descriptors=12 directories=2 rollback aliases lost-ids")
    finally:
        qmp.close()


def system_volume(qtest):
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    session = open_session(qtest, generation, volume="/dh0", read_only=True)
    opened = execute(qtest, [Command(generation, FS_OPEN, "/startup/system",
        session=session, flags=OPEN_READ)])[0]
    assert status(opened) == STATUS_OK
    handle = get32(opened.raw, 16)
    read = execute(qtest, [Command(generation, FS_READ, handle=handle,
        session=session, capacity=64)])[0]
    assert status(read) == STATUS_OK and output(qtest, read, get32(read.raw, 52)) == b"bootstrap\n"
    refused = execute(qtest, [Command(generation, FS_OPEN, "/startup/system",
        session=session, flags=OPEN_WRITE | OPEN_TRUNCATE)])[0]
    assert status(refused) == STATUS_READ_ONLY
    assert status(execute(qtest, [Command(generation, FS_CLOSE,
        session=session, handle=handle)])[0]) == STATUS_OK
    assert status(execute(qtest, [Command(generation, FS_SESSION_CLOSE,
        session=session)])[0]) == STATUS_OK
    assert claim(qtest, generation, release=True) == STATUS_OK
    assert claim(qtest, generation, owner=PROVIDER + 1) == STATUS_OK
    assert claim(qtest, generation, owner=PROVIDER + 1, release=True) == STATUS_OK
    print("PASS system readonly bootstrap-read claim-handover")


def measure_objects(qtest, root, qmp_path, destination):
    """Measure provider-only growth in a preallocated, stopped qtest guest."""
    QmpClient = runpy.run_path(os.path.join(os.path.dirname(__file__),
        "astra-input-hotplug.py"))["QmpClient"]
    qtest.detect_endian()
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    session = open_session(qtest, generation)
    qmp = QmpClient(qmp_path)
    names = ("fs-descriptors", "fs-directories", "fs-objects", "rss-kib", "fds")

    def sample():
        return {name: qmp.execute("qom-get", {"path": "/machine",
            "property": "astra-host-" + name}) for name in names}

    def call(operation, **kwargs):
        answer = execute(qtest, [Command(generation, operation,
            session=session, **kwargs)])[0]
        assert status(answer) == STATUS_OK, (operation, status(answer))
        return answer

    record = {"host": os.uname().machine, "holds": {}}
    try:
        for kind, count, path, flags in (
                ("file", 1000, "/cost/file", OPEN_READ),
                ("dir", 200, "/cost", OPEN_READ | OPEN_DIRECTORY)):
            before = sample()
            held = []
            for _ in range(count):
                handle = get32(call(FS_OPEN, path=path, flags=flags).raw, 16)
                held.append(handle)
                if kind == "dir":
                    call(FS_READDIR, handle=handle, capacity=8192, value=128)
            during = sample()
            assert during["fs-descriptors"] - before["fs-descriptors"] == count
            assert during["fds"] - before["fds"] == count
            for handle in held:
                call(FS_CLOSE, handle=handle)
            after = sample()
            assert after["fs-descriptors"] == before["fs-descriptors"]
            assert after["fs-directories"] == before["fs-directories"]
            assert after["fds"] == before["fds"]
            record["holds"][kind] = dict(count=count, before=before,
                during=during, after=after,
                rss_kib_per_object=max(0, during["rss-kib"] - before["rss-kib"]) / count)
            with open(destination, "w") as stream:
                json.dump(record, stream, indent=2, sort_keys=True)
        call(FS_SESSION_CLOSE)
        assert sample()["fs-descriptors"] == 0
        print("PASS provider-only memory " + json.dumps(record, sort_keys=True))
    finally:
        qmp.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="Astra QEMU system emulator")
    parser.add_argument("provider", help="matching confined storage launcher")
    parser.add_argument("--uid", type=int, required=True)
    parser.add_argument("--gid", type=int, required=True)
    parser.add_argument("--budget-out", help="record provider-only memory on this host")
    args = parser.parse_args()
    # Every protocol oracle runs through the real privilege-separated provider.
    provider_main = runpy.run_path(os.path.join(os.path.dirname(__file__),
        "test-storage-provider.py"))["main"]
    for scenario in ("full", "serial", "budgets"):
        provider_main([args.qemu, args.provider, "--uid", str(args.uid),
            "--gid", str(args.gid), "--scenario", scenario])
    if args.budget_out:
        provider_main([args.qemu, args.provider, "--uid", str(args.uid),
            "--gid", str(args.gid), "--scenario", "memory",
            "--budget-out", args.budget_out])
    print("ASTRA QEMU HOSTFS PASS")

if __name__ == "__main__":
    main()
