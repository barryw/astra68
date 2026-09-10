#!/usr/bin/env python3
"""Measure display consumption while dragging one Astra window via QMP."""

import argparse
import json
import socket
import time


PROPERTIES = (
    "astra-display-render-batches",
    "astra-display-cursor-updates",
    "astra-display-submissions",
    "astra-display-completions",
)


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
            raise RuntimeError(reply["error"])

    def counters(self):
        return {name: self.execute("qom-get", {
            "path": "/machine", "property": name}) for name in PROPERTIES}

    def move(self, x, y):
        self.execute("input-send-event", {"events": [
            {"type": "abs", "data": {"axis": "x", "value": x}},
            {"type": "abs", "data": {"axis": "y", "value": y}},
        ]})

    def button(self, down):
        self.execute("input-send-event", {"events": [{
            "type": "btn", "data": {"button": "left", "down": down},
        }]})


def difference(after, before):
    return {name: after[name] - before[name] for name in PROPERTIES}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmp", default="/run/astra/qmp.sock")
    parser.add_argument("--start-x", type=int, default=500)
    parser.add_argument("--start-y", type=int, default=105)
    parser.add_argument("--distance", type=int, default=600)
    parser.add_argument("--samples", type=int, default=60)
    parser.add_argument("--hertz", type=float, default=60.0)
    parser.add_argument("--settle", type=float, default=2.0)
    args = parser.parse_args()
    if args.samples < 1 or args.hertz <= 0:
        parser.error("samples and hertz must be positive")

    qmp = Qmp(args.qmp)
    qmp.move(args.start_x, args.start_y)
    time.sleep(0.1)
    before = qmp.counters()
    qmp.button(True)
    started = time.monotonic()
    for sample in range(1, args.samples + 1):
        qmp.move(args.start_x + args.distance * sample // args.samples,
                 args.start_y)
        deadline = started + sample / args.hertz
        delay = deadline - time.monotonic()
        if delay > 0:
            time.sleep(delay)
    sent_at = time.monotonic()
    during = qmp.counters()
    qmp.button(False)
    time.sleep(args.settle)
    settled = qmp.counters()
    print(json.dumps({
        "requested_samples": args.samples,
        "send_milliseconds": round((sent_at - started) * 1000, 3),
        "at_send_end": difference(during, before),
        "after_settle": difference(settled, before),
    }, sort_keys=True))


if __name__ == "__main__":
    main()
