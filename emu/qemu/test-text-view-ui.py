#!/usr/bin/env python3
"""Exercise shared Text View, scrolling and clipboard through native input."""

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import time

import astra_image


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("text_view_terminal", HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)

EXPECTED = (
    "Shared Text View\n\n"
    "Read-only text remains selectable and copyable.\n"
    "TextModel owns UTF-8, lines and selection.\n"
    "TextSurface emits hardware glyph commands.\n"
    "ScrollView owns clipping and scrolling.\n\n"
    "Keyboard\tAction\r\n"
    "Arrows\tMove selection\n"
    "Shift\tExtend selection\n"
    "Home / End\tStart / end of line\n"
    "Ctrl+Home / End\tStart / end of document\n"
    "Page Up / Down\tMove through visible lines\n"
    "Meta+A / C\tSelect all / Copy\n\n"
    "Unicode: Ω 世界\n"
    "Long lines keep their original bytes; horizontal scrolling reveals text beyond the viewport.\n"
    "End of shared control example."
).encode("utf-8")


def glyphs(machine):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": "astra-display-glyph-commands"})


def presented(machine, before):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if glyphs(machine) > before:
            return
        time.sleep(.1)
    raise RuntimeError("native Text View produced no glyph commands")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("root observer needs explicit non-root ASTRA_TEST_QEMU_UID/GID")
    with tempfile.TemporaryDirectory(prefix="astra-text-view-ui-") as work:
        image = str(Path(work) / "system.img")
        shutil.copyfile(args.image, image)
        astra_image.install(image, vim_runtime=None)
        # Read clipboard bytes through the native NDK example, independently
        # of an interactive shell's interpretation of pasted text.
        astra_image._replace_volume_file(image, str(HERE.parents[1] /
            "ndk/build/dist/examples/text_clipboard.elf"),
            "/%s/ndk-text-clipboard" % astra_image.LOCAL_COMMANDS_DIRECTORY)
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            if not terminal.open_terminal(machine, 120, 60):
                raise RuntimeError("Terminal did not open")
            before = machine.sequence()
            machine.qmp.type_line("open /apps/InterfaceGallery.app; print TEXT-VIEW-OPEN-$?")
            if terminal.wait_for_command(machine, "TEXT-VIEW-OPEN-0", 60, before) is None:
                raise RuntimeError("Interface Gallery did not open")
            previous = glyphs(machine)
            machine.qmp.key("tab")
            for _ in range(4):
                machine.qmp.key("right")
            presented(machine, previous)
            # Text page: read-only Field, ScrollView, then Text View.
            for _ in range(3):
                machine.qmp.key("tab")
            previous = glyphs(machine)
            machine.qmp.chord("ctrl", "home")
            machine.qmp.chord("ctrl", "end")
            machine.qmp.key("up")
            machine.qmp.key("end")
            presented(machine, previous)
            previous = glyphs(machine)
            log_before = len(machine.syslog_lines())
            trace_before = machine.sequence()
            machine.qmp.chord("alt", "a")
            machine.qmp.chord("alt", "c")
            presented(machine, previous)
            deadline = time.monotonic() + 20
            while not any("gallery clipboard copy complete" in line for line in
                          machine.said(trace_before)[0] + machine.syslog_lines()[log_before:]):
                if time.monotonic() >= deadline:
                    raise RuntimeError("native clipboard copy did not complete")
                time.sleep(.1)
            # Terminal's left edge stays exposed beside Gallery.
            machine.qmp.point(220, 160)
            machine.qmp.button(True)
            machine.qmp.button(False)
            before = machine.sequence()
            machine.qmp.type_line(
                "ndk-text-clipboard --inspect; print TEXT-VIEW-COPY-EXIT-$?")
            lines = terminal.wait_for_command(machine, "TEXT-VIEW-COPY-EXIT-0", 60,
                                               before, exact=True)
            copied = "" if lines is None else "".join(
                line[len("clipboard:"):] for line in lines if line.startswith("clipboard:"))
            if lines is None or copied != EXPECTED.hex():
                raise RuntimeError("Text View clipboard output mismatch: " +
                                   repr(lines if lines is not None else
                                        machine.said(before)[0]))
            faults = [line for line in machine.said_with_syslog()[0]
                      if "process_fault" in line or "user fault:" in line.lower() or
                      "panic" in line.lower()]
            if faults:
                raise RuntimeError("native faults: " + repr(faults))
            print("ASTRA TEXT VIEW UI PASS: native scrolling, glyphs and multiline UTF-8 clipboard")
        finally:
            machine.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
