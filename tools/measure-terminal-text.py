#!/usr/bin/env python3
"""Measure a scrolling Terminal command through Astra's QMP counters."""

import argparse
import json
import socket
import statistics
import time


PROPERTIES = (
    "astra-display-render-batches",
    "astra-display-render-commands",
    "astra-display-fill-commands",
    "astra-display-blit-commands",
    "astra-display-glyph-commands",
    "astra-display-submissions",
    "astra-display-completions",
)
QCODES = {" ": "spc", "-": "minus", ".": "dot", "/": "slash"}
SHIFTED = {":": "semicolon"}


class Qmp:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
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

    def key(self, qcode):
        for down in (True, False):
            self.execute("input-send-event", {"events": [{
                "type": "key", "data": {"down": down,
                "key": {"type": "qcode", "data": qcode}}}]})
            time.sleep(0.02)

    def chord(self, modifier, qcode):
        self.send(True, modifier)
        self.key(qcode)
        self.send(False, modifier)

    def send(self, down, qcode):
        self.execute("input-send-event", {"events": [{
            "type": "key", "data": {"down": down,
            "key": {"type": "qcode", "data": qcode}}}]})
        time.sleep(0.02)

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

    def counters(self):
        return {name: self.property(name) for name in PROPERTIES}


def settled(qmp, quiet_seconds):
    glyph_property = "astra-display-glyph-commands"
    last_glyphs = qmp.property(glyph_property)
    changed = time.monotonic()
    while time.monotonic() - changed < quiet_seconds:
        time.sleep(0.02)
        glyphs = qmp.property(glyph_property)
        if glyphs != last_glyphs:
            changed = time.monotonic()
            last_glyphs = glyphs
    return qmp.counters()


def run(qmp, command, quiet_seconds):
    qmp.type_without_enter(command)
    before = settled(qmp, quiet_seconds)
    started = time.monotonic()
    qmp.key("ret")
    deadline = started + 5.0
    while qmp.property("astra-display-glyph-commands") == \
            before["astra-display-glyph-commands"]:
        if time.monotonic() >= deadline:
            raise RuntimeError("command produced no rendered text")
        time.sleep(0.02)
    after = settled(qmp, quiet_seconds)
    elapsed = time.monotonic() - started - quiet_seconds
    if after["astra-display-submissions"] != \
            after["astra-display-completions"]:
        raise RuntimeError("display queue did not drain")
    return elapsed, {name: after[name] - before[name] for name in PROPERTIES}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs="*", default=["help"])
    parser.add_argument("--qmp", default="/data/astra/run/qmp.sock")
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmups", type=int, default=2)
    parser.add_argument("--quiet", type=float, default=0.75)
    parser.add_argument("--max-batches", type=float)
    arguments = parser.parse_args()
    command = " ".join(arguments.command)
    qmp = Qmp(arguments.qmp)
    for _ in range(arguments.warmups):
        run(qmp, command, arguments.quiet)
    samples = []
    for index in range(arguments.runs):
        elapsed, counters = run(qmp, command, arguments.quiet)
        sample = {"run": index + 1, "milliseconds": round(elapsed * 1000, 3),
                  **counters}
        samples.append(sample)
        print(json.dumps(sample, sort_keys=True), flush=True)
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
    if arguments.max_batches is not None and \
            batches_median > arguments.max_batches:
        raise SystemExit("median render batches %.1f exceeds %.1f" %
                         (batches_median, arguments.max_batches))


if __name__ == "__main__":
    main()
