#!/usr/bin/env python3
"""Upstream SDL2 timer and thread tests, read back from the system log.

Both programs report through SDL_Log, which the Astra port sends to the
system log, and exit by themselves.

testtimer (default): SDL_GetTicks against SDL_Delay, a 1 ms periodic timer
for 10 s, three concurrent timers (100, 50 and 233 ms; the first removed
after 10 s, the other two after 15 s) and a 1 s delay measured three ways.
Each timer must fire about as often as its period says, and never after it
was removed.

testthread (--program testthread): thread-local storage seen differently by
two threads, a thread joined on a flag, then a SIGTERM the program raises at
itself whose handler sleeps, stops the second thread and joins it.
"""

import argparse
import collections
import importlib.util
import os
import re
import shutil
import sys
import tempfile
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


class Log:
    """Every line the machine logs, kept across polls.

    The trace is a ring, and testtimer's callbacks log several hundred lines
    in 15 s, so a single read at the end would find only the last of them.
    Records are kept by sequence and joined when read, which also mends a
    line whose continuation had not been written at the previous poll."""

    def __init__(self, machine, strict=True):
        # strict: a ring that wrapped between polls fails the gate; a gate
        # that only needs the latest story records the loss instead.
        self.strict = strict
        self.wrapped = 0
        self.machine = machine
        self.records = {}
        self.exits = []
        self.seen_exits = set()
        # Newest record of any kind seen: the ring also carries kernel
        # events, so text records alone cannot say whether it wrapped.
        self.newest = 0

    def poll(self):
        highest = self.newest
        oldest = None
        for line in self.machine.trace():
            record = terminal_gate.TRACE_RECORD.match(line)
            if record is None:
                continue
            sequence = int(record.group(1))
            oldest = sequence if oldest is None else min(oldest, sequence)
            self.newest = max(self.newest, sequence)
            if "process_exit" in line and sequence not in self.seen_exits:
                self.seen_exits.add(sequence)
                fields = line.split()
                self.exits.append((sequence, int(fields[-3], 16),
                                   int(fields[-2], 16)))
            text = terminal_gate.RECORD.match(line)
            if text is not None:
                self.records[sequence] = text.group(2)
        if highest and oldest is not None and oldest > highest + 1:
            if not self.strict:
                self.wrapped += 1
                return
            raise RuntimeError("the trace ring wrapped between polls "
                               "(%d to %d)" % (highest, oldest))

    def lines(self, after=0):
        lines, pending = [], ""
        for sequence in sorted(self.records):
            if sequence <= after:
                continue
            body = self.records[sequence]
            continued = body.endswith("\\")
            pending += body[:-1] if continued else body
            if not continued:
                lines.append((sequence, pending))
                pending = ""
        if pending:
            lines.append((sequence, pending))
        return lines

    def find(self, pattern):
        compiled = re.compile(pattern)
        return [(sequence, compiled.search(line))
                for sequence, line in self.lines()
                if compiled.search(line) is not None]


def run_until(log, program, done, seconds):
    """Polls until @p done(log) holds and @p program has exited clean."""
    deadline = time.monotonic() + seconds
    while True:
        log.poll()
        if any(reason in (3, 4) for _, _, reason in log.exits):
            raise RuntimeError("a process faulted: %r" % log.exits)
        errors = log.find(r"ERROR: (.*)")
        if errors:
            raise RuntimeError("%s logged an error: %s" %
                               (program, errors[0][1].group(1)))
        if done(log):
            return
        if time.monotonic() >= deadline:
            raise RuntimeError("%s did not finish: %r" %
                               (program, [line for _, line in
                                          log.lines()][-30:]))
        time.sleep(0.25)


def exited_clean_after(log, sequence):
    # Other processes exit too (remote desktop retries with no host helper);
    # this program's exit is the clean one after its last line.
    return any(at > sequence and (status, reason) == (0, 1)
               for at, status, reason in log.exits)


def first(log, pattern):
    found = log.find(pattern)
    return found[0] if found else (None, None)


def within(value, low, high, what):
    if not low <= value <= high:
        raise RuntimeError("%s is %s, expected %s to %s" %
                           (what, value, low, high))


