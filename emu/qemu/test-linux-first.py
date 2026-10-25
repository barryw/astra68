#!/usr/bin/env python3
"""Boot the real Supervisor/HostFS pair against a disposable Linux-owned image.

Run as root observer with an explicit non-root QEMU UID/GID. The fixture launches
the qualified root provider, which drops every QEMU thread to those credentials. This
fixture is not an installed sudo entry point or a production launcher.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile
import time

import astra_image
from qemu_runtime import LAUNCHER_EXPORTS, provider_command

spec = importlib.util.spec_from_file_location(
    "terminal", Path(__file__).with_name("test-terminal.py"))
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)


def lifecycle_fixture(image, work):
    tree = work / "optical-tree"
    (tree / "Folder").mkdir(parents=True)
    (tree / "Folder" / "café.txt").write_text("Astra ISO test\n", encoding="utf-8")
    iso = work / "disc.iso"
    subprocess.run(["genisoimage", "-quiet", "-input-charset", "utf-8", "-R", "-J",
        "-V", "ASTRA_DISC", "-o", str(iso), str(tree)], check=True)
    udf = work / "disc.udf"
    subprocess.run(["genisoimage", "-quiet", "-udf", "-input-charset", "utf-8",
        "-R", "-J", "-V", "ASTRA_UDF", "-o", str(udf), str(tree)], check=True)
    offset, size = astra_image.ext4_partition(image)
    volume = work / "fixture.ext4"
    astra_image._slice(image, offset, size, volume)
    astra_image._debugfs(volume, "write %s /home/disc.iso" % iso, "native ISO fixture")
    astra_image._debugfs(volume, "write %s /home/disc.udf" % udf, "native UDF fixture")
    astra_image._splice(image, offset, volume)


def lifecycle_gate(machine, optical_only=False):
    spec = importlib.util.spec_from_file_location("atlas", Path(__file__).with_name("test-atlas.py"))
    atlas = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(atlas)
    step = 0

    def command(text):
        nonlocal step
        step += 1
        return atlas.command_output(machine, text, "lifecycle-%03d.txt" % step, 90)

    def desktop_has(name, expected):
        deadline = time.monotonic() + 45
        while time.monotonic() < deadline:
            lines = machine.said_with_syslog()[0]
            starts = [at for at, line in enumerate(lines)
                if re.search(r"desktop viewport \d+ \d+ \d+$", line)]
            if starts:
                start = starts[-1]
                count = int(lines[start].split()[-1])
                icons = [match.group(1) for line in lines[start + 1:]
                    if (match := terminal.DESKTOP_ICON.search(line)) is not None]
                if len(icons) == count and (name in icons) == expected: return
            time.sleep(.1)
        raise RuntimeError("desktop volume membership did not become %s=%s" % (name, expected))

    # Runtime shell predates every new mount, so all following access exercises
    # namespace refresh, command launch handoff and native/POSIX adapter use.
    for cycle in range(3):
        lines = command("mount --image /home/disc.iso")
        assert any("mounted /cd0" in line for line in lines), lines
        desktop_has("/cd0", True)
        lines = command("ls /cd0; cat /cd0/Folder/caf*.txt")
        assert "Folder" in lines and "Astra ISO test" in lines, lines
        space = command("df")
        mounted = [line.split() for line in space if line.startswith("/")]
        assert {row[0] for row in mounted} == {"/dh0", "/ram0", "/tmp", "/cd0"}, space
        assert all(len(row) == 5 and int(row[1]) > 0 and
            0 <= int(row[2]) <= int(row[1]) and 0 <= int(row[3]) <= int(row[1])
            and row[4] == row[0] for row in mounted), space
        lines = command("{ exec 9</cd0/Folder/caf*.txt; unmount /cd0; removal=$?; "
            "print BUSY-STATUS-$removal; lsof /cd0; exec 9<&-; [[ $removal == 1 ]]; } 2>&1")
        assert "BUSY-STATUS-1" in lines and any("/cd0/Folder/café.txt" in line for line in lines), lines
        assert any("PID COMMAND KIND HANDLE ACCESS INODE VOLUME PATH" in line for line in lines), lines
        if cycle == 0:
            drawer = atlas.atlas_processes(machine, 90)
            before = machine.sequence()
            machine.qmp.double_click(*machine.desktop_icon("/cd0"))
            atlas.wait_root(machine, "/cd0", 1, 1, 0, before, 90)
            atlas.close_atlas(machine)
            atlas.check_retired(machine, 90, drawer)
            cwd = command("cd /cd0/Folder; print CWD-PID-$$")
            holders = [int(line.removeprefix("CWD-PID-")) for line in cwd
                if line.startswith("CWD-PID-") and line.removeprefix("CWD-PID-").isdigit()]
            assert len(holders) == 1 and holders[0] > 1, cwd
            # Helpers leave the volume first, so only the parent shell's CWD
            # can cause this refusal; observing must not add its own holder.
            lines = command("{ (cd /; unmount /cd0); removal=$?; "
                "print CWD-BUSY-STATUS-$removal; (cd /; lsof /cd0); [[ $removal == 1 ]]; } 2>&1")
            rows = [line.split(" ", 7) for line in lines]
            assert "CWD-BUSY-STATUS-1" in lines and any(len(row) == 8 and
                row[0] == str(holders[0]) and row[1:3] == ["zsh", "directory"] and
                row[5].isdigit() and int(row[5]) > 0 and row[7] == "/cd0/Folder"
                for row in rows), lines
            command("cd /work")
        lines = command("{ print forbidden > /cd0/new; print READONLY-STATUS-$?; } 2>&1")
        assert "READONLY-STATUS-1" in lines, lines
        assert any("unmounted" in line for line in command("unmount /cd0"))
        desktop_has("/cd0", False)
        graph = command("volumes --all")
        assert not any(" ISO9660 " in line for line in graph), graph
    assert any("mounted /cd0" in line for line in command("mount --image /home/disc.udf"))
    desktop_has("/cd0", True)
    graph = command("volumes --all")
    assert any("mount" in line and "mounted" in line and "/cd0" in line and
        "UDF" in line and "ro" in line for line in graph), graph
    assert "Astra ISO test" in command("cat /cd0/Folder/caf*.txt")
    lines = command("{ print forbidden > /cd0/new; print READONLY-STATUS-$?; } 2>&1")
    assert "READONLY-STATUS-1" in lines, lines
    assert any("unmounted" in line for line in command("unmount /cd0"))
    desktop_has("/cd0", False)
    graph = command("volumes --all")
    assert not any(" UDF " in line for line in graph), graph
    if optical_only:
        print("NATIVE OPTICAL IMAGE PASS: ISO9660 and UDF root reads, desktop, "
              "read-only refusal, busy holders and image retirement", flush=True)
        return
    lines = command("{ unmount /dh0; print SYSTEM-STATUS-$?; } 2>&1")
    assert "SYSTEM-STATUS-1" in lines and any("running system" in line for line in lines), lines
    lines = command("{ unmount /tmp; print TMP-STATUS-$?; } 2>&1")
    assert "TMP-STATUS-1" in lines and any("running system" in line for line in lines), lines

    graph = command("volumes --all --references")
    sources = [line.split() for line in graph if len(line.split()) == 8 and
        line.split()[3] == "source" and int(line.split()[6]) & 256]
    assert len(sources) == 1, graph
    source = sources[0][1]
    old = command("volumes --references")
    old_ram = next(line.split()[:3] for line in old if line.endswith(" /ram0"))
    for cycle in range(3):
        assert any("unmounted" in line for line in command("unmount /ram0"))
        desktop_has("/ram0", False)
        if cycle == 1:
            before = machine.sequence()
            machine.qmp.type_line("ps | while read -r id rest; do "
                "[[ $rest == *' /services/hostfs' ]] && print kill > /proc/$id/ctl; done")
            assert machine.wait_for_trace_text("desktop ready", 90, before), "HostFS restart failed with RAM unmounted"
            desktop_has("/apps", True)
            atlas.desktop.wait_quiet(machine, "desktop after HostFS restart")
            assert terminal.open_terminal_window(machine, 90), "fresh Terminal failed after HostFS restart"
            desktop_has("/ram0", False)
        assert any("mounted /ram0" in line for line in command("mount --ram %s 16777216 1024" % source))
        desktop_has("/ram0", True)
        lines = command("print fresh-ram > /ram0/remounted.txt; cat /ram0/remounted.txt")
        assert "fresh-ram" in lines, lines
    drawer = atlas.atlas_processes(machine, 90)
    before = machine.sequence()
    machine.qmp.type_line("open /apps/Atlas.app --volume %s %s %s; print OLD-RAM-STATUS-$?" % tuple(old_ram))
    lines = terminal.wait_for_command(machine, "OLD-RAM-STATUS-", 90, before)
    assert lines is not None
    statuses = [line.strip().split("OLD-RAM-STATUS-", 1)[1] for line in machine.said(before)[0]
        if line.strip().startswith("OLD-RAM-STATUS-")]
    assert statuses and all(value != "0" for value in statuses), statuses
    atlas.check_retired(machine, 90, drawer)
    print("NATIVE LIFECYCLE PASS: late ISO/UDF, file/CWD busy/lsof, readonly, desktop/Atlas, image retirement, "
          "RAM remounts, HostFS restart while RAM absent, existing shell refresh and stale incarnation refusal", flush=True)


def main():
    parser = argparse.ArgumentParser()
    for name in ("qemu", "provider", "rom", "image"):
        parser.add_argument(name, type=lambda value: Path(value).resolve())
    parser.add_argument("--uid", type=int, required=True)
    parser.add_argument("--gid", type=int, required=True)
    parser.add_argument("--lifecycle", action="store_true")
    parser.add_argument("--optical-only", action="store_true")
    args = parser.parse_args()
    if os.geteuid() != 0 or args.uid <= 0 or args.gid <= 0:
        parser.error("root observer and explicit non-root QEMU UID/GID required")
    with tempfile.TemporaryDirectory(prefix="astra-linux-first-") as temporary:
        work = Path(temporary)
        work.chmod(0o755)
        image = work / "system.img"
        shutil.copyfile(args.image, image)
        manifest = astra_image.STARTUP_MANIFEST
        if args.lifecycle:
            # Recovery fault injection uses the existing service restart policy.
            # Production HostFS remains critical; only this disposable test
            # removes its critical designation, as the filesystem kill gate does.
            lines = manifest.splitlines()
            assert lines[0].startswith("service /services/hostfs ")
            lines[0] = lines[0].replace(" critical priority=20", " priority=20")
            manifest = "\n".join(lines) + "\n"
        astra_image.install(image, manifest_text=manifest)
        if args.lifecycle or args.optical_only:
            lifecycle_fixture(image, work)
        offset, length = astra_image.ext4_partition(image)
        exports = work / "exports"
        for name in ("work", "sound"):
            (exports / name).mkdir(parents=True)
        control = work / "control"
        control.mkdir()
        os.chown(control, args.uid, args.gid)
        provider = provider_command(args.provider, image, exports, args.uid, args.gid)
        # Machine's existing observation announcement points only to test
        # exports, never to the provider's private mounts or data authority.
        fixture = work / "provider-fixture.sh"
        fixture.write_text("#!/bin/sh\nset -eu\nprintf '%s\\n' " +
                           shlex.quote(LAUNCHER_EXPORTS + str(exports)) +
                           "\nexec " + shlex.join(provider) + ' "$@"\n')
        machine = terminal.Machine(str(args.qemu), str(args.rom), str(image),
            str(control), extra_args=("-pidfile", str(control / "qemu.pid")),
            launcher=("sh", str(fixture)), socket_owner=(args.uid, args.gid))
        passed = False
        try:
            pid = int((control / "qemu.pid").read_text())
            for task in Path("/proc/%d/task" % pid).iterdir():
                status = dict(line.split(":", 1) for line in (task / "status").read_text().splitlines()
                              if ":" in line)
                assert list(map(int, status["Uid"].split())) == [args.uid] * 4
                assert list(map(int, status["Gid"].split())) == [args.gid] * 4
                assert not status["Groups"].strip() and status["NoNewPrivs"].strip() == "1"
                assert all(int(status[name].strip(), 16) == 0 for name in
                           ("CapInh", "CapPrm", "CapEff", "CapBnd", "CapAmb"))
            assert terminal.open_terminal(machine, 90, 45), "desktop/Terminal did not start"
            print("native desktop, Terminal and shell ready", flush=True)
            commands = (
                ("volumes", "/dh0"),
                ("volumes --all", "source"),
                ("df", "1K-blocks Used Available Mounted on"),
                ("cat /dh0/startup/system", "service /services/hostfs grants HOST_FILESYSTEM"),
                ("ls /dh0", "services"),
                ("print linux-first > /home/linux-first.txt; cat /home/linux-first.txt", "linux-first"),
                ("print ram-native > /ram0/native.txt; cat /ram0/native.txt", "ram-native"),
                ("print tmp-native > /tmp/native.txt; cat /tmp/native.txt", "tmp-native"),
                ("print work-native > /work/native.txt; cat /work/native.txt", "work-native"),
                ("ps", "/services/hostfs"),
            )
            for command, expected in commands:
                before = machine.sequence()
                machine.qmp.type_line(command + "; print ASTRA-NATIVE-EXIT-$?")
                assert terminal.wait_for_command(machine, expected, 45, before) is not None, command
                assert terminal.wait_for_command(machine, "ASTRA-NATIVE-EXIT-0", 45, before) is not None, command
                assert machine.settle(), command
                print("PASS: " + command, flush=True)
            if args.lifecycle or args.optical_only:
                lifecycle_gate(machine, args.optical_only)
            lines = "\n".join(machine.said_with_syslog()[0])
            assert "/services/storage" not in lines and "/services/ramfs" not in lines
            assert "store defaults:" not in lines, lines
            assert not any("process_fault" in line or "panic" in line.lower()
                           for line in machine.trace()), machine.recent_faults()
            before = machine.sequence()
            machine.qmp.type_line("shutdown")
            assert machine.process.wait(timeout=45) == 88, machine.said(before)[0]
            passed = True
        finally:
            if not passed:
                print("\n".join(machine.said_with_syslog()[0]))
                print("\n".join(machine.recent_serial()))
            machine.close()
        volume = work / "volume.img"
        astra_image._slice(image, offset, length, volume)
        subprocess.run(["e2fsck", "-fn", str(volume)], check=True)
        saved = subprocess.check_output(["debugfs", "-R", "cat /home/linux-first.txt", str(volume)],
                                        stderr=subprocess.DEVNULL, text=True)
        assert saved == "linux-first\n", saved
        assert (exports / "work" / "native.txt").read_text() == "work-native\n"
        print("ASTRA LINUX-FIRST BOOT PASS: native system/RAM/tmp/work, no guest writer, durable shutdown")


if __name__ == "__main__":
    main()
