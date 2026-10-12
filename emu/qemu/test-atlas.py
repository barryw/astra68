#!/usr/bin/env python3
"""Launch Atlas through DH0 and the normal launcher, with real native entries."""

import argparse
import importlib.util
import os
import shutil
import tempfile
import time


HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    "desktop_apps", os.path.join(HERE, "test-desktop-apps.py"))
desktop = importlib.util.module_from_spec(spec)
spec.loader.exec_module(desktop)
terminal = desktop.terminal_gate
image_tools = terminal.astra_image


def fixture(image, work):
    offset, size = image_tools.ext4_partition(image)
    volume = os.path.join(work, "volume.ext4")
    image_tools._slice(image, offset, size, volume)
    image_tools._mkdir(volume, "/atlas-empty", "empty Atlas fixture")
    image_tools._mkdir(volume, "/atlas-many", "large Atlas fixture")
    content = os.path.join(work, "file.txt")
    with open(content, "w", encoding="utf-8") as handle:
        handle.write("Atlas native directory fixture\n")
    for at in range(255):
        image_tools._debugfs(volume,
            "write %s /atlas-many/file-%03d.txt" % (content, at),
            "Atlas file fixture")
    image_tools._mkdir(volume, "/atlas-many/Folder", "Atlas folder fixture")
    image_tools._debugfs(volume, 'write %s "/atlas-many/café.txt"' % content,
                         "Atlas UTF-8 fixture")
    image_tools._splice(image, offset, volume)


def wait_root(machine, root, count, folders, status, after, timeout):
    wanted = "atlas root %s entries %d folders %d status %d" % (
        root, count, folders, status)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        lines, _ = machine.said_with_syslog(after)
        if any(wanted in line for line in lines):
            desktop.wait_quiet(machine, "Atlas root presentation")
            return
        if any("Atlas window" in line or "application launch refused" in line or
               "desktop launch" in line for line in lines):
            raise RuntimeError("Atlas refused: %s" % lines[-20:])
        time.sleep(0.1)
    raise RuntimeError("missing %r: %s" % (
        wanted, machine.said_with_syslog(after)[0][-30:]))


def click(machine, x, y):
    machine.qmp.point(x, y)
    machine.qmp.button(True)
    machine.qmp.button(False)


def close_atlas(machine, width=760):
    # Atlas's normal frame starts at (360,120); close gadget follows the theme.
    click(machine, 360 + width - desktop.CLOSE_INSET, 120 + desktop.TITLE_Y)
    # Terminal regains focus and blinks every 500 ms; still reject continuous drawing.
    desktop.wait_quiet(machine, "Atlas close", seconds=0.2)


def command_output(machine, command, name, timeout):
    before = machine.sequence()
    machine.qmp.type_line(command + " > /work/" + name +
                          "; print ATLAS-OUTPUT-$?")
    lines = terminal.wait_for_command(machine, "ATLAS-OUTPUT-0", timeout, before,
                                      exact=True)
    if lines is None:
        raise RuntimeError("native oracle command failed: %s" % command)
    paths = [os.path.join(directory, name)
             for directory, _, files in os.walk(machine.reader.hostfs_root)
             if name in files]
    if len(paths) != 1:
        raise RuntimeError("native oracle output missing/ambiguous: %s" % paths)
    with open(paths[0], encoding="utf-8") as output:
        return output.read().splitlines()


def check_retired(machine, timeout):
    lines = command_output(machine, "ps", "atlas-processes.txt", timeout)
    if any("/apps/Atlas.app" in line for line in lines):
        raise RuntimeError("Atlas process survived close: %s" % lines)


def root_oracle(machine, timeout):
    entries = command_output(machine, "ls -1AF /dh0", "atlas-root.txt", timeout)
    if "atlas-many/" not in entries or "atlas-empty/" not in entries:
        raise RuntimeError("native root oracle omitted fixtures: %s" % entries)
    return len(entries), sum(name.endswith("/") for name in entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--deadline", type=float, default=60.0)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-atlas-") as work:
        image = os.path.join(work, "system.img")
        shutil.copyfile(args.image, image)
        fixture(image, work)
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            if not terminal.open_terminal(machine, 120, args.deadline):
                raise RuntimeError("Terminal could not open for native root oracle")
            count, folders = root_oracle(machine, args.deadline)
            before = machine.sequence()
            submitted = desktop.display_count(machine, "submissions")
            machine.qmp.double_click(*machine.desktop_icon("/dh0"))
            wait_root(machine, "/dh0", count, folders, 0, before, args.deadline)
            print("ATLAS DH0 root PASS", flush=True)
            if desktop.display_count(machine, "submissions") <= submitted:
                raise RuntimeError("Atlas submitted no drawing")
            close_atlas(machine)
            check_retired(machine, args.deadline)

            for root, count, folders, status in (
                    ("/dh0/atlas-many", 257, 1, 0),
                    ("/dh0/atlas-empty", 0, 0, 0),
                    ("/dh0/atlas-missing", 0, 0, 2),
                    ("/dh0/atlas-many/file-000.txt", 0, 0, 4)):
                before = machine.sequence()
                machine.qmp.type_line("open /apps/Atlas.app " + root)
                wait_root(machine, root, count, folders, status,
                          before, args.deadline)
                if count > 200:
                    machine.qmp.key("tab")
                    for key in ("pgdn", "end", "home"):
                        machine.qmp.key(key)
                        desktop.wait_quiet(machine, "Atlas keyboard " + key)
                    machine.qmp.point(700, 350)
                    machine.qmp.input_events([
                        {"type": "btn", "data": {
                            "button": "wheel-down", "down": True}},
                        {"type": "btn", "data": {
                            "button": "wheel-down", "down": False}}])
                    desktop.wait_quiet(machine, "Atlas wheel")
                    # Drag the right resize edge in and out; normal client reflows.
                    start = 760
                    for target in (480, 96, 760):
                        machine.qmp.point(360 + start, 400)
                        machine.qmp.button(True)
                        machine.qmp.point(360 + target, 400)
                        machine.qmp.button(False)
                        desktop.wait_quiet(machine, "Atlas resize")
                        start = target
                close_atlas(machine)
                check_retired(machine, args.deadline)
                print("ATLAS %s PASS" % root, flush=True)
            desktop.require_pointer(machine, ((200, 200), (1500, 700)))
            if machine.process.poll() is not None or any(
                    "PANIC" in line for line in machine.log):
                raise RuntimeError("machine failed after Atlas")
            print("ASTRA ATLAS PASS DH0, 257 entries, UTF-8, empty/error, "
                  "input/reflow, process retirement")
        except Exception:
            print("Atlas diagnostics:", flush=True)
            for line in machine.said_with_syslog()[0]:
                if "Atlas" in line or "atlas" in line or "desktop" in line:
                    print(line, flush=True)
            for line in machine.recent_faults() + machine.recent_trace(40):
                print(line, flush=True)
            print("\n".join(machine.recent_serial(50)), flush=True)
            raise
        finally:
            machine.close()


if __name__ == "__main__":
    main()
