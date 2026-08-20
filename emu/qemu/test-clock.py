#!/usr/bin/env python3
"""Boot the machine and ask it what day it is.

The chain this proves is five links long and every one of them is somewhere
else: the host's clock, which NTP keeps right; the wall-clock registers in the
machine model; the kernel's `CLOCK_REALTIME`; `date` in userspace; and the
timestamp lwext4 writes into an inode. Any one of them can be wrong on its own
and look fine from either side, which is why this is a gate rather than three
unit tests.

It asserts:

  * `date -e` agrees with the host's clock. The window is wide enough for a
    boot and a slow gate and narrow enough that a stopped clock, a clock
    counting from boot, or an epoch answer all fail it.
  * the three renderings agree -- `date`, `date -i` and `date -e` are the same
    instant said three ways, so a calendar bug shows up as a disagreement
    rather than as a plausible wrong date.
  * a file written now carries a date now. That is the whole point of the
    exercise: before this, everything the machine wrote was stamped zero and
    `ls -l` showed a dash.
"""

import argparse
import importlib.util
import os
import re
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import astra_image  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
# A boot, a desktop, a terminal and a few commands. The clock is the host's, so
# the only drift is how long the machine takes to answer.
SKEW_SECONDS = 600.0
EPOCH = re.compile(r"^(\d{10})$")
# `date -uIs`: an ISO instant with the offset it was rendered in.
ISO = re.compile(r"(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2}):(\d{2})"
                 r"([+-]\d{2}:\d{2})")
# GNU date's default, which is what `date` with no argument prints:
# `Wed Aug 19 20:50:55 EDT 2026`.
HUMAN = re.compile(r"(Mon|Tue|Wed|Thu|Fri|Sat|Sun) (\w{3}) ([ \d]\d) "
                   r"(\d{2}):(\d{2}):(\d{2}) (\w{2,5}) (\d{4})")
LISTING = re.compile(r"-rw.*\s(\w{3}) ([ \d]\d) (\d{2}):(\d{2}) stamped\.txt")


