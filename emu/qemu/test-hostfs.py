#!/usr/bin/env python3
"""Exercise the Astra generic host filesystem transport at its trust boundary."""

import argparse
import glob
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
# Bound channels live apart from BUFFER, which every execute() rewrites.
SCOPED_BUFFER = 0x02050000
# A claim's channel, and the release-race channel.
CLAIM_BUFFER = 0x02060000
RACE_BUFFER = 0x02070000
SUBMISSION = 0x0200F000
CHANNEL_CONFIG = 0x0200F040
CHANNEL_APERTURE = 0xFFD00000
COMMAND_SIZE = 512
COMMAND_VERSION = 3
VOLUME_WORK = 0
VOLUME_SOUND = 1
VOLUME_COUNT = 2
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
UNLINK_DIRECTORY = 1
SESSION_READ_ONLY = 1
CLAIM_RELEASE = 1
SERVICE_METRICS = 2
METRICS_SNAPSHOT = 1
SERVICE_ENTROPY = 4
ENTROPY_FILL = 1
ENTROPY_MAX = 256
METRICS_CAPACITY = 16 + 8 * (12 + 2 * 20)
METRIC_HOST_COMMANDS = 8

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
STATUS_NOT_DIR = 4
STATUS_LOOP = 18
STATUS_READ_ONLY = 19
KIND_SYMLINK = 3
SYSCALL_INVALID_ARGUMENT = 2
CONFIG_VERSION = 3
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
    """One record-3 command and its operands; place() lays them out."""

    def __init__(self, generation, operation, path=b"", path2=b"", flags=0,
                 handle=0, value=0, offset=0, data=b"", capacity=0,
                 service=1, session=0):
        self.raw = bytearray(COMMAND_SIZE)
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


claim_generations = [100]


def claim(qtest, generation, owner=PROVIDER, release=False, provider=True):
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
    answer = status(qtest.read(CLAIM_BUFFER + CHANNEL_HEADER_SIZE,
                               COMMAND_SIZE))
    assert configure_channel(qtest, generation, CONFIG_CLOSE, slot=slot,
                             owner=owner,
                             channel_generation=channel_generation) == 0
    return answer


def executed_claim(qtest, generation, owner=PROVIDER):
    """CLAIM through HOST_EXECUTE, which the kernel never stamps."""
    return status(execute(qtest, [Command(generation, FS_CLAIM)],
                          owner=owner)[0])


def open_session(qtest, generation, volume=0, root="/", read_only=False,
                 parent=0, owner=PROVIDER):
    answer = execute(qtest, [Command(
        generation, FS_SESSION_OPEN, path=root,
        value=volume if parent == 0 else 0,
        flags=SESSION_READ_ONLY if read_only else 0, session=parent)],
        owner=owner)[0]
    assert status(answer) == STATUS_OK, status(answer)
    return get32(answer.raw, 56)


def provider_holds(path):
    """True while a process of ours -- the QEMU under test -- holds `path`."""
    target = os.path.realpath(path)
    for fd in glob.glob("/proc/[0-9]*/fd/*"):
        try:
            if os.readlink(fd) == target:
                return True
        except OSError:
            pass
    return False


def configure_channel(qtest, generation, operation, slot=3, owner=PROVIDER,
                      channel_generation=7, buffer=BUFFER, byte_size=4096,
                      capacity=1, scope_flags=0):
    config = bytearray(64)
    physical = buffer if operation == 1 else 0
    byte_size = byte_size if operation == 1 else 0
    capacity = capacity if operation == 1 else 0
    struct.pack_into(">IHHIIIIIIIII", config, 0, 64, CONFIG_VERSION,
                     operation, slot,
                     owner, generation, channel_generation, physical,
                     byte_size, capacity, 0,
                     scope_flags if operation == 1 else 0)
    qtest.write(CHANNEL_CONFIG, config)
    qtest.write32(HACC_CHANNEL_CONFIG, CHANNEL_CONFIG)
    return qtest.read32(HACC_CHANNEL_RESULT)


