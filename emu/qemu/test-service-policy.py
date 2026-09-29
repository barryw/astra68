#!/usr/bin/env python3
"""Service tiers and user service policy, through the `service` command.

Default mode, on an image built with `astra_image.py --fault-probe`:
  - `service list` names each service's tier;
  - every lifecycle and policy operation on a system or critical service is
    refused;
  - a user choice for a manifest service (media: not at boot, never
    restarted) survives a clean restart and is obeyed at boot;
  - a user service that keeps dying (fault-probe, restart=always) restarts
    at once, then backs off instead of hot-looping, and `service stop` ends
    the retries.

--critical, on an image built with `astra_image.py --fault-critical`:
  - fault-probe is a critical service; its death must halt the machine with
    a panic that names it.
"""

import argparse
import importlib.util
import os
import shutil
import subprocess
import tempfile
import time


HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)

SHUTDOWN_EXIT = 88
PROBE_DEATH = "fault probe dying"


class Shell:
    def __init__(self, machine):
        self.machine = machine
        self.number = 0

    def run(self, text, timeout=30.0):
        """Every line the command printed, and its exit status."""
        self.number += 1
        marker = "SP-COMMAND-%u-" % self.number
        self.machine.settle()
        before = self.machine.sequence()
        self.machine.qmp.type_line(text + "; print -r -- " + marker + "$?")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            lines, _ = self.machine.said(before)
            for line in lines:
                if line.startswith(marker) and line[len(marker):].isdigit():
                    return lines, int(line[len(marker):])
            time.sleep(0.05)
        raise RuntimeError("command hung: %s\n%s" %
                           (text, "\n".join(self.machine.said(before)[0])))

    def ok(self, text, *expected):
        lines, status = self.run(text)
        if status != 0:
            raise RuntimeError("command failed (%d): %s\n%s" %
                               (status, text, "\n".join(lines)))
        for needle in expected:
            if not any(needle in line for line in lines):
                raise RuntimeError("%r did not print %r:\n%s" %
                                   (text, needle, "\n".join(lines)))
        return lines

    def refused(self, text):
        lines, status = self.run(text)
        if status == 0 or not any("not permitted" in line for line in lines):
            raise RuntimeError("%r was not refused (status %d):\n%s" %
                               (text, status, "\n".join(lines)))


def boot(qemu, rom, image, directory, arguments):
    machine = terminal.Machine(qemu, rom, image, directory)
    if not terminal.open_terminal(machine, arguments.boot_deadline,
                                  arguments.command_deadline):
        machine.close()
        raise RuntimeError("Astra terminal did not start")
    return machine


def listed_tier(lines, service):
    for line in lines:
        fields = line.split()
        if fields and fields[0] == service:
            return fields[1]
    raise RuntimeError("service list omitted %s:\n%s" %
                       (service, "\n".join(lines)))


def check_tiers(shell):
    lines = shell.ok("service list", "TIER")
    for service, tier in (("storage", "critical"), ("display", "critical"),
                          ("desktop", "system"), ("input", "system"),
                          ("media", "user"), ("remote-desktop", "user")):
        if listed_tier(lines, service) != tier:
            raise RuntimeError("%s is not %s:\n%s" %
                               (service, tier, "\n".join(lines)))
    for text in ("service stop desktop", "service restart display",
                 "service pause storage", "service start input",
                 "service disable desktop", "service enable display",
                 "service set desktop restart never",
                 "service set storage restart always"):
        shell.refused(text)


def probe_deaths(machine, after):
    return sum(PROBE_DEATH in line for line in machine.said(after)[0])


