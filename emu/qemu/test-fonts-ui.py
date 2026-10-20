#!/usr/bin/env python3
"""Drive Fonts import, family activation and system choice through native UI."""

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
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
font_spec = importlib.util.spec_from_file_location(
    "font_fixture_format", HERE.parents[1] / "tools/fonts/afnt.py")
font_format = importlib.util.module_from_spec(font_spec)
font_spec.loader.exec_module(font_format)


def display_count(machine, name):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": "astra-display-" + name})


def fresh_lines(machine, trace_before, syslog_before):
    return machine.said(trace_before)[0] + machine.syslog_lines()[syslog_before:]


def open_fonts(machine):
    before = display_count(machine, "glyph-commands")
    trace_before = machine.sequence()
    syslog_before = len(machine.syslog_lines())
    machine.qmp.double_click(*machine.desktop_icon("/apps/Fonts.app"))
    end = time.monotonic() + 30
    while time.monotonic() < end:
        # Retained host logs include earlier boots and window activations.
        lines = fresh_lines(machine, trace_before, syslog_before)
        if any("launch /apps/Fonts.app" in line and "status=0" in line
               for line in lines) and \
                display_count(machine, "glyph-commands") > before:
            return
        time.sleep(.1)
    raise RuntimeError("Fonts window did not open: " + repr(lines[-30:]))


def wait_ui(machine, operation, trace_before, syslog_before):
    end = time.monotonic() + 90
    while time.monotonic() < end:
        lines = fresh_lines(machine, trace_before, syslog_before)
        if any("fonts UI " + operation + " complete" in line for line in lines):
            return
        time.sleep(.1)
    raise RuntimeError("Fonts " + operation + " UI did not complete: " + repr(lines[-30:]))


def wait_import_state(machine, state, trace_before, syslog_before):
    end = time.monotonic() + 90
    while time.monotonic() < end:
        lines = fresh_lines(machine, trace_before, syslog_before)
        if any("fonts UI import " + state in line for line in lines):
            return
        time.sleep(.1)
    raise RuntimeError("Fonts import state absent: " + state + "; " + repr(lines[-30:]))


def uninstall_current(machine):
    trace_before = machine.sequence()
    syslog_before = len(machine.syslog_lines())
    # Last action must be reachable inside the default window.
    machine.qmp.point(800, 720)
    machine.qmp.button(True)
    machine.qmp.button(False)
    wait_ui(machine, "confirmation", trace_before, syslog_before)
    machine.qmp.key("tab")
    machine.qmp.key("tab")
    trace_before = machine.sequence()
    syslog_before = len(machine.syslog_lines())
    machine.qmp.key("ret")
    wait_ui(machine, "removal", trace_before, syslog_before)


def information_pane(machine):
    # Fresh details focus order: viewport, List, Back, Use, Information.
    for _ in range(5):
        machine.qmp.key("tab")
    trace_before, syslog_before = machine.sequence(), len(machine.syslog_lines())
    machine.qmp.key("ret")
    wait_ui(machine, "information", trace_before, syslog_before)
    for _ in range(2):
        machine.qmp.key("tab")
    machine.qmp.chord("ctrl", "end")
    machine.qmp.key("pgup")
    machine.qmp.chord("alt", "a")
    trace_before, syslog_before = machine.sequence(), len(machine.syslog_lines())
    machine.qmp.chord("alt", "c")
    wait_ui(machine, "information copy", trace_before, syslog_before)
    # Back returns to the selected style, with ordinary detail focus reset.
    machine.qmp.key("tab")
    trace_before, syslog_before = machine.sequence(), len(machine.syslog_lines())
    machine.qmp.key("ret")
    wait_ui(machine, "information close", trace_before, syslog_before)
    print("Fonts information pane: native Text View, scroll, select, copy and Back passed", flush=True)


def wait_live_font(machine, prefix, trace_before, syslog_before, glyphs_before):
    end = time.monotonic() + 15
    while time.monotonic() < end:
        lines = fresh_lines(machine, trace_before, syslog_before)
        adopted = {}
        for line in lines:
            match = re.search(r"display font adopted: (Desktop|Fonts) -> ([A-Za-z0-9:._-]+)", line)
            if match is not None and match[2].startswith(prefix):
                adopted[match[1]] = match[2]
        if adopted.get("Desktop") == adopted.get("Fonts") and "Fonts" in adopted and \
                display_count(machine, "glyph-commands") > glyphs_before:
            print("Fonts live adoption Desktop/Fonts=" + adopted["Fonts"], flush=True)
            return adopted["Fonts"]
        time.sleep(.1)
    raise RuntimeError("live system font did not reach Desktop and Fonts: " + repr(lines[-50:]))


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


