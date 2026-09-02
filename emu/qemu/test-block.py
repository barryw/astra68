#!/usr/bin/env python3
"""Certify the Astra QEMU AstraHost block-service MMIO contract."""

import argparse
import json
import os
import socket
import struct
import subprocess
import tempfile
import time


VESTA = 0xFFF00000
SYS_STATUS = VESTA + 0x010
RAM_SIZE = VESTA + 0x030
IRQ_RAW = VESTA + 0x300
IRQ_ENABLE = VESTA + 0x304
IRQ_SOFT = VESTA + 0x308
IRQ_CONFIG0 = VESTA + 0x380
TIMER0_LOAD = VESTA + 0x400
TIMER0_CONTROL = VESTA + 0x408

SCRATCH = VESTA + 0x018
DISPLAY_REQ_ID = VESTA + 0x1E4
DISPLAY_REQ_OP = VESTA + 0x1E8
DISPLAY_REQ_SOURCE = VESTA + 0x1EC

BLOCK_ID = VESTA + 0x150
BLOCK_VERSION = VESTA + 0x154
BLOCK_CAPS = VESTA + 0x158
BLOCK_STATE = VESTA + 0x15C
BLOCK_MEDIA_GEN = VESTA + 0x160
BLOCK_MEDIA_SIZE_HI = VESTA + 0x164
BLOCK_MEDIA_SIZE_LO = VESTA + 0x168
BLOCK_QUEUE = VESTA + 0x16C
BLOCK_REQ_ID = VESTA + 0x170
BLOCK_REQ_OP = VESTA + 0x174
BLOCK_REQ_LBA_HI = VESTA + 0x178
BLOCK_REQ_LBA_LO = VESTA + 0x17C
BLOCK_REQ_SECTORS = VESTA + 0x180
BLOCK_REQ_BUFFER = VESTA + 0x184
BLOCK_REQ_SUBMIT = VESTA + 0x188
BLOCK_CPL_ID = VESTA + 0x18C
BLOCK_CPL_STATUS = VESTA + 0x190
BLOCK_CPL_DETAIL = VESTA + 0x194
BLOCK_CPL_MEDIA_GEN = VESTA + 0x198
BLOCK_CPL_HOST_GEN = VESTA + 0x19C
BLOCK_CPL_POP = VESTA + 0x1A0
BLOCK_ERROR = VESTA + 0x1A4
BLOCK_HOST_GEN = VESTA + 0x1A8
BLOCK_STATE_ACK = VESTA + 0x1AC
BLOCK_MAX_SECTORS = VESTA + 0x1B0

SYS_ASTRA_HOST = 1 << 5

STATE_LINK_UP = 1 << 0
STATE_MEDIA_PRESENT = 1 << 1
STATE_WRITE_ENABLE = 1 << 2

QUEUE_COMPLETION_VALID = 1 << 20
QUEUE_REQUEST_READY = 1 << 8
QUEUE_DEPTH_SHIFT = 24

OP_READ = 1
OP_WRITE = 2
OP_FLUSH = 3
SUBMIT = 1
POP = 1
STATE_ACK = 1
DEVICE_RESET = 1 << 1

ERROR_BAD_OP = 1 << 0
ERROR_BAD_COUNT = 1 << 1
ERROR_BAD_BUFFER = 1 << 2
ERROR_NO_MEDIA = 1 << 3
ERROR_WRITE_PROTECT = 1 << 4
ERROR_LBA_RANGE = 1 << 5
ERROR_BAD_ID = 1 << 7
ERROR_BAD_FLAGS = 1 << 8

IRQ_STORAGE = 1 << 4

SDRAM_BASE = 0x02000000
SDRAM_SIZE = 32 * 1024 * 1024
SECTOR = 512
IMAGE_SECTORS = 2048
BUFFER = SDRAM_BASE + 0x10000
PANEL = 0xFFF01000
PANEL_ID = PANEL + 0x00
PANEL_RAW_INPUT = PANEL + 0x10
PANEL_LED_DATA = PANEL + 0x18
PANEL_LED_OWNERSHIP = PANEL + 0x1C

ASTRAEA = 0xFFF10000
ASTRAEA_IRQ_ENABLE = ASTRAEA + 0x010
ASTRAEA_SOURCE = ASTRAEA + 0x040
ASTRAEA_COLOR = ASTRAEA + 0x060