def check_backoff(machine, shell):
    shell.ok("service add crash-probe /services/fault-probe "
             "--manual --restart=always")
    before = machine.sequence()
    shell.ok("service start crash-probe")
    # Immediate restart, then 0.2 s doubling to a 10 s cap: about seven
    # deaths in six seconds, one or two in the next eight. A hot loop is
    # hundreds; no restart at all is one.
    time.sleep(6.0)
    early = probe_deaths(machine, before)
    if not 4 <= early <= 12:
        raise RuntimeError("crash-probe died %d times in 6 s" % early)
    time.sleep(8.0)
    later = probe_deaths(machine, before)
    if not early < later <= early + 4:
        raise RuntimeError("crash-probe backoff: %d deaths, then %d" %
                           (early, later))
    shell.ok("service stop crash-probe")
    stopped = machine.sequence()
    time.sleep(12.0)
    if probe_deaths(machine, stopped) != 0:
        raise RuntimeError("crash-probe restarted after service stop")
    shell.ok("service inspect crash-probe", "state: stopped")
    shell.ok("service delete crash-probe")
    return later


def check_policy_before_restart(shell):
    shell.ok("service inspect media", "tier: user", "boot: yes",
             "restart: on-fault")
    shell.ok("service disable media")
    shell.ok("service set media restart never")
    shell.ok("service inspect media", "boot: no", "restart: never")


def check_policy_after_restart(shell):
    # Obeyed at boot: never launched, so "stopped" -- under QEMU, which has
    # no audio host, a launched media service would read "failed".
    shell.ok("service inspect media", "boot: no", "restart: never",
             "state: stopped")
    shell.ok("service enable media")
    shell.ok("service set media restart on-fault")
    shell.ok("service inspect media", "boot: yes", "restart: on-fault")


def clean_shutdown(machine, deadline):
    machine.qmp.type_line("shutdown")
    try:
        code = machine.process.wait(timeout=deadline)
    except subprocess.TimeoutExpired:
        raise RuntimeError("shutdown did not complete:\n" +
                           "\n".join(machine.recent_trace(40)))
    if code != SHUTDOWN_EXIT:
        raise RuntimeError("shutdown exited QEMU with %d" % code)


def run_policy(arguments, root):
    image = os.path.join(root, "storage.img")
    shutil.copyfile(arguments.image, image)
    first = os.path.join(root, "first")
    second = os.path.join(root, "second")
    os.mkdir(first)
    os.mkdir(second)
    machine = boot(arguments.qemu, arguments.rom, image, first, arguments)
    try:
        shell = Shell(machine)
        check_tiers(shell)
        deaths = check_backoff(machine, shell)
        check_policy_before_restart(shell)
        clean_shutdown(machine, arguments.command_deadline)
    finally:
        machine.close()
    machine = boot(arguments.qemu, arguments.rom, image, second, arguments)
    try:
        check_policy_after_restart(Shell(machine))
    finally:
        machine.close()
    print("ASTRA SERVICE POLICY PASS (tiers refused, %d probe deaths "
          "backed off, media policy persisted)" % deaths)


def run_critical(arguments, root):
    image = os.path.join(root, "storage.img")
    shutil.copyfile(arguments.image, image)
    directory = os.path.join(root, "machine")
    os.mkdir(directory)
    machine = terminal.Machine(arguments.qemu, arguments.rom, image,
                               directory)
    try:
        expected = "critical service fault-probe exited: status 77"
        if not machine.wait_for_serial(expected, arguments.boot_deadline):
            raise RuntimeError("no halt naming fault-probe; serial:\n" +
                               "\n".join(machine.recent_serial(60)))
        if not machine.wait_for_serial("SYSTEM HALTED", 10.0):
            raise RuntimeError("the machine did not halt; serial:\n" +
                               "\n".join(machine.recent_serial(60)))
    finally:
        machine.close()
    print("ASTRA SERVICE CRITICAL PASS (halted naming fault-probe)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--critical", action="store_true")
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    parser.add_argument("--command-deadline", type=float, default=60.0)
    arguments = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-service-policy-") as root:
        if arguments.critical:
            run_critical(arguments, root)
        else:
            run_policy(arguments, root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
