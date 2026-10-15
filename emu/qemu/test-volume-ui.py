#!/usr/bin/env python3
"""Qualify real Desktop Unmount, complete busy diagnostics and modal catalog updates."""

import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
import zlib


HERE = Path(__file__).resolve().parent


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, HERE / filename)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


atlas = module("volume_ui_atlas", "test-atlas.py")
linux = module("volume_ui_linux", "test-linux-first.py")
desktop = atlas.desktop
terminal = atlas.terminal
LONG_PATH = "/cd0/Folder/Long" + "x" * 180 + "/name\\x0awith\\\\escape.txt"


def fixture(image, work):
    linux.lifecycle_fixture(str(image), work)
    # Extend the same real ISO with a legal long/control-byte filename. Rock
    # Ridge preserves it; the UI must escape the bytes and wrap the whole path.
    tree = work / "optical-tree"
    nested = tree / "Folder" / ("Long" + "x" * 180)
    nested.mkdir()
    (nested / "name\nwith\\escape.txt").write_text("Complete busy path fixture\n", encoding="utf-8")
    iso = work / "disc.iso"
    subprocess.run(["genisoimage", "-quiet", "-input-charset", "utf-8", "-R", "-J",
        "-V", "ASTRA_DISC", "-o", str(iso), str(tree)], check=True)
    offset, size = atlas.image_tools.ext4_partition(str(image))
    volume = work / "ui-fixture.ext4"
    atlas.image_tools._slice(str(image), offset, size, str(volume))
    atlas.image_tools._debugfs(str(volume), "rm /home/disc.iso", "replace native UI ISO fixture")
    atlas.image_tools._debugfs(str(volume), "write %s /home/disc.iso" % iso, "complete native UI ISO fixture")
    atlas.image_tools._splice(str(image), offset, str(volume))


def desktop_has(machine, name, expected, timeout):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        lines = machine.said_with_syslog()[0]
        starts = [at for at, line in enumerate(lines)
                  if re.search(r"desktop viewport \d+ \d+ \d+$", line)]
        if starts:
            start = starts[-1]
            count = int(lines[start].split()[-1])
            names = [match.group(1) for line in lines[start + 1:]
                     if (match := terminal.DESKTOP_ICON.search(line)) is not None]
            if len(names) == count and (name in names) == expected:
                return
        time.sleep(.1)
    raise RuntimeError("desktop membership did not become %s=%s" % (name, expected))


def context_unmount(machine, name, timeout):
    before = machine.sequence()
    machine.qmp.point(*machine.desktop_icon(name))
    machine.qmp.input_events([{"type": "btn", "data": {"button": "right", "down": down}}
                             for down in (True, False)])
    # Input-device consumption precedes the application's asynchronous event
    # handling. Wait for the common menu's successful server admission before
    # sending keys; the same success log accompanies normal user interaction.
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        lines = machine.said_with_syslog(before)[0]
        if any("desktop context menu opened" in line for line in lines):
            break
        failures = [line for line in lines if "desktop context menu" in line and "failed" in line]
        if failures:
            raise RuntimeError("context menu was refused: %s" % failures)
        time.sleep(.05)
    else:
        raise RuntimeError("desktop context menu did not open")
    desktop.wait_quiet(machine, "desktop context menu presentation")
    # Shared menu End selects the last enabled row, regardless of hover state.
    machine.qmp.key("end")
    machine.qmp.key("ret")


def rendered(machine, predicate, after, timeout):
    end = time.monotonic() + timeout
    seen = []
    while time.monotonic() < end:
        lines = machine.said_with_syslog(after)[0]
        seen = [tuple(map(int, match.groups())) for line in lines
                if (match := re.search(r"desktop usage rendered (\d+) (\d+) (\d+) (\d+) (\d+) (\d+)$", line)) is not None]
        if seen and predicate(seen[-1]):
            return seen[-1]
        time.sleep(.1)
    raise RuntimeError("busy presentation did not match: %s" % (seen[-6:],))


