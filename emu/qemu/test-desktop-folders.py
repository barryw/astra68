#!/usr/bin/env python3
"""Exercise persistent Desktop folders and nested Atlas folder actions."""

import argparse
import importlib.util
import os
import re
import shutil
import tempfile
import time

HERE = os.path.dirname(__file__)
spec = importlib.util.spec_from_file_location("terminal_gate",
    os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)


def click(machine, x, y, button="left"):
    machine.qmp.point(x, y)
    machine.qmp.input_events([
        {"type": "btn", "data": {"button": button, "down": down}}
        for down in (True, False)])


def render_batches(machine):
    return machine.qmp.execute("qom-get", {"path": "/machine",
        "property": "astra-display-render-batches"})


def wait_render(machine, previous, deadline=10):
    end = time.monotonic() + deadline
    while render_batches(machine) <= previous:
        if time.monotonic() >= end:
            relevant = [line for line in machine.syslog_lines()
                if "atlas[" in line.lower() or "desktop[" in line or "display[" in line]
            raise RuntimeError("menu did not render: %s" % relevant[-35:])
        time.sleep(.05)


def new_folder(machine, point, name):
    batches = render_batches(machine)
    click(machine, *point, button="right")
    wait_render(machine, batches)
    batches = render_batches(machine)
    machine.qmp.key("down")
    machine.qmp.key("right")
    wait_render(machine, batches)
    machine.qmp.key("down")
    machine.qmp.key("ret")
    time.sleep(.3)
    machine.qmp.type_text(name)
    machine.qmp.key("ret")


def new_folder_hover(machine, point, name):
    batches = render_batches(machine)
    click(machine, *point, button="right")
    wait_render(machine, batches)
    batches = render_batches(machine)
    machine.qmp.point(point[0] + 32, point[1] + 16)
    wait_render(machine, batches)
    machine.qmp.point(point[0] + 190, point[1] + 24)
    machine.qmp.button(True)
    machine.qmp.button(False)
    time.sleep(.3)
    machine.qmp.type_text(name)
    machine.qmp.key("ret")


def wait_atlas_icon(machine, path, after, deadline):
    pattern = re.compile(r"atlas icon " + re.escape(path) +
                         r" (\d+) (\d+) (\d+) (\d+)$")
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        for line in reversed(machine.syslog_lines()[after:]):
            match = pattern.search(line)
            if match:
                x, y, w, h = map(int, match.groups())
                return x + w // 2, y + h // 2
        time.sleep(.1)
    raise RuntimeError("Atlas did not show %s: %s" %
                       (path, machine.syslog_lines()[after:][-25:]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--deadline", type=float, default=60)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-desktop-folders-") as work:
        image = os.path.join(work, "system.img")
        shutil.copyfile(args.image, image)
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            if not machine.wait_for_serial(terminal.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not boot: serial=%s log=%s exit=%s" %
                    (machine.log[-30:], machine.recent_serial(30),
                     machine.process.poll()))
            assert machine.desktop_icon("/apps", args.deadline)
            assert not any("desktop icon /apps/" in line
                           for line in machine.syslog_lines()), \
                "installed applications appeared on Desktop"
            new_folder(machine, (1500, 700), "Work")
            try:
                icon = machine.desktop_icon("/home/Desktop/Work", args.deadline)
            except RuntimeError as error:
                relevant = [line for line in machine.syslog_lines()
                    if "desktop[" in line or "desktop icon " in line or
                       "desktop viewport " in line or "folder" in line.lower() or
                       "context menu" in line or "menu command" in line]
                raise RuntimeError("folder creation failed: %s" % relevant[-35:]) from error
            click(machine, *icon, button="right")
            machine.qmp.key("down")
            machine.qmp.key("down")
            machine.qmp.key("ret")
            machine.qmp.type_text("Projects")
            machine.qmp.key("ret")
            icon = machine.desktop_icon("/home/Desktop/Projects", args.deadline)
            before = len(machine.syslog_lines())
            machine.qmp.double_click(*icon)
            end = time.monotonic() + args.deadline
            while not any("atlas root /home/Desktop/Projects entries 0 folders 0 status 0"
                          in line for line in machine.syslog_lines()[before:]):
                if time.monotonic() >= end:
                    raise RuntimeError("Atlas did not open Desktop folder")
                time.sleep(.1)
            before = len(machine.syslog_lines())
            new_folder(machine, (700, 250), "Inside")
            nested = wait_atlas_icon(machine, "/home/Desktop/Projects/Inside",
                                     before, args.deadline)
            machine.qmp.double_click(*nested)
            end = time.monotonic() + args.deadline
            while not any("atlas root /home/Desktop/Projects/Inside entries 0 folders 0 status 0"
                          in line for line in machine.syslog_lines()[before:]):
                if time.monotonic() >= end:
                    raise RuntimeError("Atlas did not open nested folder")
                time.sleep(.1)
            assert machine.process.poll() is None
            assert not any("PANIC" in line for line in machine.log)
            click(machine, 360 + 760 - 12, 120 + 15)
            before = len(machine.syslog_lines())
            click(machine, *nested, button="right")
            for _ in range(3):
                machine.qmp.key("down")
            machine.qmp.key("ret")
            end = time.monotonic() + args.deadline
            while not any("atlas root /home/Desktop/Projects entries 0 folders 0 status 0"
                          in line for line in machine.syslog_lines()[before:]):
                if time.monotonic() >= end:
                    raise RuntimeError("Atlas did not delete nested folder")
                time.sleep(.1)
            click(machine, 360 + 760 - 12, 120 + 15)
            before = len(machine.syslog_lines())
            click(machine, *icon, button="right")
            for _ in range(3):
                machine.qmp.key("down")
            machine.qmp.key("ret")
            end = time.monotonic() + args.deadline
            while not any("desktop viewport " in line
                          for line in machine.syslog_lines()[before:]):
                if time.monotonic() >= end:
                    raise RuntimeError("Desktop did not refresh after deletion")
                time.sleep(.1)
            assert machine.process.poll() is None
            new_folder_hover(machine, (1500, 700), "Persist")
            assert machine.desktop_icon("/home/Desktop/Persist", args.deadline)
            machine.close()
            machine = terminal.Machine(args.qemu, args.rom, image, work)
            if not machine.wait_for_serial(terminal.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not reboot after folder creation")
            assert machine.desktop_icon("/home/Desktop/Persist", args.deadline)
            assert machine.process.poll() is None
            print("ASTRA DESKTOP FOLDERS PASS create, rename, nested open, delete, reboot persistence")
        finally:
            machine.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