def configure_scope(qtest, generation, operation, slot, channel_generation,
                    scope=0, scope_flags=0, scope_owner=0, owner=CLIENT,
                    buffer=SCOPED_BUFFER, byte_size=8192 + OPERAND_ROOM,
                    capacity=2):
    """The kernel's channel configuration, version 2, with scope words. The
    channel belongs to a client; the scope's creator is the provider."""
    config = bytearray(64)
    opening = operation == CONFIG_OPEN
    struct.pack_into(">IHHIIIIIIIIII", config, 0, 64, CONFIG_VERSION,
                     operation, slot, owner, generation, channel_generation,
                     buffer if opening else 0, byte_size if opening else 0,
                     capacity if opening else 0, scope, scope_flags,
                     scope_owner)
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
    sound = open_session(qtest, generation, volume=VOLUME_SOUND)
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
    assert status(detached) == STATUS_ACCESS

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
        STATUS_BAD_HANDLE

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
    assert status(orphan) == STATUS_ACCESS
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
        assert status(stale) == STATUS_BAD_HANDLE, (session, status(stale))
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
    open_session(qtest, generation, volume=VOLUME_SOUND)
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
        assert status(scoped_wait(qtest, 2, second)) == STATUS_ACCESS
    finally:
        os.close(writer)
    assert not os.path.exists(os.path.join(root, "work", "after-revoke"))
    later = scoped(qtest, 9, 3, Command(generation, FS_STAT, "/",
                                        session=work))
    assert status(later) == STATUS_ACCESS
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
        commands = [Command(generation, FS_SESSION_OPEN, "/", value=0)
                    for _ in range(capacity)]
        commands[capacity // 2] = Command(generation, FS_CLAIM,
                                          flags=CLAIM_RELEASE)
        channel_write(qtest, RACE_BUFFER, capacity, commands,
                      first=round_index * capacity)
        producer = (round_index + 1) * capacity
        qtest.write32(RACE_BUFFER + 36, producer)
        qtest.write32(CHANNEL_APERTURE + slot * 4096 + 0x20, producer)
        qtest.wait32(RACE_BUFFER + 48, producer)
        sessions = []
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
    # The race must actually have been run: both outcomes seen (Task 4
    # re-review N3), or a serialised batch would pass inertly.
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
    """One host root, a directory per volume: a root session opens one, a
    path in one never reaches another, and a volume the machine does not
    have is refused."""
    generation = qtest.read32(HACC_GENERATION)
    assert claim(qtest, generation) == STATUS_OK
    work = open_session(qtest, generation)
    sound = open_session(qtest, generation, volume=VOLUME_SOUND)
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
    unknown = execute(qtest, [Command(
        generation, FS_SESSION_OPEN, "/", value=VOLUME_COUNT)])[0]
    assert status(unknown) == STATUS_INVALID
    stale = Command(generation, FS_STAT, "/fonts", session=sound)
    struct.pack_into(">H", stale.raw, 4, COMMAND_VERSION - 1)
    assert status(execute(qtest, [stale])[0]) == STATUS_INVALID


