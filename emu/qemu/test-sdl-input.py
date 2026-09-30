#!/usr/bin/env python3
"""Upstream SDL2 input demos, driven through QMP and read back from the
render batches the display service sends.

testkeys (default): prints every scancode name through SDL_Log, which the
Astra port sends to the system log, and exits clean.

testmouse (--program testmouse): input events become drawing, so each is
checked in the batches the display helper receives (a stand-in helper that
decodes every render-only batch):
  - a left-button drag draws a red line whose vector is the drag's;
  - Shift held through a left drag draws a red filled rectangle of the
    drag's size, so keyboard events arrive too;
  - one wheel notch draws the green wheel line across the whole window;
  - the window's close gadget quits it clean (SDL_QUIT).
"""

import argparse
import collections
import importlib.util
import mmap
import os
import shutil
import struct
import sys
import tempfile
import threading
import time


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def load(name, file):
    spec = importlib.util.spec_from_file_location(
        name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


terminal_gate = load("astra_terminal_gate", "test-terminal.py")
mailbox_gate = load("astra_display_mailbox_gate", "test-display-mailbox.py")

PRESENT_RENDER_BATCH = 3            # ASTRA_DISPLAY_FRAME_PRESENT_RENDER_BATCH
RENDER_ONLY = 0x00010004            # ASTRA_RENDER_BATCH_VERSION_1_4
ARENA_OFFSET = 0x00800000           # ASTRA_RENDER_BATCH_ARENA_OFFSET
SUBMISSION = 0x00801000 - ARENA_OFFSET
COMMAND_BYTES = 64
OP_FILL, OP_FILL_RECTS, OP_LINE, OP_LINES = 1, 4, 256, 262

# testmouse's colours in the window content's canonical form: RGB565 or
# ARGB8888, whichever the content surface is.
RED = {0xF800, 0xFFFF0000}
WHEEL_GREEN = {0x07F0, 0xFF00FF80}

# A 640x480 window opens centred: frame origin (640, 300); the title bar's
# centre line is 15 pixels down and the close gadget's centre 12 pixels in
# from the right edge (test-desktop-apps.py).
FRAME_X, FRAME_Y, FRAME_W = 640, 300, 640
TITLE_Y, CLOSE_INSET = 15, 12


def be32(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


def point(word):
    x, y = word >> 16, word & 0xFFFF
    return (x - 0x10000 if x & 0x8000 else x,
            y - 0x10000 if y & 0x8000 else y)


class Shapes:
    """Lines and filled rectangles of every render-only batch, by colour."""

    def __init__(self, payload):
        self.payload = payload
        self.lock = threading.Lock()
        self.lines = collections.deque(maxlen=20000)
        self.rects = collections.deque(maxlen=20000)
        self.batches = 0

    def observe(self, operation, frame_bytes):
        if operation != PRESENT_RENDER_BATCH:
            return
        data = self.payload[:frame_bytes]
        if len(data) < 64 or be32(data, 4) != RENDER_ONLY:
            return
        lines, rects = [], []
        for index in range(be32(data, 12)):
            base = SUBMISSION + index * COMMAND_BYTES
            opcode = be32(data, base + 4) >> 16
            color = be32(data, base + 60)
            if opcode == OP_LINE:
                lines.append((point(be32(data, base + 44)),
                              point(be32(data, base + 48)), color))
            elif opcode == OP_FILL:
                size = be32(data, base + 56)
                rects.append((point(be32(data, base + 48)),
                              (size >> 16, size & 0xFFFF), color))
            elif opcode in (OP_LINES, OP_FILL_RECTS):
                records = be32(data, base + 40) - ARENA_OFFSET
                count = be32(data, base + 44)
                own_color = be32(data, base + 48) & 1
                for record in range(records, records + 16 * count, 16):
                    colour = be32(data, record + 8) if own_color else color
                    if opcode == OP_LINES:
                        lines.append((point(be32(data, record)),
                                      point(be32(data, record + 4)), colour))
                    else:
                        size = be32(data, record + 4)
                        rects.append((point(be32(data, record)),
                                      (size >> 16, size & 0xFFFF), colour))
        with self.lock:
            self.lines.extend(lines)
            self.rects.extend(rects)
            self.batches += 1

    def line(self, vector, colors):
        with self.lock:
            return any((q[0] - p[0], q[1] - p[1]) == vector and c in colors
                       for p, q, c in self.lines)

    def rect(self, size, colors):
        with self.lock:
            return any(s == size and c in colors for _, s, c in self.rects)

    def horizontal(self, length, colors):
        with self.lock:
            return any(p[1] == q[1] and abs(q[0] - p[0]) >= length and
                       c in colors for p, q, c in self.lines)


def wait_for(condition, what, seconds=10.0):
    deadline = time.monotonic() + seconds
    while not condition():
        if time.monotonic() >= deadline:
            raise RuntimeError("no %s within %.0fs" % (what, seconds))
        time.sleep(0.05)


def exits_after(machine, sequence):
    exits = []
    for line in machine.trace():
        record = terminal_gate.TRACE_RECORD.match(line)
        if (record is None or int(record.group(1)) <= sequence or
                "process_exit" not in line):
            continue
        fields = line.split()
        exits.append((int(fields[-3], 16), int(fields[-2], 16)))
    return exits


def wait_clean_exit(machine, sequence, program):
    # Other processes exit too (remote desktop retries with no host helper),
    # so the test is a clean exit among them and no faulting exit at all.
    # Reasons 3 and 4: user fault, signal (kernel process.h).
    deadline = time.monotonic() + 20
    while True:
        exits = exits_after(machine, sequence)
        if any(reason in (3, 4) for _, reason in exits):
            raise RuntimeError("a process faulted: %r" % exits)
        if (0, 1) in exits:
            return
        if time.monotonic() >= deadline:
            raise RuntimeError("%s did not exit clean: %r %r" %
                               (program, exits, machine.recent_serial(30)))
        time.sleep(0.1)


def drag(machine, start, end):
    machine.qmp.point(*start)
    machine.qmp.button(True)
    machine.qmp.point(*end)
    machine.qmp.button(False)


def test_testmouse(machine, shapes):
    wait_for(lambda: shapes.batches >= 10, "testmouse frames", 60)
    first = (900, 480)
    drag(machine, first, (first[0] + 100, first[1] + 60))
    wait_for(lambda: shapes.line((100, 60), RED), "red line for the drag")

    second = (1100, 700)
    machine.qmp.send(True, "shift")
    drag(machine, second, (second[0] - 70, second[1] - 40))
    machine.qmp.send(False, "shift")
    wait_for(lambda: shapes.rect((70, 40), RED),
             "red rectangle for the Shift drag")

    machine.qmp.input_events([
        {"type": "btn", "data": {"button": "wheel-up", "down": down}}
        for down in (True, False)])
    wait_for(lambda: shapes.horizontal(600, WHEEL_GREEN),
             "green wheel line")

    sequence = machine.trace_sequence()
    machine.qmp.point(FRAME_X + FRAME_W - CLOSE_INSET, FRAME_Y + TITLE_Y)
    machine.qmp.button(True)
    machine.qmp.button(False)
    wait_clean_exit(machine, sequence, "testmouse")


def test_testkeys(machine):
    wanted = ('Scancode #4, "A"', 'Scancode #41, "Escape"',
              'Scancode #225, "Left Shift"')
    last = "Scancode #511"
    deadline = time.monotonic() + 60
    while True:
        said = "\n".join(machine.said(0)[0])
        ends = [int(record.group(1)) for record in
                map(terminal_gate.TRACE_RECORD.match, machine.trace())
                if record is not None and last in record.string]
        if all(text in said for text in wanted) and ends:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError("testkeys logged no scancode table: %r" %
                               machine.recent_serial(30))
        time.sleep(0.2)
    # Its own exit follows the table's last line.
    wait_clean_exit(machine, ends[0], "testkeys")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--program", default="testkeys",
                        choices=("testkeys", "testmouse"))
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-sdl-input-") as work:
        image = os.path.join(work, "test.img")
        shutil.copyfile(arguments.image, image)
        mailbox = os.path.join(work, "mailbox.bin")
        mailbox_gate.create_mailbox(mailbox)
        payload_file = open(mailbox_gate.payload_path(mailbox), "r+b")
        payload = mmap.mmap(payload_file.fileno(), 0)
        shapes = Shapes(payload)
        stream = open(mailbox, "r+b")
        view = mmap.mmap(stream.fileno(), mailbox_gate.HEADER_BYTES)
        helper = mailbox_gate.Helper(view, observe=shapes.observe)
        helper.thread.start()
        os.environ["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
        os.environ["ASTRA_DISPLAY_PAYLOAD_PATH"] = \
            mailbox_gate.payload_path(mailbox)
        machine = terminal_gate.Machine(arguments.qemu, arguments.rom, image,
                                        work)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting: %r" %
                                   machine.recent_serial(40))
            if arguments.program == "testmouse":
                test_testmouse(machine, shapes)
                print("SDL upstream testmouse QEMU: PASS (drag line, "
                      "Shift-drag rectangle, wheel line, close; %d batches)"
                      % shapes.batches)
            else:
                test_testkeys(machine)
                print("SDL upstream testkeys QEMU: PASS (scancode names "
                      "logged, clean exit)")
        finally:
            machine.close()
            helper.stopping = True
            helper.thread.join(timeout=1)
            helper.release()
            view.close()
            payload.close()


if __name__ == "__main__":
    main()