def native_directory_names(machine, path):
    before = machine.sequence()
    machine.qmp.type_line("ls -1fiF %s; print FONT-BROWSER-LIST-DONE" % path)
    lines, _ = machine.wait_for_text("FONT-BROWSER-LIST-DONE", 30, before,
                                      exact=True)
    if lines is None or not machine.wait_for_ready(30, before):
        raise RuntimeError("native root listing did not complete")
    ordered = []
    for line in lines:
        match = re.fullmatch(r"\s*[0-9]+\s+(.+)", line)
        if match is None or match[1].endswith(("@", "|", "=")):
            continue
        candidate = match[1].rstrip("/*")
        if candidate not in (".", ".."):
            ordered.append(candidate)
    return ordered


def native_fixture_rows(machine, name):
    if not terminal.open_terminal_window(machine, 30):
        raise RuntimeError("cannot query native font fixture directory")
    ordered = native_directory_names(machine, "/dh0")
    if ordered.count(name) != 1:
        raise RuntimeError("native root listing lacks font: %r; logs=%r" %
                           (ordered, machine.said()[0][-40:]))
    shipped = [entry for entry in native_directory_names(machine, "/dh0/fonts")
               if entry.endswith(".afnt")]
    if shipped.count("astra-workbench.afnt") != 1:
        raise RuntimeError("shipped system face absent: %r" % shipped)
    families = []
    default_family = None
    for entry in shipped:
        path = HERE.parents[1] / "sw/userspace/graphics/fonts" / entry
        family = json.loads(font_format.parse_afnt(path.read_bytes())[b"NAME"])["family"]
        if family not in families:
            families.append(family)
        if entry == "astra-workbench.afnt":
            default_family = family
    machine.qmp.type_line("exit")
    time.sleep(.5)
    return ordered.index(name), families.index(default_family)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("font", nargs="?",
                        help="font to import through Fonts from DH0")
    parser.add_argument("--cancel-first", action="store_true",
                        help="cancel a delayed real import through native controls before retry")
    fixture = parser.add_mutually_exclusive_group()
    fixture.add_argument("--long-style", action="store_true",
                        help="exercise the longest accepted AFNT style name")
    fixture.add_argument("--unsupported-preview", action="store_true",
                        help="manage a valid AFNT whose bitmap format cannot be previewed")
    fixture.add_argument("--long-notice", action="store_true",
                        help="read and copy a complete 32 KiB multilingual license")
    args = parser.parse_args()
    if args.cancel_first and (args.font is None or args.long_style or
                              args.unsupported_preview or args.long_notice):
        parser.error("--cancel-first requires an outline font")
    if args.font is None and not (args.long_style or args.unsupported_preview or args.long_notice):
        parser.error("font is required unless a fixture is selected")
    if os.geteuid() != 0:
        parser.error("run as root observer with ASTRA_TEST_QEMU_UID/GID set to a non-root account")
    with tempfile.TemporaryDirectory(prefix="astra-fonts-ui-") as work:
        image = str(Path(work) / "system.img")
        shutil.copyfile(args.image, image)
        astra_image.install(image, vim_runtime=None)
        previous, old_inventory = state_on_disk(image, work)
        source = args.font
        if args.long_style or args.unsupported_preview or args.long_notice:
            parts = font_format.parse_afnt((HERE.parents[1] /
                "sw/userspace/graphics/fonts/astra-workbench.afnt").read_bytes())
            metadata = json.loads(parts[b"NAME"])
            metadata.update(family="Preview Fixture" if args.unsupported_preview else
                            "Long Notice Fixture" if args.long_notice else "Long Style Fixture",
                            style="S" * 127 if args.long_style else "Regular")
            if args.long_notice:
                notice = ("License Ω 世界\tcomplete original line\r\n" * 1024)[:32768]
                metadata["license"] = notice
                metadata["notices"] = [
                    {"name_id": 13, "platform_id": 3, "encoding_id": 1,
                     "language_id": 1033, "text": notice,
                     "raw_hex": notice.encode("utf-16-be").hex()},
                    {"name_id": 14, "platform_id": 42, "encoding_id": 99,
                     "language_id": 0, "raw_hex": "ff00"}]
            strikes = [parts[b"STRK"][at:at + font_format.STRIKE.size]
                       for at in range(4, len(parts[b"STRK"]), font_format.STRIKE.size)]
            if args.unsupported_preview:
                for at, record in enumerate(strikes):
                    fields = list(font_format.STRIKE.unpack(record))
                    assert fields[1] == font_format.A8
                    fields[1] = font_format.INDEX8
                    strikes[at] = font_format.STRIKE.pack(*fields)
            source = str(Path(work) / "fixture.afnt")
            Path(source).write_bytes(font_format.serialize_afnt(
                metadata, parts[b"CMAP"],
                strikes,
                [parts[b"GLYP"][at:at + font_format.GLYPH.size]
                 for at in range(4, len(parts[b"GLYP"]), font_format.GLYPH.size)],
                parts[b"BITM"]))
        astra_image._replace_volume_file(image, source,
                                         "/zz-astra-font.ttf")
        environment = None
        if args.cancel_first:
            worker = Path(os.environ.get("ASTRA_FONT_WORKER_PATH",
                                         "/usr/local/libexec/astra-font-worker"))
            if not worker.is_file():
                raise RuntimeError("native font worker required")
            os.chmod(work, 0o755)
            wrapper = Path(work) / "delayed-font-worker"
            wrapper.write_text("#!/bin/sh\nsleep 5\nexec " +
                               shlex.quote(str(worker.resolve())) + ' "$@"\n')
            wrapper.chmod(0o755)
            environment = os.environ.copy()
            environment["ASTRA_FONT_WORKER_PATH"] = str(wrapper)
        machine = terminal.Machine(args.qemu, args.rom, image, work, environment=environment)
        try:
            if not machine.wait_for_serial(terminal.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not reach stage 8")
            row, system_family = native_fixture_rows(machine, "zz-astra-font.ttf")
            print("Fonts native source row=%u" % row, flush=True)
            open_fonts(machine)
            for _ in range(3):
                machine.qmp.key("tab")
            machine.qmp.key("ret")
            for _ in range(2):
                machine.qmp.key("tab")
            machine.qmp.key("home")
            for _ in range(row):
                machine.qmp.key("down")
            trace_before = machine.sequence()
            syslog_before = len(machine.syslog_lines())
            machine.qmp.key("ret")
            if args.cancel_first:
                wait_import_state(machine, "started", trace_before, syslog_before)
                wait_import_state(machine, "progress", trace_before, syslog_before)
                # Only interactive control in the progress pane is shared Cancel.
                machine.qmp.key("tab")
                cancel_trace, cancel_log = machine.sequence(), len(machine.syslog_lines())
                machine.qmp.key("ret")
                wait_import_state(machine, "cancelling", cancel_trace, cancel_log)
                wait_import_state(machine, "cancelled", cancel_trace, cancel_log)
                print("Fonts native cancellation: progress, Cancel, retirement and retry passed", flush=True)
                # Cancellation returns to browser, retaining selected source row.
                for _ in range(2):
                    machine.qmp.key("tab")
                trace_before, syslog_before = machine.sequence(), len(machine.syslog_lines())
                machine.qmp.key("ret")
            wait_ui(machine, "import", trace_before, syslog_before)
            before = display_count(machine, "glyph-commands")
            trace_before = machine.sequence()
            syslog_before = len(machine.syslog_lines())
            # First family row: client List begins at (24, 70).
            machine.qmp.double_click(520, 330)
            wait_ui(machine, "details", trace_before, syslog_before)
            if display_count(machine, "glyph-commands") <= before:
                raise RuntimeError("family double-click rendered no details; recent=" +
                                   repr(machine.said_with_syslog()[0][-100:]))
            # Return to families, then activate the newly imported last family.
            for _ in range(3):
                machine.qmp.key("tab")
            machine.qmp.key("ret")
            for _ in range(2):
                machine.qmp.key("tab")
            machine.qmp.key("end")
            trace_before = machine.sequence()
            syslog_before = len(machine.syslog_lines())
            machine.qmp.key("ret")
            wait_ui(machine, "details", trace_before, syslog_before)
            if args.unsupported_preview:
                # Details remain usable even when the renderer rejects this strike.
                uninstall_current(machine)
                machine.close()
                machine = None
                choice, inventory = state_on_disk(image, work)
                default = "system:astra-workbench"
                if (choice or default) != (previous or default) or \
                        (inventory or "") != (old_inventory or ""):
                    raise RuntimeError("unsupported preview blocked removal: %r, %r" %
                                       (choice, inventory))
                print("ASTRA FONTS UI PASS unsupported preview, family detail, uninstall")
                return 0
            for _ in range(3):
                information_pane(machine)
            # Selection must preserve List focus before tabbing to Back/Use.
            for key in ("tab", "tab"):
                machine.qmp.key(key)
            for key in ("down", "up"):
                trace_before = machine.sequence()
                syslog_before = len(machine.syslog_lines())
                machine.qmp.key(key)
                wait_ui(machine, "preview", trace_before, syslog_before)
            for _ in range(2):
                machine.qmp.key("tab")
            trace_before = machine.sequence()
            syslog_before = len(machine.syslog_lines())
            glyphs_before = display_count(machine, "glyph-commands")
            machine.qmp.key("ret")
            live_selected = wait_live_font(machine, "user:", trace_before,
                                           syslog_before, glyphs_before)
            after = display_count(machine, "glyph-commands")
            faults = [line for line in machine.said_with_syslog()[0]
                      if "process_fault" in line or "user fault:" in line.lower() or
                      "panic" in line.lower()]
            if faults:
                raise RuntimeError("native font faults: " + repr(faults))
            recent = [line for line in machine.said_with_syslog()[0]
                      if "font" in line.lower() or "exit" in line.lower()][-100:]
        except Exception:
            if machine is not None:
                print("Fonts native failure: launcher exit=%r serial=%r" %
                      (machine.process.poll(), machine.recent_serial(100)), flush=True)
                try:
                    print("Fonts native failure syslog=" +
                          repr(machine.syslog_lines()[-100:]), flush=True)
                except OSError as error:
                    print("Fonts native failure syslog unavailable: " + repr(error), flush=True)
            raise
        finally:
            if machine is not None:
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
        if live_selected != selected:
            raise RuntimeError("live font differs from saved Config Kit choice")
        volume = str(Path(work) / "inspect-volume.ext4")
        result = subprocess.run([
            "debugfs", "-R", "stat /config/system/fonts/installed/" +
            token + ".afnt", volume], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        if result.returncode != 0 or "Inode:" not in result.stdout:
            raise RuntimeError("UI import AFNT object missing: " + result.stdout)
        installed = Path(work) / "installed.afnt"
        subprocess.run(["debugfs", "-R", "dump /config/system/fonts/installed/" +
            token + ".afnt " + str(installed), volume], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        metadata = json.loads(font_format.parse_afnt(installed.read_bytes())[b"NAME"])
        original = Path(source).read_bytes()
        if original.startswith(b"AFNT"):
            if metadata != json.loads(font_format.parse_afnt(original)[b"NAME"]):
                raise RuntimeError("native AFNT import changed source metadata")
        else:
            if metadata.get("converter") != "astra-font-worker/0.3" or \
                    metadata.get("source_files", [{}])[0].get("sha256") != \
                    hashlib.sha256(original).hexdigest() or \
                    not isinstance(metadata.get("notices"), list):
                raise RuntimeError("native import lost converter/source/notice metadata")
            for notice in metadata["notices"]:
                raw = bytes.fromhex(notice["raw_hex"])
                sfnt = original[:4] in (b"\0\1\0\0", b"OTTO", b"ttcf", b"true")
                if (sfnt and raw not in original) or not all(key in notice for key in (
                        "name_id", "platform_id", "encoding_id", "language_id")):
                    raise RuntimeError("native import lost original notice bytes/identity")
                if notice["platform_id"] == 0 or (notice["platform_id"] == 3 and
                        notice["encoding_id"] in (0, 1, 10)):
                    try:
                        decoded = raw.decode("utf-16-be")
                    except UnicodeDecodeError:
                        decoded = None
                    if decoded is not None and "\0" not in decoded and \
                            notice.get("text") != decoded:
                        raise RuntimeError("native import changed decoded notice text")
            print("Fonts persisted native source hash and %u complete notices" %
                  len(metadata["notices"]), flush=True)
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            if not machine.wait_for_serial(terminal.BOOT_MARKER, 120):
                raise RuntimeError("Astra did not restart after font selection")
            open_fonts(machine)
            # Choose shipped face before removing current imported system face.
            for key in ("tab", "tab", "home"):
                machine.qmp.key(key)
            for _ in range(system_family):
                machine.qmp.key("down")
            machine.qmp.key("ret")
            for _ in range(4):
                machine.qmp.key("tab")
            trace_before = machine.sequence()
            syslog_before = len(machine.syslog_lines())
            glyphs_before = display_count(machine, "glyph-commands")
            machine.qmp.key("ret")
            wait_live_font(machine, "system:", trace_before, syslog_before, glyphs_before)
            for _ in range(3):
                machine.qmp.key("tab")
            machine.qmp.key("ret")
            for key in ("tab", "tab", "end"):
                machine.qmp.key(key)
            trace_before = machine.sequence()
            syslog_before = len(machine.syslog_lines())
            machine.qmp.key("ret")
            wait_ui(machine, "details", trace_before, syslog_before)
            uninstall_current(machine)
        finally:
            machine.close()
        selected, inventory = state_on_disk(image, work)
        if selected is None or not selected.startswith("system:") or inventory != "":
            raise RuntimeError("UI uninstall did not save built-in choice and empty inventory: %r, %r" %
                               (selected, inventory))
        result = subprocess.run([
            "debugfs", "-R", "stat /config/system/fonts/installed/" +
            token + ".afnt", volume], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        if "Inode:" in result.stdout:
            raise RuntimeError("UI uninstall left AFNT object on disk")
    print("ASTRA FONTS UI PASS %s, AFNT object, family detail, system choice, uninstall" %
          ("long AFNT style" if args.long_style else
           "long multiline license" if args.long_notice else "font import"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
