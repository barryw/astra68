#!/usr/bin/env python3
"""Certify the Astra QEMU keyboard and pointer MMIO contract."""

import argparse
import json
import os
import socket
import struct
import subprocess
import tempfile
import time


VESTA = 0xFFF00000
IRQ_RAW = VESTA + 0x300
INPUT_STATUS = VESTA + 0x70C
INPUT_HEADER = VESTA + 0x710
INPUT_VALUE = VESTA + 0x714
INPUT_SEQUENCE = VESTA + 0x71C
INPUT_GENERATION = VESTA + 0x720
INPUT_CONTROL = VESTA + 0x724

INPUT_VALID = 1 << 8
INPUT_OVERFLOW = 1 << 9
INPUT_POP = 1 << 0
INPUT_ACK_OVERFLOW = 1 << 1
INPUT_IRQ = 1 << 5


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


class AstraInputTest:
    def __init__(self, qtest, qmp):
        self.qtest = qtest
        self.qmp = qmp
        self.swap = False

    def read32(self, address):
        value = int(self.qtest.send(f"readl 0x{address:x}"), 0)
        return int.from_bytes(value.to_bytes(4, "little"), "big") if self.swap else value

    def write32(self, address, value):
        if self.swap:
            value = int.from_bytes(value.to_bytes(4, "big"), "little")
        self.qtest.send(f"writel 0x{address:x} 0x{value:x}")

    def detect_endian(self):
        value = self.read32(VESTA + 0x700)
        if value == 0x54504E49:
            self.swap = True
            value = self.read32(VESTA + 0x700)
        if value != 0x494E5054:
            raise AssertionError(f"input ID mismatch: 0x{value:08x}")

    def inject(self, event):
        self.qmp.execute("input-send-event", {"events": [event]})

    def pop(self):
        event = (
            self.read32(INPUT_HEADER),
            self.read32(INPUT_VALUE),
            self.read32(INPUT_SEQUENCE),
            self.read32(INPUT_GENERATION),
        )
        self.write32(INPUT_CONTROL, INPUT_POP)
        return event

    def run(self):
        self.detect_endian()
        assert self.read32(VESTA + 0x01C) == 0x00068030
        assert self.read32(VESTA + 0x020) == 0x51454D55  # QEMU
        assert self.read32(VESTA + 0x030) == 128 * 1024 * 1024
        assert self.read32(VESTA + 0x704) == 0x00010001
        assert self.read32(VESTA + 0x708) == 0x00000003
        assert self.read32(INPUT_STATUS) == 0

        self.inject({"type": "key", "data": {
            "down": True, "key": {"type": "qcode", "data": "a"}}})
        self.inject({"type": "rel", "data": {"axis": "x", "value": 7}})
        self.inject({"type": "rel", "data": {"axis": "y", "value": -3}})
        self.inject({"type": "btn", "data": {"down": True, "button": "left"}})

        status = self.read32(INPUT_STATUS)
        assert status & INPUT_VALID and status & 0xFF == 4
        assert self.read32(IRQ_RAW) & INPUT_IRQ
        generation = self.read32(INPUT_GENERATION)
        assert generation != 0

        events = [self.pop() for _ in range(4)]
        assert events[0] == (0x01010001, 0x04, 0x00010001, generation)
        assert events[1] == (0x02010000, 7, 0x00020001, generation)
        assert events[2] == (0x02010002, 0xFFFFFFFD, 0x00020002, generation)
        assert events[3] == (0x02030001, 1, 0x00020003, generation)
        assert self.read32(INPUT_STATUS) == 0
        assert not self.read32(IRQ_RAW) & INPUT_IRQ

        key = {"type": "key", "data": {
            "down": True, "key": {"type": "qcode", "data": "b"}}}
        for _ in range(32):
            self.inject(key)
        status = self.read32(INPUT_STATUS)
        assert status & 0xFF == 31
        assert status & INPUT_OVERFLOW
        for _ in range(31):
            self.pop()
        assert self.read32(INPUT_STATUS) == INPUT_OVERFLOW
        self.write32(INPUT_CONTROL, INPUT_ACK_OVERFLOW)
        assert self.read32(INPUT_STATUS) == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", help="Astra QEMU system emulator")
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-input-") as temp:
        rom = os.path.join(temp, "input-test.rom")
        qtest_path = os.path.join(temp, "qtest.sock")
        qmp_path = os.path.join(temp, "qmp.sock")
        with open(rom, "wb") as output:
            output.write(struct.pack(">II", 0x02001000, 0xFFE00008))

        command = [
            args.qemu, "-machine", "astra68,accel=qtest",
            "-bios", rom, "-S", "-display", "none", "-nodefaults",
            "-qtest", f"unix:{qtest_path},server=on,wait=off",
            "-qmp", f"unix:{qmp_path},server=on,wait=off",
        ]
        environment = os.environ.copy()
        private_lib = os.path.realpath(
            os.path.join(os.path.dirname(args.qemu), "..", "lib"))
        if os.path.isdir(private_lib):
            existing = environment.get("LD_LIBRARY_PATH")
            environment["LD_LIBRARY_PATH"] = (
                private_lib if not existing else f"{private_lib}:{existing}"
            )
        process = subprocess.Popen(command, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True,
                                   env=environment)
        qtest = qmp = None
        try:
            qtest = LineSocket(qtest_path)
            qmp = QmpSocket(qmp_path)
            qmp.execute("cont")
            AstraInputTest(qtest, qmp).run()
            print("ASTRA QEMU INPUT PASS")
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
                raise RuntimeError(
                    f"QEMU exited {process.returncode}\n{stdout}{stderr}")


if __name__ == "__main__":
    main()