VEGA = 0xFFF20000
VEGA_IRQ_ENABLE = VEGA + 0x010
VEGA_FRAMEBUFFER_BASE = VEGA + 0x020


def image_byte(offset):
    return ((offset * 7) + (offset >> 9) + 0x5A) & 0xFF


class LineSocket:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        deadline = time.monotonic() + 5.0
        while True:
            try:
                self.sock.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.01)
        self.file = self.sock.makefile("rwb", buffering=0)

    def send(self, line):
        self.file.write(line.encode("ascii") + b"\n")
        reply = self.file.readline().decode("ascii").strip()
        if not reply.startswith("OK"):
            raise RuntimeError(f"command {line!r} failed: {reply}")
        return reply[2:].strip()

    def close(self):
        self.file.close()
        self.sock.close()


class QmpSocket(LineSocket):
    def __init__(self, path):
        super().__init__(path)
        greeting = json.loads(self.file.readline())
        if "QMP" not in greeting:
            raise RuntimeError(f"invalid QMP greeting: {greeting}")
        self.execute("qmp_capabilities")

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write(json.dumps(request).encode("ascii") + b"\n")
        while True:
            reply = json.loads(self.file.readline())
            if "event" in reply:
                continue
            if "error" in reply:
                raise RuntimeError(f"QMP {command} failed: {reply['error']}")
            return reply.get("return")


