#!/usr/bin/env python3
"""Exercise native outline import, AFNT publication and persistent selection."""

import argparse
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time

import astra_image

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("terminal_gate", HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)

PROBE = "/apps/FontOutlineProbe.app"


def corrupt_later_face(source, destination):
    data = bytearray(Path(source).read_bytes())
    if len(data) < 20 or data[:4] != b"ttcf" or \
            int.from_bytes(data[8:12], "big") < 2:
        raise RuntimeError("later-face failure needs a two-face TTC")
    second = int.from_bytes(data[16:20], "big")
    if second + 4 > len(data):
        raise RuntimeError("invalid second TTC face offset")
    data[second:second + 4] = b"FAIL"
    Path(destination).write_bytes(data)


def stored_objects(image, work):
    offset, length = astra_image.ext4_partition(image)
    volume = str(Path(work) / "inspect-volume.ext4")
    astra_image._slice(image, offset, length, volume)
    result = subprocess.run(
        ["debugfs", "-R", "ls -p /config/system/fonts/installed", volume],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0 or "while " in result.stdout:
        raise RuntimeError("cannot inspect font store: " + result.stdout)
    return re.findall(r"[a-z2-7]{52}\.(?:afnt|tmp)", result.stdout)


def font_syslog(machine):
    root = machine.reader.hostfs_root
    if root is None:
        return ""
    result = subprocess.run(
        ["sudo", "-n", "find", root, "-type", "f", "-name", "syslog*",
         "-exec", "grep", "-iE",
         "fonts host|fonts stage|fonts publish|font worker", "{}", "+"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return result.stdout[-4000:] if result.returncode in (0, 1) else result.stderr[-1000:]


def boot(qemu, rom, image, work, phase):
    machine = terminal.Machine(qemu, rom, image, work)
    try:
        deadline = time.monotonic() + 120
        reached = False
        while time.monotonic() < deadline:
            if machine.wait_for_serial(terminal.BOOT_MARKER, 1):
                reached = True
                break
            if any("launch-failed " + PROBE in line for line in machine.said()[0]):
                break
        if not reached:
            lines = machine.said()[0]
            failures = [line for line in lines if PROBE in line][-10:]
            fonts = [line for line in lines if "font" in line.lower()][-25:]
            serial = [line for line in machine.log if "font" in line.lower()][-30:]
            raise RuntimeError("stage 8 absent; probe=%s; fonts=%s; serial=%s; syslog=%s; tail=%s" %
                               (failures, fonts, serial, font_syslog(machine), lines[-15:]))
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            lines = machine.said()[0]
            if any("launch " + PROBE in line and "status=0" in line
                   for line in lines):
                return
            if any("launch-failed " + PROBE in line for line in lines):
                raise RuntimeError("%s: %s" %
                                   (phase, [line for line in lines
                                            if PROBE in line][-10:]))
            time.sleep(.1)
        raise RuntimeError("%s readiness absent: %s" %
                           (phase, [line for line in machine.said()[0]
                                    if PROBE in line][-10:]))
    finally:
        machine.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("bundle")
    parser.add_argument("source")
    parser.add_argument("--expect-limit", action="store_true")
    parser.add_argument("--expect-later-failure", action="store_true")
    parser.add_argument("--expect-variable", action="store_true")
    parser.add_argument("--amiga", action="store_true",
                        help="run fixed-strike Amiga import probe")
    args = parser.parse_args()
    global PROBE
    if args.amiga:
        if args.expect_limit or args.expect_later_failure or args.expect_variable:
            parser.error("--amiga cannot combine with outline-only cases")
        PROBE = "/apps/FontAmigaProbe.app"
    if not Path("/usr/local/libexec/astra-font-worker").is_file():
        raise RuntimeError("installed Astra font worker required")
    with tempfile.TemporaryDirectory(prefix="astra-font-outline-") as work:
        image = str(Path(work) / "system.img")
        shutil.copyfile(args.image, image)
        astra_image.install(image, vim_runtime=None)
        astra_image.install_test_app(image, args.bundle)
        source = args.source
        if args.expect_later_failure:
            source = str(Path(work) / "bad-second-face.ttc")
            corrupt_later_face(args.source, source)
        astra_image._replace_volume_file(
            image, source, "/fonts/amiga-fixture.amiga" if args.amiga else
            "/fonts/outline-fixture.ttf")
        if args.expect_limit:
            limit = Path(work) / "expect-limit"
            limit.write_text("limit\n", encoding="ascii")
            astra_image._replace_volume_file(
                image, str(limit), "/expect-outline-limit")
        if args.expect_later_failure:
            marker = Path(work) / "expect-later-failure"
            marker.write_text("later-failure\n", encoding="ascii")
            astra_image._replace_volume_file(
                image, str(marker), "/expect-outline-later-failure")
        if args.expect_variable:
            marker = Path(work) / "expect-variable"
            marker.write_text("variable\n", encoding="ascii")
            astra_image._replace_volume_file(
                image, str(marker), "/expect-outline-variable")
        startup = Path(work) / "startup"
        startup.write_text(
            astra_image.DISPLAY_STARTUP_MANIFEST +
            "service %s grants FONTS FONTS_ADMIN FONTS_RENDER SYSTEM:r LIBS:r "
            "required priority=20\n" % PROBE, encoding="ascii")
        astra_image._replace_volume_file(image, str(startup), "/startup/system")
        boot(args.qemu, args.rom, image, work, "first boot")
        if args.expect_later_failure:
            objects = stored_objects(image, work)
            if objects:
                raise RuntimeError("failed collection left AFNT objects: %s" %
                                   objects)
            print("ASTRA FONT OUTLINE PASS later-face failure rolled back")
            return 0
        if args.expect_limit:
            print("ASTRA FONT OUTLINE PASS oversized AFNT limit, no partial catalog")
            return 0
        marker = Path(work) / "again"
        marker.write_text("again\n", encoding="ascii")
        astra_image._replace_volume_file(
            image, str(marker), "/expect-outline-again")
        boot(args.qemu, args.rom, image, work, "restart")
    if args.amiga:
        print("ASTRA FONT AMIGA PASS native import, AFNT, catalog, restart")
    elif args.expect_variable:
        print("ASTRA FONT OUTLINE PASS variable styles, AFNT, selection, restart")
    else:
        print("ASTRA FONT OUTLINE PASS native outline->AFNT, render, selection, restart")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
