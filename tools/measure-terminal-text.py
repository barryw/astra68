#!/usr/bin/env python3
"""Measure a scrolling Terminal command through Astra's QMP counters."""

import argparse
import json
import socket
import statistics
import struct
import time


PROPERTIES = (
    "astra-block-read-requests",
    "astra-block-read-sectors",
    "astra-display-render-batches",
    "astra-display-render-commands",
    "astra-display-fill-commands",
    "astra-display-blit-commands",
    "astra-display-glyph-commands",
    "astra-display-submissions",
    "astra-display-completions",
)
QCODES = {" ": "spc", "-": "minus", ".": "dot", "/": "slash",
          "=": "equal", ",": "comma", ";": "semicolon",
          "'": "apostrophe"}
SHIFTED = {":": "semicolon", "+": "equal", "%": "5", "_": "minus",
           "?": "slash", "\"": "apostrophe", ">": "dot", "<": "comma",
           "$": "4", "(": "9", ")": "0", "*": "8", "!": "1"}
INPUT_STATUS = 0xFFF0070C
INPUT_COUNT_MASK = 0x1F
TRACE_ADDRESS = 0x020C4000
TRACE_SIZE = 0x10000
TRACE_MAGIC = 0x41545243
TRACE_HEADER_SIZE = 32
TRACE_RECORD_SIZE = 32
TRACE_USER_EVENT = 0xE000
CPU_HZ_ADDRESS = 0xFFF00028
CPU_CYCLES_LO_ADDRESS = 0xFFF000EC
CPU_CYCLES_HI_ADDRESS = 0xFFF000F0