class AstraBlockTest:
    def __init__(self, qtest, qmp, image_path, panel_path):
        self.qtest = qtest
        self.qmp = qmp
        self.image_path = image_path
        self.panel_path = panel_path
        self.swap = False
        self.next_id = 1

    def read32(self, address):
        value = int(self.qtest.send(f"readl 0x{address:x}"), 0)
        if self.swap:
            return int.from_bytes(value.to_bytes(4, "little"), "big")
        return value

    def write32(self, address, value):
        if self.swap:
            value = int.from_bytes(value.to_bytes(4, "big"), "little")
        self.qtest.send(f"writel 0x{address:x} 0x{value:x}")

    def read_memory(self, address, size):
        payload = self.qtest.send(f"read 0x{address:x} 0x{size:x}")
        return bytes.fromhex(payload[2:] if payload.startswith("0x") else payload)

    def write_memory(self, address, data):
        self.qtest.send(f"write 0x{address:x} 0x{len(data):x} 0x{data.hex()}")

    def detect_endian(self):
        value = self.read32(BLOCK_ID)
        if value == 0x54534F48:
            self.swap = True
            value = self.read32(BLOCK_ID)
        if value != 0x484F5354:
            raise AssertionError(f"block ID mismatch: 0x{value:08x}")

    def submit(self, operation, lba=0, sectors=0, buffer=BUFFER, flags=0,
               request_id=None):
        if request_id is None:
            request_id = self.next_id
            self.next_id += 1
        self.write32(BLOCK_ERROR, 0xFFFFFFFF)
        self.write32(BLOCK_REQ_ID, request_id)
        self.write32(BLOCK_REQ_OP, (flags << 8) | operation)
        self.write32(BLOCK_REQ_LBA_HI, lba >> 32)
        self.write32(BLOCK_REQ_LBA_LO, lba & 0xFFFFFFFF)
        self.write32(BLOCK_REQ_SECTORS, sectors)
        self.write32(BLOCK_REQ_BUFFER, buffer)
        self.write32(BLOCK_REQ_SUBMIT, SUBMIT)
        return request_id, self.read32(BLOCK_ERROR)

    def await_completion(self):
        deadline = time.monotonic() + 5.0
        while True:
            if self.read32(BLOCK_QUEUE) & QUEUE_COMPLETION_VALID:
                break
            if time.monotonic() >= deadline:
                raise AssertionError("completion never became valid")
            self.qtest.send("clock_step")
            time.sleep(0.001)
        completion = {
            "id": self.read32(BLOCK_CPL_ID),
            "status": self.read32(BLOCK_CPL_STATUS) >> 16,
            "sectors": self.read32(BLOCK_CPL_STATUS) & 0xFFFF,
            "detail": self.read32(BLOCK_CPL_DETAIL),
            "media_generation": self.read32(BLOCK_CPL_MEDIA_GEN),
            "host_generation": self.read32(BLOCK_CPL_HOST_GEN),
        }
        return completion

    def pop(self):
        self.write32(BLOCK_CPL_POP, POP)

    def test_identity(self):
        assert self.read32(RAM_SIZE) == SDRAM_SIZE
        assert self.read32(SYS_STATUS) & SYS_ASTRA_HOST, "SYS_ASTRA_HOST clear"
        assert self.read32(BLOCK_VERSION) == 0x00010001
        assert self.read32(BLOCK_CAPS) == 0x7, "read, write, and flush expected"
        assert self.read32(BLOCK_MAX_SECTORS) == 128
        state = self.read32(BLOCK_STATE)
        assert state == (STATE_LINK_UP | STATE_MEDIA_PRESENT |
                         STATE_WRITE_ENABLE), f"state 0x{state:x}"
        sectors = (self.read32(BLOCK_MEDIA_SIZE_HI) << 32) | \
            self.read32(BLOCK_MEDIA_SIZE_LO)
        assert sectors == IMAGE_SECTORS, sectors
        assert self.read32(BLOCK_MEDIA_GEN) == 1
        assert self.read32(BLOCK_HOST_GEN) != 0
        assert self.read32(BLOCK_QUEUE) >> QUEUE_DEPTH_SHIFT == 4

    def test_front_panel_bridge(self):
        assert self.read32(PANEL_ID) == 0x504E4C30
        assert self.read32(PANEL_RAW_INPUT) == 0x00000200
        self.write32(PANEL_LED_DATA, 0x5)
        with open(self.panel_path, "rb") as panel:
            panel.seek(0x18)
            assert struct.unpack("<I", panel.read(4))[0] == 0x5

    def test_complete_machine_reset(self):
        """Every guest-visible chip returns to its power-on register state."""
        host_generation = self.read32(BLOCK_HOST_GEN)
        dirty_registers = (
            (SCRATCH, 0x12345678),
            (IRQ_ENABLE, 0xFFFFFFFF),
            (IRQ_SOFT, 0xFFFFFFFF),
            (IRQ_CONFIG0, 0x00010007),
            (TIMER0_LOAD, 0x1234),
            (TIMER0_CONTROL, 0x4),
            (DISPLAY_REQ_ID, 0x42),
            (DISPLAY_REQ_OP, 0x99),
            (DISPLAY_REQ_SOURCE, SDRAM_BASE),
            (PANEL_LED_DATA, 0xA5),
            (PANEL_LED_OWNERSHIP, 0xFF),
            (ASTRAEA_IRQ_ENABLE, 0x9),
            (ASTRAEA_SOURCE, 0x11223344),
            (ASTRAEA_COLOR, 0x55667788),
            (VEGA_IRQ_ENABLE, 0x7),
            (VEGA_FRAMEBUFFER_BASE, 0x18000000),
        )
        for address, value in dirty_registers:
            self.write32(address, value)

        self.qmp.execute("system_reset")
        deadline = time.monotonic() + 1.0
        while self.read32(BLOCK_HOST_GEN) == host_generation:
            if time.monotonic() >= deadline:
                raise AssertionError("machine reset did not complete")
            time.sleep(0.001)

        for address, _ in dirty_registers:
            assert self.read32(address) == 0, \
                f"register 0x{address:08x} survived reset"
        assert self.read32(IRQ_RAW) == IRQ_STORAGE

    def test_reset_state_change(self):
        """A reset raises a state change; the storage IRQ holds until acked."""
        assert self.read32(BLOCK_STATE_ACK) == STATE_ACK
        assert self.read32(IRQ_RAW) & IRQ_STORAGE
        self.write32(BLOCK_STATE_ACK, STATE_ACK)
        assert self.read32(BLOCK_STATE_ACK) == 0
        assert not self.read32(IRQ_RAW) & IRQ_STORAGE
        queue = self.read32(BLOCK_QUEUE)
        assert queue & QUEUE_REQUEST_READY
        assert not queue & QUEUE_COMPLETION_VALID

    def test_read(self):
        request_id, error = self.submit(OP_READ, lba=3, sectors=4)
        assert error == 0, f"read rejected 0x{error:x}"
        assert not self.read32(BLOCK_QUEUE) & QUEUE_COMPLETION_VALID, \
            "completion must not be immediate"
        assert not self.read32(IRQ_RAW) & IRQ_STORAGE

        completion = self.await_completion()
        assert self.read32(IRQ_RAW) & IRQ_STORAGE, "completion must raise IRQ"
        assert completion["id"] == request_id
        assert completion["status"] == 0, completion
        assert completion["sectors"] == 4, completion
        assert completion["detail"] == 0
        assert completion["media_generation"] == 1
        assert completion["host_generation"] == self.read32(BLOCK_HOST_GEN)

        got = self.read_memory(BUFFER, 4 * SECTOR)
        want = bytes(image_byte(3 * SECTOR + i) for i in range(4 * SECTOR))
        assert got == want, "read data mismatch"

        self.pop()
        assert not self.read32(BLOCK_QUEUE) & QUEUE_COMPLETION_VALID
        assert not self.read32(IRQ_RAW) & IRQ_STORAGE

    def test_maximum_read(self):
        sectors = self.read32(BLOCK_MAX_SECTORS)
        request_id, error = self.submit(OP_READ, lba=64, sectors=sectors)
        assert error == 0, f"maximum read rejected 0x{error:x}"
        completion = self.await_completion()
        assert completion["id"] == request_id
        assert completion["status"] == 0
        assert completion["sectors"] == sectors
        got = self.read_memory(BUFFER, sectors * SECTOR)
        want = bytes(image_byte(64 * SECTOR + i)
                     for i in range(sectors * SECTOR))
        assert got == want, "maximum read data mismatch"
        self.pop()
        assert not self.read32(BLOCK_QUEUE) & QUEUE_COMPLETION_VALID
        assert not self.read32(IRQ_RAW) & IRQ_STORAGE

    def test_write_and_flush(self):
        payload = bytes((i * 13 + 7) & 0xFF for i in range(2 * SECTOR))
        for name in ("astra-block-write-requests",
                     "astra-block-write-sectors",
                     "astra-block-flush-requests"):
            assert self.qmp.execute("qom-get", {
                "path": "/machine", "property": name,
            }) == 0
        assert self.qmp.execute("qom-get", {
            "path": "/machine",
            "property": "astra-block-durability-transitions",
        }) == 0
        self.qmp.execute("qom-set", {
            "path": "/machine",
            "property": "astra-block-durability-transitions",
            "value": 7,
        })
        assert self.qmp.execute("qom-get", {
            "path": "/machine",
            "property": "astra-block-durability-transitions",
        }) == 7
        self.qmp.execute("qom-set", {
            "path": "/machine",
            "property": "astra-block-durability-transitions",
            "value": 0,
        })
        self.write_memory(BUFFER, payload)

        request_id, error = self.submit(OP_WRITE, lba=10, sectors=2)
        assert error == 0, f"write rejected 0x{error:x}"
        completion = self.await_completion()
        assert completion["id"] == request_id
        assert completion["status"] == 0 and completion["sectors"] == 2
        self.pop()

        _, error = self.submit(OP_FLUSH)
        assert error == 0, f"flush rejected 0x{error:x}"
        completion = self.await_completion()
        assert completion["status"] == 0 and completion["sectors"] == 0
        self.pop()
        assert self.qmp.execute("qom-get", {
            "path": "/machine",
            "property": "astra-block-durability-transitions",
        }) == 2
        assert self.qmp.execute("qom-get", {
            "path": "/machine",
            "property": "astra-block-write-requests",
        }) == 1
        assert self.qmp.execute("qom-get", {
            "path": "/machine",
            "property": "astra-block-write-sectors",
        }) == 2
        assert self.qmp.execute("qom-get", {
            "path": "/machine",
            "property": "astra-block-flush-requests",
        }) == 1

        with open(self.image_path, "rb") as image:
            image.seek(10 * SECTOR)
            assert image.read(2 * SECTOR) == payload, "image content mismatch"

    def test_read_back_written_data(self):
        self.write_memory(BUFFER, b"\x00" * (2 * SECTOR))
        _, error = self.submit(OP_READ, lba=10, sectors=2)
        assert error == 0
        completion = self.await_completion()
        assert completion["status"] == 0
        self.pop()
        got = self.read_memory(BUFFER, 2 * SECTOR)
        want = bytes((i * 13 + 7) & 0xFF for i in range(2 * SECTOR))
        assert got == want, "written data did not read back"

    def test_rejections(self):
        """Every rejection reports its own bit and produces no completion."""
        cases = [
            ("bad op", dict(operation=9, sectors=1), ERROR_BAD_OP),
            ("zero count", dict(operation=OP_READ, sectors=0),
             ERROR_BAD_COUNT),
            ("oversized count", dict(operation=OP_READ, sectors=129),
             ERROR_BAD_COUNT),
            ("flush with sectors", dict(operation=OP_FLUSH, sectors=1),
             ERROR_BAD_COUNT),
            ("unaligned buffer",
             dict(operation=OP_READ, sectors=1, buffer=BUFFER + 1),
             ERROR_BAD_BUFFER),
            ("buffer below SDRAM",
             dict(operation=OP_READ, sectors=1, buffer=0x1000),
             ERROR_BAD_BUFFER),
            ("buffer overruns SDRAM",
             dict(operation=OP_READ, sectors=2,
                  buffer=SDRAM_BASE + SDRAM_SIZE - SECTOR),
             ERROR_BAD_BUFFER),
            ("lba past media",
             dict(operation=OP_READ, lba=IMAGE_SECTORS, sectors=1),
             ERROR_LBA_RANGE),
            ("transfer crosses end",
             dict(operation=OP_READ, lba=IMAGE_SECTORS - 1, sectors=2),
             ERROR_LBA_RANGE),
            ("zero id",
             dict(operation=OP_READ, sectors=1, request_id=0),
             ERROR_BAD_ID),
            ("unknown flags",
             dict(operation=OP_READ, sectors=1, flags=0x80),
             ERROR_BAD_FLAGS),
        ]
        for name, arguments, expected in cases:
            _, error = self.submit(**arguments)
            assert error & expected, f"{name}: error 0x{error:x}"
            self.qtest.send("clock_step")
            assert not self.read32(BLOCK_QUEUE) & QUEUE_COMPLETION_VALID, \
                f"{name} produced a completion"
            assert not self.read32(IRQ_RAW) & IRQ_STORAGE, \
                f"{name} raised an interrupt"

        # BLOCK_ERROR is write-one-to-clear and accumulates until cleared.
        self.write32(BLOCK_ERROR, 0)
        assert self.read32(BLOCK_ERROR) != 0, "a zero write must clear nothing"
        self.write32(BLOCK_ERROR, 0xFFFFFFFF)
        assert self.read32(BLOCK_ERROR) == 0

    def test_queue_full(self):
        """The advertised queue accepts that many independent transfers."""
        requests = {}
        for index in range(4):
            buffer = BUFFER + index * SECTOR
            request_id, error = self.submit(
                OP_READ, lba=index, sectors=1, buffer=buffer)
            assert error == 0, f"request {index} rejected 0x{error:x}"
            requests[request_id] = (index, buffer)

        queue = self.read32(BLOCK_QUEUE)
        assert not queue & QUEUE_REQUEST_READY, "full device claimed ready"
        _, error = self.submit(OP_READ, lba=4, sectors=1,
                               buffer=BUFFER + 4 * SECTOR)
        assert error & (1 << 6), f"expected queue-full, got 0x{error:x}"

        while requests:
            completion = self.await_completion()
            assert completion["status"] == 0, completion
            assert completion["sectors"] == 1, completion
            index, buffer = requests.pop(completion["id"])
            got = self.read_memory(buffer, SECTOR)
            want = bytes(image_byte(index * SECTOR + i)
                         for i in range(SECTOR))
            assert got == want, f"read {index} data mismatch"
            self.pop()
        assert self.read32(BLOCK_QUEUE) & QUEUE_REQUEST_READY

    def test_device_reset_cancels_the_whole_queue(self):
        """Reset atomically revokes every lane and starts a new generation."""
        host_generation = self.read32(BLOCK_HOST_GEN)
        for index in range(4):
            _, error = self.submit(
                OP_READ, lba=index, sectors=1,
                buffer=BUFFER + index * SECTOR)
            assert error == 0, f"request {index} rejected 0x{error:x}"
        assert not self.read32(BLOCK_QUEUE) & QUEUE_REQUEST_READY

        self.write32(BLOCK_STATE_ACK, DEVICE_RESET)
        assert self.read32(BLOCK_HOST_GEN) != host_generation
        queue = self.read32(BLOCK_QUEUE)
        assert queue & QUEUE_REQUEST_READY
        assert not queue & QUEUE_COMPLETION_VALID
        assert queue & 0x1F == 0, f"requests survived reset: 0x{queue:x}"
        assert self.read32(BLOCK_STATE_ACK) == STATE_ACK
        assert self.read32(IRQ_RAW) & IRQ_STORAGE

        for _ in range(3):
            self.qtest.send("clock_step")
            assert not self.read32(BLOCK_QUEUE) & QUEUE_COMPLETION_VALID
        self.write32(BLOCK_STATE_ACK, STATE_ACK)
        assert not self.read32(IRQ_RAW) & IRQ_STORAGE

    def test_flush_is_a_queue_barrier(self):
        """A flush orders writes on both sides of it without draining depth."""
        first = bytes((i * 5 + 1) & 0xFF for i in range(SECTOR))
        second = bytes((i * 11 + 3) & 0xFF for i in range(SECTOR))
        self.write_memory(BUFFER, first)
        self.write_memory(BUFFER + SECTOR, second)

        first_id, error = self.submit(
            OP_WRITE, lba=20, sectors=1, buffer=BUFFER)
        assert error == 0
        flush_id, error = self.submit(OP_FLUSH)
        assert error == 0
        second_id, error = self.submit(
            OP_WRITE, lba=21, sectors=1, buffer=BUFFER + SECTOR)
        assert error == 0

        for expected in (first_id, flush_id, second_id):
            completion = self.await_completion()
            assert completion["id"] == expected, completion
            assert completion["status"] == 0, completion
            self.pop()

        with open(self.image_path, "rb") as image:
            image.seek(20 * SECTOR)
            assert image.read(SECTOR) == first
            assert image.read(SECTOR) == second

    def run(self):
        self.detect_endian()
        self.test_identity()
        self.test_front_panel_bridge()
        self.test_complete_machine_reset()
        self.test_reset_state_change()
        self.test_read()
        self.test_maximum_read()
        self.test_write_and_flush()
        self.test_read_back_written_data()
        self.test_rejections()
        self.test_queue_full()
        self.test_device_reset_cancels_the_whole_queue()
        self.test_flush_is_a_queue_barrier()