def test_testtimer(log):
    last = r"Delay 1 second = (\d+) ms in ticks, (\d+) ms in ticks64, " \
           r"([0-9.]+) ms according to performance counter"
    run_until(log, "testtimer",
              lambda log: (first(log, last)[0] is not None and
                           exited_clean_after(log, first(log, last)[0])),
              150)

    resolution = first(log, r"Timer resolution: desired = 1 ms, "
                            r"actual = ([0-9.]+) ms")[1]
    if resolution is None:
        raise RuntimeError("the 1 ms timer never fired")
    # SDL_Delay(1) and the timer thread's wait both sleep to the next
    # kernel deadline, not to a tick.
    within(float(resolution.group(1)), 0.9, 1.5, "1 ms timer period")

    removed = first(log, r"Removing timer 1")[0]
    stopped = first(log, r"1 million iterations of ticktock")[0]
    if removed is None or stopped is None:
        raise RuntimeError("testtimer skipped its multiple-timer phase")
    fired = collections.Counter()
    for sequence, match in log.find(r"Timer (\d+) : param = (\d+)"):
        interval, param = int(match.group(1)), int(match.group(2))
        if (interval, param) not in ((100, 1), (50, 2), (233, 3)):
            raise RuntimeError("timer callback with interval %d param %d"
                               % (interval, param))
        if sequence > stopped or (param == 1 and sequence > removed):
            raise RuntimeError("timer %d fired after it was removed" % param)
        fired[param] += 1
    # 10 s at 100 ms, 15 s at 50 ms and 15 s at 233 ms.
    within(fired[1], 95, 101, "timer 1 callbacks")
    within(fired[2], 285, 301, "timer 2 callbacks")
    within(fired[3], 61, 65, "timer 3 callbacks")

    delay = first(log, last)[1]
    within(int(delay.group(1)), 1000, 1050, "1 s delay in ticks")
    within(int(delay.group(2)), 1000, 1050, "1 s delay in ticks64")
    within(float(delay.group(3)), 1000.0, 1050.0,
           "1 s delay by performance counter")
    return "1 ms timer %s ms; callbacks %d/%d/%d; 1 s delay %s ms" % (
        resolution.group(1), fired[1], fired[2], fired[3], delay.group(1))


def test_testthread(log):
    finished = r"Thread '#2' exiting!"
    run_until(log, "testthread",
              lambda log: (first(log, finished)[0] is not None and
                           exited_clean_after(log, first(log, finished)[0])),
              90)
    # The program raises SIGTERM right after creating thread #2, so the
    # handler's line and #2's first line may come in either order.
    order = [
        (r"Main thread data initially: main thread",),
        (r"Started thread #1: My thread id is \d+, thread data = baby thread",),
        (r"Thread '#1' is alive!",),
        (r"Waiting for thread #1",),
        (r"Thread '#1' exiting!",),
        (r"Main thread data finally: main thread",),
        (r"Started thread #2: My thread id is \d+, thread data = baby thread",
         r"Killed with SIGTERM, waiting 5 seconds to exit"),
        (finished,),
    ]
    at = 0
    for patterns in order:
        reached = at
        for pattern in patterns:
            sequence = next((s for s, _ in log.find(pattern) if s > at), None)
            if sequence is None:
                raise RuntimeError("testthread never logged %r after "
                                   "sequence %d: %r" %
                                   (pattern, at, log.lines(at)[-30:]))
            reached = max(reached, sequence)
        at = reached
    # Each thread wakes once a second: five times for #1 before the main
    # thread's 5 s delay ends, and about five for #2 during the handler's.
    one = len(log.find(r"Thread '#1' is alive!"))
    two = len(log.find(r"Thread '#2' is alive!"))
    within(one, 5, 6, "thread #1 wake-ups")
    within(two, 5, 6, "thread #2 wake-ups")
    ids = [int(match.group(1)) for _, match in
           log.find(r"Started thread #\d: My thread id is (\d+)")]
    return "TLS per thread, %d and %d wake-ups, SIGTERM handler joined " \
           "thread (ids %s)" % (one, two, ids)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--program", default="testtimer",
                        choices=("testtimer", "testthread"))
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-sdl-runtime-") as work:
        image = os.path.join(work, "test.img")
        shutil.copyfile(arguments.image, image)
        machine = terminal_gate.Machine(arguments.qemu, arguments.rom, image,
                                        work)
        log = Log(machine)
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not finish booting: %r" %
                                   machine.recent_serial(40))
            if arguments.program == "testtimer":
                result = test_testtimer(log)
            else:
                result = test_testthread(log)
            print("SDL upstream %s QEMU: PASS (%s)" %
                  (arguments.program, result))
        finally:
            machine.close()


if __name__ == "__main__":
    main()