def expected_text(lines):
    rows = []
    for line in lines:
        if line.startswith("PID COMMAND "):
            continue
        fields = line.split(" ", 7)
        if len(fields) != 8 or not fields[0].isdigit():
            raise RuntimeError("malformed native lsof output: %r" % line)
        pid, name, kind, handle, access, inode, volume, path = fields
        if kind != "file" or access != "r" or path not in ("/cd0/Folder/café.txt", LONG_PATH):
            raise RuntimeError("unexpected retained resource: %r" % line)
        rows.append((int(handle), int(pid), name, int(inode), path))
    if len(rows) != 33 or len({row[1] for row in rows}) != 1:
        raise RuntimeError("not all 33 exact shell-held files appeared: %s" % rows)
    if sum(row[4] == LONG_PATH for row in rows) != 1:
        raise RuntimeError("complete escaped long-path holder missing: %s" % rows)
    crc = 0
    for handle, pid, name, inode, path in sorted(rows):
        text = "PID %d (%s) — file #%d — read only — inode %d\n%s" % (pid, name, handle, inode, path)
        crc = zlib.crc32(text.encode("utf-8") + b"\0", crc)
    return crc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--deadline", type=float, default=90.0)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-volume-ui-") as temporary:
        work = Path(temporary)
        image = work / "system.img"
        shutil.copyfile(args.image, image)
        atlas.image_tools.install(str(image))
        built = subprocess.run(atlas.image_tools._build_invocation([
            "make", "-C", os.path.join(terminal.ROOT, "sw", "userspace", "commands"),
            "build/m68k/sbase-sleep"]), stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if built.returncode != 0:
            raise RuntimeError("could not build native fixture delay command: %s" %
                               built.stdout[-400:].decode("utf-8", "replace"))
        atlas.image_tools.install_test_command(str(image), "sbase-sleep")
        fixture(image, work)
        machine = terminal.Machine(args.qemu, args.rom, str(image), temporary)
        step = 0

        def command(text):
            nonlocal step
            step += 1
            return atlas.command_output(machine, text, "volume-ui-%03d.txt" % step, args.deadline)

        try:
            if not terminal.open_terminal(machine, 120, args.deadline):
                raise RuntimeError("Terminal did not start")
            command("/commands/sbase-sleep 0")
            roots = [line.split(" ", 7) for line in command("lsof /dh0")
                     if not line.startswith("PID COMMAND ")]
            if not any(len(row) == 8 and row[0].isdigit() and int(row[0]) > 1 and
                       row[1] in ("terminal", "zsh") and row[2] == "root" for row in roots):
                raise RuntimeError("delegated application roots lack actual native holder PIDs: %s" % roots)
            print("VOLUME UI delegated startup roots identify actual application holders PASS", flush=True)
            assert any("mounted /cd0" in line for line in command("mount --image /home/disc.iso"))
            desktop_has(machine, "/cd0", True, args.deadline)
            # Zsh's named descriptor form allocates the actual native-backed
            # descriptors without assuming a one-digit redirection grammar.
            command("; ".join("exec {hold%d}</cd0/Folder/caf*.txt" % descriptor for descriptor in range(32)) +
                "; exec {hold32}</cd0/Folder/Long*/name*.txt")
            # A separate native actor removes RAM0 while the busy modal is up.
            # Close its inherited optical descriptors before it waits for the
            # observer's readiness flag through the normal WORK filesystem.
            closes = "; ".join("exec {hold%d}<&-" % descriptor for descriptor in range(33))
            if machine.reader.hostfs_root is None:
                raise RuntimeError("private provider observation root is unavailable")
            ready = Path(machine.reader.hostfs_root) / "work" / "modal-ready"
            if not ready.parent.is_dir() or ready.exists():
                raise RuntimeError("private WORK readiness fixture is not fresh")
            command("( %s; while [[ ! -e /work/modal-ready ]]; do /commands/sbase-sleep 1 || exit; done; "
                    "unmount /ram0 > /work/modal-unmount.txt ) & print MODAL-WATCH-ARMED" % closes)
            crc = expected_text(command("( %s; lsof /cd0 )" % closes))
            before = machine.sequence()
            submissions = desktop.display_count(machine, "submissions")
            context_unmount(machine, "/cd0", args.deadline)
            first = rendered(machine, lambda row: row[0] == 33 and row[1] == crc and row[2] == 0 and
                row[3] > 0 and row[4] == 0, before, args.deadline)
            if desktop.display_count(machine, "submissions") <= submissions:
                raise RuntimeError("busy modal produced no real drawing")
            # The actor remains blocked until the dialog and all scrolling
            # checks have completed, regardless of guest/observer timing.
            desktop_has(machine, "/ram0", True, 2.0)
            # Viewport focus delegates End/Home/wheel to the common controls.
            atlas.click(machine, 800, 540)
            # Activating the ScrollView must leave the dialog open; only the
            # dialog's Close control owns dismissal. Prove this before the
            # separate outside-owner click check below.
            machine.qmp.key("end")
            rendered(machine, lambda row: row[:2] == first[:2] and row[2] == row[3] and row[5] == 32,
                     before, args.deadline)
            machine.qmp.key("home")
            rendered(machine, lambda row: row[:2] == first[:2] and row[2] == 0 and row[4] == 0,
                     before, args.deadline)
            # The busy dialog is modal for the Desktop's GUI session. An
            # outside Desktop click cannot move focus or send input behind it;
            # the already focused viewport must still receive End afterward.
            atlas.click(machine, 1800, 990)
            machine.qmp.key("end")
            rendered(machine, lambda row: row[:2] == first[:2] and row[2] == row[3] and row[5] == 32,
                     before, args.deadline)
            # Wheel input follows the pointer rather than keyboard focus.
            machine.qmp.point(800, 540)
            machine.qmp.input_events([{"type": "btn", "data": {"button": "wheel-up", "down": down}}
                                     for down in (True, False)])
            rendered(machine, lambda row: row[:2] == first[:2] and row[2] == row[3] - 20,
                     before, args.deadline)
            machine.qmp.key("home")
            rendered(machine, lambda row: row[:2] == first[:2] and row[2] == 0 and row[4] == 0,
                     before, args.deadline)
            ready.touch(mode=0o644, exist_ok=False)
            desktop_has(machine, "/ram0", False, args.deadline)
            desktop_has(machine, "/cd0", True, args.deadline)
            print("VOLUME UI busy exact PID/name/handle/inode, complete UTF-8/escaped/wrapped paths, 33 rows, outside-owner click refusal, End/wheel/Home, modal live catalog PASS", flush=True)
            # Escape uses the normal dialog close path; the same menu succeeds
            # only after the real retaining descriptors have all been closed.
            machine.qmp.key("esc")
            atlas.click(machine, 200, 140)
            command(closes)
            assert command("lsof /cd0") == ["PID COMMAND KIND HANDLE ACCESS INODE VOLUME PATH"]
            context_unmount(machine, "/cd0", args.deadline)
            desktop_has(machine, "/cd0", False, args.deadline)
            atlas.click(machine, 200, 140)
            end = time.monotonic() + args.deadline
            while True:
                graph = command("volumes --all")
                if not any(" ISO9660 " in line for line in graph):
                    break
                if time.monotonic() >= end:
                    raise RuntimeError("Desktop did not finish image-source cleanup: %s" % graph)
                time.sleep(.2)
            if machine.process.poll() is not None or any("PANIC" in line for line in machine.log):
                raise RuntimeError("machine failed after volume UI")
            print("VOLUME UI context Unmount actual retirement, disappearing icon and retained ISO source release PASS", flush=True)
        except Exception:
            print("Volume UI diagnostics:", flush=True)
            for line in machine.said_with_syslog()[0][-100:]:
                print(line, flush=True)
            print("\n".join(machine.recent_faults() + machine.recent_trace(40) + machine.recent_serial(40)), flush=True)
            raise
        finally:
            machine.close()


if __name__ == "__main__":
    main()