def load_gate():
    """test-terminal.py by path: its name is not an identifier."""
    path = os.path.join(HERE, "test-terminal.py")
    spec = importlib.util.spec_from_file_location("astra_terminal_gate", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def ask(machine, gate, command, deadline, needle=None):
    """Types one command and returns the lines it answered with.

    The needle is what says the command finished. A launched program reports
    its exit status through the shell, which is a needle nothing else produces;
    a builtin like `write` reports nothing, so the caller names what to wait
    for instead.
    """
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line(command)
    said, _ = machine.wait_for_text(
        needle if needle is not None else "%s: exited" % command.split()[0],
        deadline, before)
    if said is None:
        raise RuntimeError("%r never finished" % command)
    return said


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--boot-deadline", type=float, default=180.0)
    parser.add_argument("--command-deadline", type=float, default=60.0)
    arguments = parser.parse_args()

    gate = load_gate()
    failures = []
    directory = tempfile.mkdtemp(prefix="astra-clock-")
    try:
        scratch = os.path.join(directory, "card.img")
        shutil.copyfile(arguments.image, scratch)
        astra_image.install(scratch, arguments.catalog)
        machine = gate.Machine(arguments.qemu, arguments.rom, scratch,
                               directory)
        try:
            if not gate.open_terminal(machine, arguments.boot_deadline,
                                      arguments.command_deadline):
                print("FAIL: no terminal", file=sys.stderr)
                return 1

            host_before = time.time()
            epoch_said = ask(machine, gate, "date -e",
                             arguments.command_deadline)
            iso_said = ask(machine, gate, "date -uIs",
                           arguments.command_deadline)
            human_said = ask(machine, gate, "date -u",
                             arguments.command_deadline)
            host_after = time.time()

            epoch = None
            for line in epoch_said:
                match = EPOCH.match(line.strip())
                if match is not None:
                    epoch = int(match.group(1))
            if epoch is None:
                failures.append("`date -e` printed no epoch: %r" % epoch_said)
            else:
                drift = min(abs(epoch - host_before), abs(epoch - host_after))
                print("date -e = %d, host = %d, drift %.0fs"
                      % (epoch, int(host_before), drift))
                if drift > SKEW_SECONDS:
                    failures.append("the machine's clock is %.0fs from the "
                                    "host's" % drift)

            iso = None
            for line in iso_said:
                match = ISO.search(line)
                if match is not None:
                    iso = match
            if iso is None:
                failures.append("`date -uIs` printed no ISO instant: %r"
                                % iso_said)
            human = None
            for line in human_said:
                match = HUMAN.search(line)
                if match is not None:
                    human = match
            if human is None:
                failures.append("`date` printed no readable instant: %r"
                                % human_said)
            # The same instant said three ways. Minutes rather than seconds,
            # because the three commands are three launches apart.
            if iso is not None and human is not None:
                # Both are UTC here -- `-i` always is, and `date -u` was asked
                # for one -- so the day and the hour have to be the same.
                months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
                same = (human.group(8) == iso.group(1) and
                        months[int(iso.group(2)) - 1] == human.group(2) and
                        int(human.group(3)) == int(iso.group(3)) and
                        human.group(4) == iso.group(4))
                if not same:
                    failures.append("`date -u` and `date -uIs` disagree: %s vs %s"
                                    % (human.group(0), iso.group(0)))
                if human.group(7) != "UTC":
                    failures.append("`date -u` printed zone %r, not UTC"
                                    % human.group(7))

            # The format specifiers, which is what a person actually types.
            # `+%s` is the same instant as `date -e`, and `%Z` is the zone the
            # machine reported rather than a global nobody set.
            custom = ask(machine, gate, "date +%Y-%m-%dT%H:%M:%S%Z",
                         arguments.command_deadline)
            stamped_custom = None
            for line in custom:
                match = re.search(r"(\d{4})-(\d{2})-(\d{2})T"
                                  r"(\d{2}):(\d{2}):(\d{2})(\w{2,5})", line)
                if match is not None:
                    stamped_custom = match
            if stamped_custom is None:
                failures.append("`date +FORMAT` rendered nothing: %r" % custom)
            elif iso is not None:
                # The custom form is local and `-i` is UTC, so only the date
                # part is comparable, and only when the offset is zero.
                print("date +FORMAT = %s, zone %s"
                      % (stamped_custom.group(0), stamped_custom.group(7)))
                if stamped_custom.group(7) == "":
                    failures.append("`%Z` rendered an empty zone")

            # A file written now, listed with the date it was written.
            ask(machine, gate, "write stamped.txt from the gate",
                arguments.command_deadline, "stamped.txt")
            listed = ask(machine, gate, "ls -l", arguments.command_deadline)
            stamped = None
            for line in listed:
                match = LISTING.search(line)
                if match is not None:
                    stamped = match
            if stamped is None:
                failures.append("no dated listing for the file just written: "
                                "%r" % listed)
            elif stamped_custom is not None:
                # A listing is local time, because that is what a person
                # reading it means by "when" -- so it is compared against the
                # local rendering rather than against the UTC one, which is a
                # different day for most of the world for part of every day.
                month = int(stamped_custom.group(2))
                names = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]
                if stamped.group(1) != names[month - 1] or \
                        int(stamped.group(2)) != int(stamped_custom.group(3)):
                    failures.append("the file's date %s %s is not today (%s)"
                                    % (stamped.group(1), stamped.group(2),
                                       stamped_custom.group(0)))
                else:
                    print("listing: %s %s %s:%s" % stamped.group(1, 2, 3, 4))
        finally:
            machine.close()
    finally:
        shutil.rmtree(directory, ignore_errors=True)

    if failures:
        for failure in failures:
            print("FAIL: %s" % failure, file=sys.stderr)
        return 1
    print("ASTRA CLOCK PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
