#!/usr/bin/env python3
"""Exercise native AFNT import, catalog choice, and restart persistence."""

import argparse
import importlib.util
from pathlib import Path
import shutil
import tempfile
import time

import astra_image

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("terminal_gate", HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)

PROBE = "/apps/FontImportProbe.app"


def boot(qemu, rom, image, work, phase):
    machine = terminal.Machine(qemu, rom, image, work)
    try:
        if not machine.wait_for_serial(terminal.BOOT_MARKER, 120):
            lines = machine.said()[0]
            raise RuntimeError("system did not reach stage 8; fonts=%s; tail=%s" %
                               ([line for line in lines if "font" in line.lower()][-25:],
                                lines[-25:]))
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            lines = machine.said()[0]
            if any("launch " + PROBE in line and "status=0" in line
                   for line in lines):
                return
            if any("launch-failed " + PROBE in line for line in lines):
                raise RuntimeError("native import failed during %s: %s" %
                                   (phase, [line for line in lines
                                            if PROBE in line][-10:]))
            time.sleep(.1)
        raise RuntimeError("native import readiness absent during %s: %s" %
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
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-font-import-") as work:
        image = str(Path(work) / "system.img")
        shutil.copyfile(args.image, image)
        astra_image.install_service(image, "fonts")
        astra_image.install_test_app(image, args.bundle)
        startup = Path(work) / "startup"
        startup.write_text(
            astra_image.DISPLAY_STARTUP_MANIFEST +
            "service %s grants FONTS FONTS_ADMIN SYSTEM:r LIBS:r "
            "required priority=20\n" % PROBE, encoding="ascii")
        astra_image._replace_volume_file(image, str(startup), "/startup/system")
        boot(args.qemu, args.rom, image, work, "first boot")
        expectation = Path(work) / "expect-again"
        expectation.write_text("again\n", encoding="ascii")
        astra_image._replace_volume_file(
            image, str(expectation), "/expect-font-import-again")
        boot(args.qemu, args.rom, image, work, "restart")
        settings = Path(work) / "damaged-settings"
        settings.write_text(
            "astra-config 1\nschema 1\ninstalled = invalid\n"
            "system_font = user:missing\n", encoding="ascii")
        astra_image._replace_volume_file(
            image, str(settings), "/config/system/fonts/settings.conf")
        marker = Path(work) / "expect-corrupt"
        marker.write_text("corrupt\n", encoding="ascii")
        astra_image._replace_volume_file(
            image, str(marker), "/expect-font-inventory-corrupt")
        boot(args.qemu, args.rom, image, work, "damaged inventory")
    print("ASTRA FONT IMPORT PASS native AFNT, idempotence, selection, "
          "restart, damaged-inventory fallback")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