def build_image(path):
    with open(path, "wb") as image:
        image.write(bytes(image_byte(i)
                          for i in range(IMAGE_SECTORS * SECTOR)))


def test_power_cut(command, environment, image_path, temp):
    payload = bytes((i * 17 + 3) & 0xFF for i in range(SECTOR))

    for cut_after, expected_op in ((1, "write"), (2, "flush")):
        qtest_path = os.path.join(temp, f"cut-{cut_after}-qtest.sock")
        qmp_path = os.path.join(temp, f"cut-{cut_after}-qmp.sock")
        cut_command = command.copy()
        cut_command[cut_command.index("-qtest") + 1] = \
            f"unix:{qtest_path},server=on,wait=off"
        cut_command[cut_command.index("-qmp") + 1] = \
            f"unix:{qmp_path},server=on,wait=off"
        process = subprocess.Popen(cut_command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True,
                                   env=environment)
        qtest = qmp = None
        try:
            qtest = LineSocket(qtest_path)
            qmp = QmpSocket(qmp_path)
            qmp.execute("cont")
            block = AstraBlockTest(qtest, qmp, image_path, "")
            block.detect_endian()
            qmp.execute("qom-set", {
                "path": "/machine",
                "property": "astra-block-power-cut-after",
                "value": cut_after,
            })
            assert qmp.execute("qom-get", {
                "path": "/machine",
                "property": "astra-block-power-cut-after",
            }) == cut_after
            block.write_memory(BUFFER, payload)
            _, error = block.submit(OP_WRITE, lba=20, sectors=1)
            assert error == 0
            if cut_after == 2:
                assert block.await_completion()["status"] == 0
                block.pop()
                _, error = block.submit(OP_FLUSH)
                assert error == 0
            deadline = time.monotonic() + 5.0
            while process.poll() is None and time.monotonic() < deadline:
                try:
                    qtest.send("clock_step")
                except (BrokenPipeError, ConnectionResetError, RuntimeError):
                    break
                time.sleep(0.001)
            try:
                process.wait(timeout=max(0.0, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                raise AssertionError(
                    f"block cut {cut_after} did not terminate QEMU")
            stderr = process.stderr.read()
            assert process.returncode == 86, process.returncode
            marker = (f"Astra68 block power cut: transition={cut_after} "
                      f"op={expected_op}")
            assert marker in stderr, stderr
        finally:
            if qmp is not None:
                try:
                    qmp.close()
                except (BrokenPipeError, ConnectionResetError):
                    pass
            if qtest is not None:
                try:
                    qtest.close()
                except (BrokenPipeError, ConnectionResetError):
                    pass
            if process.poll() is None:
                process.kill()
                process.wait()
            if process.stderr is not None:
                process.stderr.close()

        with open(image_path, "rb") as image:
            image.seek(20 * SECTOR)
            assert image.read(SECTOR) == payload

    invalid_environment = environment.copy()
    invalid_environment["ASTRA_QEMU_BLOCK_CUT_AFTER"] = "invalid"
    invalid_command = command.copy()
    invalid_command[invalid_command.index("-qtest") + 1] = \
        f"unix:{os.path.join(temp, 'invalid-qtest.sock')},server=on,wait=off"
    invalid_command[invalid_command.index("-qmp") + 1] = \
        f"unix:{os.path.join(temp, 'invalid-qmp.sock')},server=on,wait=off"
    invalid = subprocess.run(invalid_command, stdout=subprocess.DEVNULL,
                             stderr=subprocess.PIPE, text=True,
                             env=invalid_environment, timeout=5)
    assert invalid.returncode != 0
    assert "invalid ASTRA_QEMU_BLOCK_CUT_AFTER 'invalid'" in invalid.stderr


def main():
    global SDRAM_SIZE

    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="Astra QEMU system emulator")
    parser.add_argument("--memory-mib", type=int, choices=(32, 128),
                        default=32)
    args = parser.parse_args()
    SDRAM_SIZE = args.memory_mib * 1024 * 1024

    with tempfile.TemporaryDirectory(prefix="astra-block-") as temp:
        rom = os.path.join(temp, "block-test.rom")
        image = os.path.join(temp, "storage.img")
        qtest_path = os.path.join(temp, "qtest.sock")
        qmp_path = os.path.join(temp, "qmp.sock")
        panel_path = os.path.join(temp, "front-panel.bin")
        with open(rom, "wb") as output:
            output.write(struct.pack(">II", 0x02001000, 0xFFE00008))
        build_image(image)
        with open(panel_path, "wb") as output:
            output.truncate(4096)
            output.seek(0x00)
            output.write(struct.pack("<I", 0x504E4C30))
            output.seek(0x10)
            output.write(struct.pack("<I", 0x00000200))

        command = [
            args.qemu, "-machine", "astra68,accel=qtest", "-m",
            f"{args.memory_mib}M",
            "-bios", rom, "-S", "-display", "none", "-nodefaults",
            "-drive", f"if=none,format=raw,file={image}",
            "-qtest", f"unix:{qtest_path},server=on,wait=off",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
        ]
        environment = os.environ.copy()
        environment["ASTRA_FRONT_PANEL_MMIO_PATH"] = panel_path
        environment["ASTRA_FRONT_PANEL_MMIO_OFFSET"] = "0"
        private_lib = os.path.realpath(
            os.path.join(os.path.dirname(args.qemu), "..", "lib"))
        if os.path.isdir(private_lib):
            existing = environment.get("LD_LIBRARY_PATH")
            environment["LD_LIBRARY_PATH"] = (
                private_lib if not existing else f"{private_lib}:{existing}"
            )
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL,
                                   env=environment)
        qtest = qmp = None
        try:
            qtest = LineSocket(qtest_path)
            qmp = QmpSocket(qmp_path)
            qmp.execute("cont")
            AstraBlockTest(qtest, qmp, image, panel_path).run()
            with open(panel_path, "rb") as panel:
                panel.seek(0x2C)
                activity = struct.unpack("<I", panel.read(4))[0]
            assert activity == 1, "block requests did not trigger HDD LED"
            test_power_cut(command, environment, image, temp)
            print("ASTRA QEMU BLOCK PASS")
        finally:
            if qmp is not None:
                try:
                    qmp.execute("quit")
                except (BrokenPipeError, EOFError):
                    pass
                qmp.close()
            if qtest is not None:
                qtest.close()
            try:
                stdout, stderr = process.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                stdout, stderr = process.communicate()
            if process.returncode not in (0, None):
                print(stdout)
                print(stderr)
                raise SystemExit(f"qemu exited {process.returncode}")


if __name__ == "__main__":
    main()