class Qmp:
    def __init__(self, path, deadline=20.0):
        self.deadline = deadline
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(deadline)
        self.socket.connect(path)
        self.file = self.socket.makefile("rw")
        self.file.readline()
        self.execute("qmp_capabilities")

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write(json.dumps(request) + "\n")
        self.file.flush()
        while True:
            reply = json.loads(self.file.readline())
            if "event" in reply:
                continue
            if "return" in reply:
                return reply["return"]
            if "error" in reply:
                raise RuntimeError(reply["error"])

    def property(self, name):
        return self.execute("qom-get", {"path": "/machine",
                                        "property": name})

    def word(self, address):
        return self.words(address, 1)[0]

    def words(self, address, count):
        reply = self.execute("human-monitor-command", {
            "command-line": "xp /%dxw 0x%x" % (count, address)})
        values = [int(token, 16) for token in reply.split()
                  if token.startswith("0x") and len(token) == 10]
        if len(values) != count:
            raise RuntimeError("expected %d words at 0x%x, got %d" %
                               (count, address, len(values)))
        return values

    def input_events(self, events):
        deadline = time.monotonic() + self.deadline
        while self.word(INPUT_STATUS) & INPUT_COUNT_MASK:
            if time.monotonic() >= deadline:
                raise RuntimeError("input queue idle timed out")
            time.sleep(0.001)
        self.execute("input-send-event", {"events": events})
        while self.word(INPUT_STATUS) & INPUT_COUNT_MASK:
            if time.monotonic() >= deadline:
                raise RuntimeError("input queue drain timed out")
            time.sleep(0.001)

    def cpu_hz(self):
        frequency = self.word(CPU_HZ_ADDRESS)
        if frequency == 0:
            raise RuntimeError("CPU frequency is zero")
        return frequency

    def cycles(self):
        low = self.word(CPU_CYCLES_LO_ADDRESS)
        high = self.word(CPU_CYCLES_HI_ADDRESS)
        return (high << 32) | low

    def key(self, qcode):
        self.input_events([{
            "type": "key", "data": {"down": down,
            "key": {"type": "qcode", "data": qcode}}}
            for down in (True, False)])

    def chord(self, modifier, qcode):
        self.send(True, modifier)
        self.key(qcode)
        self.send(False, modifier)

    def send(self, down, qcode):
        self.input_events([{
            "type": "key", "data": {"down": down,
            "key": {"type": "qcode", "data": qcode}}}])

    def type_without_enter(self, text):
        for character in text:
            if character in QCODES:
                self.key(QCODES[character])
            elif character in SHIFTED:
                self.chord("shift", SHIFTED[character])
            elif character.isupper() and character.isalpha():
                self.chord("shift", character.lower())
            elif character.isalnum():
                self.key(character)
            else:
                raise RuntimeError("no qcode for %r" % character)

    def double_click(self, x, y):
        self.input_events([
            {"type": "abs", "data": {"axis": "x", "value": x}},
            {"type": "abs", "data": {"axis": "y", "value": y}}])
        for _ in range(2):
            for down in (True, False):
                self.input_events([{
                    "type": "btn", "data": {"button": "left",
                    "down": down}}])
                time.sleep(0.05)

    def drag(self, start_x, start_y, end_x, end_y, steps):
        self.input_events([
            {"type": "abs", "data": {"axis": "x", "value": start_x}},
            {"type": "abs", "data": {"axis": "y", "value": start_y}},
            {"type": "btn", "data": {"button": "left", "down": True}}])
        for step in range(1, steps + 1):
            self.input_events([
                {"type": "abs", "data": {"axis": "x", "value":
                    start_x + (end_x - start_x) * step // steps}},
                {"type": "abs", "data": {"axis": "y", "value":
                    start_y + (end_y - start_y) * step // steps}}])
            time.sleep(0.02)
        self.input_events([{
            "type": "btn", "data": {"button": "left", "down": False}}])

    def counters(self):
        return {name: self.property(name) for name in PROPERTIES}


def settled(qmp, quiet_seconds, deadline):
    glyph_property = "astra-display-glyph-commands"
    last_glyphs = qmp.property(glyph_property)
    changed = time.monotonic()
    while True:
        if time.monotonic() >= deadline:
            raise RuntimeError("display settle timed out")
        while time.monotonic() - changed < quiet_seconds:
            if time.monotonic() >= deadline:
                raise RuntimeError("display settle timed out")
            time.sleep(0.02)
            glyphs = qmp.property(glyph_property)
            if glyphs != last_glyphs:
                changed = time.monotonic()
                last_glyphs = glyphs
        settled_at = time.monotonic()
        counters = qmp.counters()
        following = qmp.counters()
        while following != counters:
            if time.monotonic() >= deadline:
                raise RuntimeError("display settle timed out")
            counters = following
            following = qmp.counters()
        if following[glyph_property] == last_glyphs and \
                display_drained(following):
            return following, settled_at
        last_glyphs = following[glyph_property]
        changed = time.monotonic()


def display_drained(counters):
    return counters["astra-display-submissions"] == \
        counters["astra-display-completions"]


def wait_for_desktop(qmp, deadline):
    while qmp.property("astra-display-glyph-commands") == 0:
        if time.monotonic() >= deadline:
            raise RuntimeError("desktop did not render before deadline")
        time.sleep(0.02)


def profile_request(path, command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(path)
        connection.sendall((command + "\n").encode("ascii"))
        response = connection.recv(4096).decode("ascii").strip()
    if response != "OK":
        raise RuntimeError("profiler rejected %r: %s" % (command, response))


def ready_sequence(blob, message):
    if len(blob) < TRACE_HEADER_SIZE:
        raise RuntimeError("trace ring is truncated")
    magic, _abi, size, capacity = struct.unpack_from(">IHHI", blob, 0)
    if magic != TRACE_MAGIC or size != TRACE_RECORD_SIZE or \
            TRACE_HEADER_SIZE + capacity * size > len(blob):
        raise RuntimeError("trace ring header is invalid")
    highest = 0
    for index in range(capacity):
        at = TRACE_HEADER_SIZE + index * size
        sequence, event = struct.unpack_from(">I8xH", blob, at)
        record_message, = struct.unpack_from(">I", blob, at + 20)
        if event == TRACE_USER_EVENT and record_message == message:
            highest = max(highest, sequence)
    return highest


def trace_ready_sequence(qmp, path, message):
    reply = qmp.execute("human-monitor-command", {"command-line":
        'pmemsave 0x%x %d "%s"' % (TRACE_ADDRESS, TRACE_SIZE, path)})
    if reply and reply.strip():
        raise RuntimeError("trace-ring dump failed: %s" % reply.strip())
    with open(path, "rb") as handle:
        return ready_sequence(handle.read(), message)


def trace_position(qmp):
    header = qmp.words(TRACE_ADDRESS, 5)
    abi = header[1] >> 16
    record_size = header[1] & 0xffff
    if header[0] != TRACE_MAGIC or abi != 1 or \
            record_size != TRACE_RECORD_SIZE or header[2] == 0 or \
            header[4] >= header[2] or header[3] == 0:
        raise RuntimeError("trace ring header is invalid")
    return header[3], header[4], header[2]


def next_trace_sequence(sequence):
    sequence = (sequence + 1) & 0xffffffff
    return sequence or 1


def wait_ready(qmp, _path, message, prior, deadline):
    sequence, index, capacity = prior
    while True:
        time.sleep(0.05)
        current, _write_index, current_capacity = trace_position(qmp)
        if current_capacity != capacity:
            raise RuntimeError("trace ring capacity changed")
        scanned = 0
        while sequence != current and scanned < capacity:
            record = qmp.words(
                TRACE_ADDRESS + TRACE_HEADER_SIZE + index * TRACE_RECORD_SIZE,
                TRACE_RECORD_SIZE // 4)
            if record[0] != sequence:
                raise RuntimeError("trace records were overwritten")
            if record[3] >> 16 == TRACE_USER_EVENT and \
                    record[5] == message:
                return (record[1] << 32) | record[2]
            sequence = next_trace_sequence(sequence)
            index = (index + 1) % capacity
            scanned += 1
        if sequence != current:
            raise RuntimeError("trace records were overwritten")
        if time.monotonic() >= deadline:
            raise RuntimeError("ready event timed out")


def open_terminal(qmp, quiet, trace_path, ready_message,
                  desktop_ready_message, deadline):
    end = time.monotonic() + deadline
    _ = desktop_ready_message
    wait_for_desktop(qmp, end)
    settled(qmp, quiet, end)
    prior_ready = trace_position(qmp) \
        if ready_message is not None else None
    qmp.double_click(70, 90)
    end = time.monotonic() + deadline
    if prior_ready is not None:
        wait_ready(qmp, trace_path, ready_message, prior_ready, end)
    else:
        settled(qmp, quiet, end)


def run(qmp, command, quiet_seconds, deadline_seconds):
    deadline = time.monotonic() + deadline_seconds
    qmp.type_without_enter(command)
    before, _ = settled(qmp, quiet_seconds, deadline)
    started = time.monotonic()
    qmp.key("ret")
    while qmp.property("astra-display-glyph-commands") == \
            before["astra-display-glyph-commands"]:
        if time.monotonic() >= deadline:
            raise RuntimeError("command produced no display output before deadline")
        time.sleep(0.02)
    first_output_ms = round((time.monotonic() - started) * 1000, 3)
    after, settled_at = settled(qmp, quiet_seconds, deadline)
    elapsed = settled_at - started - quiet_seconds
    if not display_drained(after):
        raise RuntimeError("display queue did not drain")
    counters = {name: after[name] - before[name] for name in PROPERTIES}
    counters["first-output-milliseconds"] = first_output_ms
    return elapsed, counters


def run_ready(qmp, command, quiet_seconds, control, label, trace_path,
              ready_message, deadline):
    end = time.monotonic() + deadline
    qmp.type_without_enter(command)
    before, _ = settled(qmp, quiet_seconds, end)
    prior_ready = trace_position(qmp)
    active = False
    if control is not None:
        qmp.execute("stop")
        try:
            profile_request(control, "start " + label)
            active = True
        finally:
            qmp.execute("cont")
    frequency = qmp.cpu_hz()
    started_cycles = qmp.cycles()
    observed_start = time.monotonic()
    try:
        qmp.key("ret")
        ready_cycles = wait_ready(
            qmp, trace_path, ready_message, prior_ready, end)
    finally:
        if control is not None:
            qmp.execute("stop")
            try:
                if active:
                    profile_request(control, "stop")
            finally:
                qmp.execute("cont")
    observed_elapsed = time.monotonic() - observed_start
    elapsed_cycles = (ready_cycles - started_cycles) & 0xffffffffffffffff
    elapsed = elapsed_cycles / frequency
    after, _ = settled(qmp, quiet_seconds, end)
    if not display_drained(after):
        raise RuntimeError("display queue did not drain")
    counters = {name: after[name] - before[name] for name in PROPERTIES}
    counters["first-output-milliseconds"] = round(elapsed * 1000, 3)
    counters["guest-cycles"] = elapsed_cycles
    counters["observation-milliseconds"] = round(observed_elapsed * 1000, 3)
    return elapsed, counters


def enforce_latency(samples, maximum_milliseconds):
    if maximum_milliseconds is None:
        return
    worst = max(sample["milliseconds"] for sample in samples)
    if worst > maximum_milliseconds:
        raise SystemExit("worst launch %.3f ms exceeds %.3f ms" %
                         (worst, maximum_milliseconds))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="*", default=["help"])
    parser.add_argument("--qmp", default="/run/astra/qmp.sock")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--quiet", type=float, default=0.75)
    parser.add_argument("--profile-control")
    parser.add_argument("--profile-label", default="command")
    parser.add_argument("--ready-message", type=lambda value: int(value, 0))
    parser.add_argument("--desktop-ready-message",
                        type=lambda value: int(value, 0))
    parser.add_argument("--trace-ring")
    parser.add_argument("--deadline", type=float, default=20.0)
    parser.add_argument("--cleanup")
    parser.add_argument("--max-batches", type=float)
    parser.add_argument("--max-milliseconds", type=float)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--open-terminal", action="store_true",
                        help="open Terminal from a freshly booted desktop")
    action.add_argument("--dump-trace-ring", metavar="PATH",
                        help="dump Astra's 64 KiB trace ring and exit")
    action.add_argument("--quit", action="store_true",
                        help="stop QEMU cleanly and exit")
    action.add_argument("--double-click", nargs=2, type=int,
                        metavar=("X", "Y"),
                        help="double-click a screen coordinate and exit")
    action.add_argument("--drag", nargs=4, type=int,
                        metavar=("X1", "Y1", "X2", "Y2"),
                        help="drag between screen coordinates and exit")
    parser.add_argument("--drag-steps", type=int, default=20)
    arguments = parser.parse_args()
    command = " ".join(arguments.command)
    if arguments.command and \
            arguments.command[0].rsplit("/", 1)[-1] == "posix":
        raise SystemExit("posix is interactive; use test-terminal.py")
    ready_values = (arguments.ready_message, arguments.trace_ring)
    if any(value is not None for value in ready_values) and \
            not all(value is not None for value in ready_values):
        raise SystemExit("ready measurement requires --ready-message and "
                         "--trace-ring")
    if arguments.profile_control is not None and \
            not all(value is not None for value in ready_values):
        raise SystemExit("profiling requires --profile-control, "
                         "--ready-message, and --trace-ring")
    if arguments.deadline <= 0:
        raise SystemExit("deadline must be positive")
    if arguments.max_milliseconds is not None and \
            arguments.max_milliseconds <= 0:
        raise SystemExit("maximum milliseconds must be positive")
    qmp = Qmp(arguments.qmp, arguments.deadline)
    if arguments.quit:
        qmp.execute("quit")
        return
    if arguments.dump_trace_ring:
        if any(character in arguments.dump_trace_ring
               for character in '\"\r\n'):
            raise SystemExit("trace-ring path contains an unsafe character")
        reply = qmp.execute("human-monitor-command", {"command-line":
            'pmemsave 0x020c4000 65536 "%s"' % arguments.dump_trace_ring})
        if reply and reply.strip():
            raise SystemExit("trace-ring dump failed: %s" % reply.strip())
        return
    if arguments.double_click:
        qmp.double_click(*arguments.double_click)
        return
    if arguments.drag:
        if arguments.drag_steps < 1:
            raise SystemExit("drag steps must be positive")
        qmp.drag(*arguments.drag, arguments.drag_steps)
        return
    if arguments.open_terminal:
        open_terminal(qmp, arguments.quiet, arguments.trace_ring,
                      arguments.ready_message,
                      arguments.desktop_ready_message, arguments.deadline)
    for _ in range(arguments.warmups):
        if arguments.ready_message is not None:
            run_ready(qmp, command, arguments.quiet, None, "warmup",
                      arguments.trace_ring, arguments.ready_message,
                      arguments.deadline)
        else:
            run(qmp, command, arguments.quiet, arguments.deadline)
        if arguments.cleanup:
            run(qmp, arguments.cleanup, arguments.quiet, arguments.deadline)
    samples = []
    for index in range(arguments.runs):
        if arguments.ready_message is not None:
            elapsed, counters = run_ready(
                qmp, command, arguments.quiet, arguments.profile_control,
                "%s-%d" % (arguments.profile_label, index + 1),
                arguments.trace_ring, arguments.ready_message,
                arguments.deadline)
        else:
            elapsed, counters = run(
                qmp, command, arguments.quiet, arguments.deadline)
        sample = {"run": index + 1, "milliseconds": round(elapsed * 1000, 3),
                  **counters}
        samples.append(sample)
        print(json.dumps(sample, sort_keys=True), flush=True)
        if arguments.cleanup:
            run(qmp, arguments.cleanup, arguments.quiet, arguments.deadline)
    batches_median = statistics.median(
        sample["astra-display-render-batches"] for sample in samples)
    print(json.dumps({
        "runs": len(samples),
        "milliseconds_median": round(statistics.median(
            sample["milliseconds"] for sample in samples), 3),
        "glyphs_median": statistics.median(
            sample["astra-display-glyph-commands"] for sample in samples),
        "commands_median": statistics.median(
            sample["astra-display-render-commands"] for sample in samples),
        "batches_median": batches_median,
    }, sort_keys=True))
    enforce_latency(samples, arguments.max_milliseconds)
    if arguments.max_batches is not None and \
            batches_median > arguments.max_batches:
        raise SystemExit("median render batches %.1f exceeds %.1f" %
                         (batches_median, arguments.max_batches))


if __name__ == "__main__":
    main()
