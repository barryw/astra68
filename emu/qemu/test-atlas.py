#!/usr/bin/env python3
"""Launch Atlas through DH0 and the normal launcher, with real native entries."""

import argparse
from contextlib import ExitStack
import importlib.util
import os
import re
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
spec = importlib.util.spec_from_file_location(
    "atlas_audio_fixture", os.path.join(HERE, "test-sdl-audio.py"))
audio = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audio)


def fixture(image, work, overflow=False):
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
    if overflow:
        # 160 Desktop folders exceed the established 152-cell first page.
        image_tools._mkdir(volume, "/home/Desktop", "Desktop overflow root")
        for at in range(160):
            name = "ZZOverflow%03d" % at
            image_tools._mkdir(volume, "/home/Desktop/" + name,
                               "desktop overflow folder")
    image_tools._splice(image, offset, volume)


def wait_root(machine, root, count, folders, status, after, timeout):
    wanted = "atlas root %s entries %d folders %d status %d" % (
        root, count, folders, status)
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        lines, _ = machine.said_with_syslog(after)
        if any(wanted in line for line in lines):
            # Terminal remains open underneath and blinks every 500 ms.
            desktop.wait_quiet(machine, "Atlas root presentation", seconds=0.2)
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


def wait_desktop_offset(machine, expected, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        lines, _ = machine.said_with_syslog()
        rows = [re.search(r"desktop viewport (\d+) (\d+) (\d+)", line)
                for line in lines]
        rows = [tuple(map(int, row.groups())) for row in rows if row is not None]
        if rows and rows[-1][0] == expected(rows[-1][1]):
            return rows[-1]
        time.sleep(0.1)
    raise RuntimeError("desktop scroll did not reach expected offset: %s" % rows[-5:])


def close_atlas(machine, width=1300):
    # Atlas's normal frame starts at (360,120); close gadget follows the theme.
    click(machine, 360 + width - desktop.CLOSE_INSET, 120 + desktop.TITLE_Y)
    # Terminal regains focus and blinks every 500 ms; still reject continuous drawing.
    desktop.wait_quiet(machine, "Atlas close", seconds=0.2)


def command_output(machine, command, name, timeout):
    if not machine.settle():
        raise RuntimeError("terminal did not settle before native oracle: " +
                           command)
    before = machine.sequence()
    machine.qmp.type_line("{ " + command + "; } > /work/" + name +
                          "; print ATLAS-OUTPUT-$?")
    lines, _ = machine.wait_for_text("ATLAS-OUTPUT-0", timeout, before, exact=True)
    failure = "missing status-zero answer" if lines is None else None
    if failure is None and not machine.wait_for_ready(timeout, before):
        failure = "missing following shell prompt"
    paths = [os.path.join(directory, name)
             for directory, _, files in os.walk(machine.reader.hostfs_root)
             if name in files]
    if failure is not None:
        captured = []
        for path in paths[:2]:
            with open(path, encoding="utf-8", errors="replace") as output:
                captured.append(output.read(2048))
        raise RuntimeError("native oracle command failed (%s): %s\n"
                           "shell lines: %r\ncaptured output: %r\ntrace:\n%s" %
                           (failure, command, machine.said(before)[0][-40:], captured,
                            "\n".join(machine.recent_trace(24))))
    if len(paths) != 1:
        raise RuntimeError("native oracle output missing/ambiguous: %s" % paths)
    with open(paths[0], encoding="utf-8") as output:
        return output.read().splitlines()


def atlas_processes(machine, timeout):
    lines = command_output(machine, "ps", "atlas-processes.txt", timeout)
    return {int(line.split()[0]) for line in lines
            if line.strip().endswith("/apps/Atlas.app")}


def check_retired(machine, timeout, allowed):
    current = atlas_processes(machine, timeout)
    if current != allowed:
        raise RuntimeError("Atlas process set changed after close: %s != %s" %
                           (current, allowed))


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
    parser.add_argument("--preinstalled", action="store_true",
                        help="skip full reinstall; caller supplies current products")
    parser.add_argument("--overflow", action="store_true",
                        help="qualify desktop scrolling with 160 extra application entries")
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("desktop gate needs the root observer with explicit "
                     "ASTRA_TEST_QEMU_UID and ASTRA_TEST_QEMU_GID")
    with tempfile.TemporaryDirectory(prefix="astra-atlas-") as work, \
            ExitStack() as resources:
        image = os.path.join(work, "system.img")
        shutil.copyfile(args.image, image)
        if not args.preinstalled:
            image_tools.install(image)
        fixture(image, work, args.overflow)
        audio_socket = os.path.join(work, "audio.sock")
        host_audio = audio.AudioHost(audio_socket,
                                     hostfs=os.path.join(work, "exports"),
                                     synthesis=False)
        resources.callback(host_audio.close)
        previous_audio = os.environ.get("ASTRA_AUDIO_HOST_SOCKET")
        if previous_audio is None:
            resources.callback(os.environ.pop, "ASTRA_AUDIO_HOST_SOCKET", None)
        else:
            resources.callback(os.environ.__setitem__,
                               "ASTRA_AUDIO_HOST_SOCKET", previous_audio)
        os.environ["ASTRA_AUDIO_HOST_SOCKET"] = audio_socket
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            if not terminal.open_terminal(machine, 120, args.deadline):
                raise RuntimeError("Terminal could not open for native root oracle")
            drawer = atlas_processes(machine, args.deadline)
            count, folders = root_oracle(machine, args.deadline)
            before = machine.sequence()
            submitted = desktop.display_count(machine, "submissions")
            machine.qmp.double_click(*machine.desktop_icon("/dh0"))
            wait_root(machine, "/dh0", count, folders, 0, before, args.deadline)
            print("ATLAS DH0 root PASS", flush=True)
            if desktop.display_count(machine, "submissions") <= submitted:
                raise RuntimeError("Atlas submitted no drawing")
            close_atlas(machine)
            check_retired(machine, args.deadline, drawer)

            command_output(machine, "mkdir /ram0/AtlasFolder; print atlas-volume > /ram0/atlas-file.txt; ls /ram0", "atlas-ram.txt", args.deadline)
            before = machine.sequence()
            machine.qmp.double_click(*machine.desktop_icon("/ram0"))
            wait_root(machine, "/ram0", 2, 1, 0, before, args.deadline)
            close_atlas(machine)
            check_retired(machine, args.deadline, drawer)
            print("ATLAS RAM0 exact native root grant PASS", flush=True)

            if args.overflow:
                # Input uses the shared ScrollView policy. End exposes the
                # last Desktop folder; Home restores DH0's exact native root.
                click(machine, 1800, 990)
                machine.qmp.key("end")
                _, maximum, icon_count = wait_desktop_offset(machine, lambda maximum: maximum, args.deadline)
                if maximum == 0 or icon_count <= 152:
                    raise RuntimeError("overflow fixture did not create scrollable desktop")
                desktop.wait_quiet(machine, "desktop overflow end")
                x, y = machine.desktop_icon("/home/Desktop/ZZOverflow159", args.deadline)
                if not (0 < x < 1908 and 34 < y < 1038):
                    raise RuntimeError("last overflow icon remained offscreen: %s" % ((x, y),))
                # Last-page thumb lies in the lower half of the common bar.
                machine.qmp.point(1914, 800)
                machine.qmp.button(True)
                machine.qmp.point(1914, 100)
                machine.qmp.button(False)
                wait_desktop_offset(machine, lambda maximum: 0, args.deadline)
                desktop.wait_quiet(machine, "desktop scrollbar drag")
                click(machine, 1800, 990)
                machine.qmp.key("end")
                wait_desktop_offset(machine, lambda maximum: maximum, args.deadline)
                machine.qmp.point(1800, 990)
                machine.qmp.input_events([
                    {"type": "btn", "data": {"button": "wheel-up", "down": True}},
                    {"type": "btn", "data": {"button": "wheel-up", "down": False}}])
                wait_desktop_offset(machine, lambda maximum: max(0, maximum - 112), args.deadline)
                desktop.wait_quiet(machine, "desktop overflow wheel")
                machine.qmp.key("home")
                wait_desktop_offset(machine, lambda maximum: 0, args.deadline)
                desktop.wait_quiet(machine, "desktop overflow home")
                before = machine.sequence()
                machine.qmp.double_click(*machine.desktop_icon("/dh0"))
                wait_root(machine, "/dh0", count, folders, 0, before, args.deadline)
                close_atlas(machine)
                check_retired(machine, args.deadline, drawer)
                print("DESKTOP overflow last icon, scrollbar drag, wheel, home, exact DH0 launch PASS", flush=True)

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
                        desktop.wait_quiet(machine, "Atlas keyboard " + key,
                                           seconds=0.2)
                    machine.qmp.point(700, 350)
                    machine.qmp.input_events([
                        {"type": "btn", "data": {
                            "button": "wheel-down", "down": True}},
                        {"type": "btn", "data": {
                            "button": "wheel-down", "down": False}}])
                    desktop.wait_quiet(machine, "Atlas wheel", seconds=0.2)
                    # Drag the right resize edge in and out; normal client reflows.
                    start = 1300
                    for target in (1100, 1300):
                        machine.qmp.point(360 + start, 400)
                        machine.qmp.button(True)
                        machine.qmp.point(360 + target, 400)
                        machine.qmp.button(False)
                        desktop.wait_quiet(machine, "Atlas resize", seconds=0.2)
                        start = target
                close_atlas(machine)
                check_retired(machine, args.deadline, drawer)
                print("ATLAS %s PASS" % root, flush=True)
            references = command_output(machine, "volumes --references", "atlas-references.txt", args.deadline)
            rows = [line.split() for line in references if line.strip().endswith(" /ram0")]
            if len(rows) != 1 or len(rows[0]) != 4:
                raise RuntimeError("exact RAM0 reference missing: %s" % references)
            epoch, identity, generation = map(int, rows[0][:3])
            before = machine.sequence()
            machine.qmp.type_line("open /apps/Atlas.app --volume %d %d %d; print ATLAS-STALE-$?" %
                                  (epoch, identity, generation + 1))
            lines = terminal.wait_for_command(machine, "ATLAS-STALE-", args.deadline, before)
            lines = None if lines is None else machine.said(before)[0]
            statuses = [] if lines is None else [line.strip()[len("ATLAS-STALE-"):]
                for line in lines if line.strip().startswith("ATLAS-STALE-") and
                line.strip()[len("ATLAS-STALE-"):].isdigit()]
            if len(statuses) != 1 or statuses[0] == "0":
                raise RuntimeError("stale volume incarnation was admitted: %s" % lines)
            check_retired(machine, args.deadline, drawer)
            print("ATLAS stale incarnation refusal PASS", flush=True)

            desktop.require_pointer(machine, ((200, 200), (1500, 700)))
            if machine.process.poll() is not None or any(
                    "PANIC" in line for line in machine.log):
                raise RuntimeError("machine failed after Atlas")
            print("ASTRA ATLAS PASS DH0, RAM0 exact grant, 257 entries, UTF-8, empty/error, "
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