def claims_and_sessions(qtest, root):
    """Record 3: one claimed owner, rooted sessions, read-only sessions,
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
    st_dev = os.stat(os.path.join(root, "sub", "inside")).st_dev
    assert get64(inside.raw, NODE_DEVICE) == \
        (os.major(st_dev) << 32) | os.minor(st_dev)
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
    assert output(qtest, entry, get32(entry.raw, 52)) == b"inside"
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
    for path, expected in (("/up/loose", STATUS_LOOP),
                           ("/up/sub/inside", STATUS_LOOP),
                           ("/abs/passwd", STATUS_LOOP),
                           ("/../loose", STATUS_INVALID),
                           ("/inside/../../loose", STATUS_INVALID)):
        escaped = execute(qtest, [Command(
            generation, FS_OPEN, path, flags=OPEN_READ, session=sub)])[0]
        assert status(escaped) == expected, (path, status(escaped))
    for path in ("../loose", "/loose", "up/loose"):
        escaped = execute(qtest, [Command(
            generation, FS_OPEN, path, handle=other, flags=OPEN_READ,
            session=sub)])[0]
        assert status(escaped) in (STATUS_INVALID, STATUS_LOOP), \
            (path, status(escaped))
    listed_up = execute(qtest, [Command(
        generation, FS_READDIR, "/up", capacity=NAME_CAPACITY,
        session=sub)])[0]
    # A final symlink opened as a directory: Linux answers ENOTDIR or ELOOP;
    # either way nothing is listed.
    assert status(listed_up) in (STATUS_LOOP, STATUS_NOT_DIR), \
        status(listed_up)
    stolen = execute(qtest, [Command(
        generation, FS_RENAME, "/inside", "/up/stolen", session=sub)])[0]
    assert status(stolen) == STATUS_LOOP, status(stolen)
    assert not os.path.exists(os.path.join(root, "stolen"))
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
    assert qtest.read32(HACC_VERSION) == 0x0001000E
    assert qtest.read32(HACC_CAPS) == 511 | CAP_FS_SCOPE | CAP_FS_SESSIONS
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
    assert version == 3
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

    malformed = Command(generation, FS_READ, handle=handle, session=work)
    put32(malformed.raw, 48, 1)
    assert status(execute(qtest, [malformed])[0]) == STATUS_INVALID

    made_link = execute(qtest, [Command(
        generation, FS_SYMLINK, "/data", "/data-link", session=work)])[0]
    assert status(made_link) == STATUS_OK
    opened_link = execute(qtest, [Command(
        generation, FS_OPEN, "/data-link", flags=OPEN_READ,
        session=work)])[0]
    assert status(opened_link) == STATUS_LOOP, status(opened_link)
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
        generation, FS_STAT, "/escape/sentinel", session=work)])[0]
    assert status(escaped) == STATUS_LOOP, status(escaped)
    traversed = execute(qtest, [Command(
        generation, FS_STAT, "/../sentinel", session=work)])[0]
    assert status(traversed) == STATUS_INVALID
    for malformed_path in ("//data", "/./data", "/batch-0/../data"):
        malformed_path_result = execute(qtest, [Command(
            generation, FS_STAT, malformed_path, session=work)])[0]
        assert status(malformed_path_result) == STATUS_INVALID, (
            malformed_path, status(malformed_path_result))
    renamed = execute(qtest, [Command(
        generation, FS_RENAME, "/data", "/escape/stolen", session=work)])[0]
    assert status(renamed) == STATUS_LOOP, status(renamed)
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
    first_name = output(qtest, first, get32(first.raw, 52))
    cursor = get64(first.raw, 32)
    assert first_name not in (b".", b"..") and cursor != 0
    second = execute(qtest, [Command(
        generation, FS_READDIR, handle=directory, offset=cursor,
        capacity=NAME_CAPACITY, session=work)])[0]
    assert status(second) == STATUS_OK, status(second)
    second_name = output(qtest, second, get32(second.raw, 52))
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
    # UNLINK's kind flags: DIRECTORY refuses a file, both are invalid.
    assert status(execute(qtest, [Command(
        generation, FS_UNLINK, "/write-only", flags=UNLINK_DIRECTORY,
        session=work)])[0]) != STATUS_OK
    assert os.path.exists(os.path.join(root, "write-only"))
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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="Astra QEMU system emulator")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-hostfs-") as temp:
        root = os.path.join(temp, "root")
        outside = os.path.join(temp, "outside")
        os.mkdir(root)
        os.mkdir(outside)
        with open(os.path.join(outside, "sentinel"), "wb") as output_file:
            output_file.write(b"outside")
        rom = os.path.join(temp, "hostfs-test.rom")
        qtest_path = os.path.join(temp, "qtest.sock")
        with open(rom, "wb") as output_file:
            output_file.write(struct.pack(">II", 0x02001000, 0xFFE00008))
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

        def first(qtest):
            run(qtest, os.path.join(root, "work"), outside)
            # A reset drops every claim and session.
            qtest.write32(HACC_RESET, 1)
            claims_and_sessions(qtest, os.path.join(root, "work"))
            volumes(qtest, root)
            scopes(qtest, root)
            claim_race(qtest)

        booted(command, environment, qtest_path, first)
        # A second machine with one thread-pool worker, so a held command
        # keeps the next one queued behind it.
        root2 = os.path.join(temp, "root2")
        for name in ("", "work", "sound"):
            os.mkdir(os.path.join(root2, name))
        environment["ASTRA_HOSTFS_ROOT"] = root2
        booted(command + ["-object",
                          "main-loop,id=main-loop,thread-pool-max=1"],
               environment, qtest_path,
               lambda qtest: (scope_revocation(qtest, root2),
                              orphan_open(qtest, root2)))
        print("ASTRA QEMU HOSTFS PASS")

if __name__ == "__main__":
    main()
