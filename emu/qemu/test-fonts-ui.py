#!/usr/bin/env python3
"""Drive Fonts import, family activation and system choice through native UI."""

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
spec = importlib.util.spec_from_file_location("font_ui_terminal",
                                              HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)


def display_count(machine, name):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": "astra-display-" + name})


def state_on_disk(image, work):
    offset, length = astra_image.ext4_partition(image)
    volume = str(Path(work) / "inspect-volume.ext4")
    astra_image._slice(image, offset, length, volume)
    result = subprocess.run([
        "debugfs", "-R", "cat /config/system/fonts/settings.conf", volume],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0 or "while " in result.stdout:
        raise RuntimeError("cannot inspect Fonts settings: " + result.stdout)
    match = re.search(r"^system_font\s*=\s*(\S+)\s*$", result.stdout,
                      re.MULTILINE)
    choice = None if match is None else match.group(1).strip('"')
    inventory = re.search(r"^installed\s*=\s*(\S+)\s*$", result.stdout,
                          re.MULTILINE)
    return choice, None if inventory is None else inventory.group(1).strip('"')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("font", help="TTF to import through Fonts from DH0")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-fonts-ui-") as work:
        image = str(Path(work) / "system.img")
        shutil.copyfile(args.image, image)
        astra_image.install(image, vim_runtime=None)
        previous, old_inventory = state_on_disk(image, work)
        astra_image._replace_volume_file(image, args.font,
                                         "/zz-astra-font.ttf")
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            if not machine.wait_for_serial(terminal.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not reach stage 8")
            before = display_count(machine, "glyph-commands")
            machine.qmp.double_click(
                *machine.desktop_icon("/apps/Fonts.app"))
            end = time.monotonic() + 30
            while time.monotonic() < end:
                if any("launch /apps/Fonts.app" in line and "status=0" in line
                       for line in machine.said_with_syslog()[0]) and \
                        display_count(machine, "glyph-commands") > before:
                    break
                time.sleep(.1)
            else:
                raise RuntimeError("Fonts window did not open")
            # Fixture root enumerates the injected font at row 2.
            for _ in range(3):
                machine.qmp.key("tab")
            machine.qmp.key("ret")
            for _ in range(2):
                machine.qmp.key("tab")
            machine.qmp.key("home")
            machine.qmp.key("down")
            machine.qmp.key("down")
            machine.qmp.key("ret")
            time.sleep(8)
            before = display_count(machine, "glyph-commands")
            # First family row: client List begins at (24, 70).
            machine.qmp.double_click(520, 330)
            time.sleep(.3)
            if display_count(machine, "glyph-commands") <= before:
                raise RuntimeError("family double-click rendered no details")
            # Return to families, then activate the newly imported last family.
            for _ in range(3):
                machine.qmp.key("tab")
            machine.qmp.key("ret")
            for _ in range(2):
                machine.qmp.key("tab")
            machine.qmp.key("end")
            machine.qmp.key("ret")
            # Rebuilt detail view starts with no focus: Scroll, List, Back, Use.
            for _ in range(4):
                machine.qmp.key("tab")
            machine.qmp.key("ret")
            time.sleep(.5)
            after = display_count(machine, "glyph-commands")
            recent = [line for line in machine.said_with_syslog()[0]
                      if "font" in line.lower() or "exit" in line.lower()][-100:]
        finally:
            machine.close()
        selected, inventory = state_on_disk(image, work)
        if inventory is None or inventory == old_inventory:
            raise RuntimeError("UI import did not publish AFNT inventory: %r -> %r; choice=%r; recent=%s" %
                               (old_inventory, inventory, selected, recent))
        if selected is None or selected == previous:
            raise RuntimeError(
                "UI choice was not saved by Config Kit: %r -> %r; glyphs %u -> %u; recent=%s" %
                (previous, selected, before, after, recent))
        token = inventory.split(",")[-1]
        if re.fullmatch(r"[a-z2-7]{52}", token) is None:
            raise RuntimeError("invalid AFNT inventory token: " + token)
        if selected != "user:" + token:
            raise RuntimeError("UI did not select imported face: %r != user:%s" %
                               (selected, token))
        volume = str(Path(work) / "inspect-volume.ext4")
        result = subprocess.run([
            "debugfs", "-R", "stat /config/system/fonts/installed/" +
            token + ".afnt", volume], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        if result.returncode != 0 or "Inode:" not in result.stdout:
            raise RuntimeError("UI import AFNT object missing: " + result.stdout)
    print("ASTRA FONTS UI PASS TTF import, AFNT object, family detail, imported system choice")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
