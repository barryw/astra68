#!/usr/bin/env python3
"""Boot disposable Fonts autostart image and verify native GUI launch."""

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


def count(machine, name):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": "astra-display-" + name})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="astra-fonts-gate-") as work:
        image = str(Path(work) / "system.img")
        shutil.copyfile(args.image, image)
        # Make readiness include Fonts' catalog, controls, surface, and window.
        startup = Path(work) / "startup"
        startup.write_text(astra_image.DISPLAY_STARTUP_MANIFEST +
                           "service /apps/Fonts.app grants GUI FONTS LIBS:r "
                           "required\n", encoding="ascii")
        astra_image._replace_volume_file(image, str(startup), "/startup/system")
        machine = terminal.Machine(args.qemu, args.rom, image, work)
        try:
            if not machine.wait_for_serial(terminal.BOOT_MARKER, 120):
                raise RuntimeError("system did not reach stage 8")
            end = time.monotonic() + 20
            while time.monotonic() < end:
                if any("launch /apps/Fonts.app" in line and "status=0" in line
                       for line in machine.said()[0]):
                    break
                time.sleep(.1)
            else:
                raise RuntimeError("Fonts did not launch: %s" %
                                   machine.said()[0][-25:])
            if count(machine, "glyph-commands") == 0 or \
                    count(machine, "render-batches") == 0:
                raise RuntimeError("Fonts opened no hardware-rendered text")
            if machine.process.poll() is not None:
                raise RuntimeError("QEMU exited after Fonts launch")
            print("ASTRA FONTS GUI PASS native autostart, hardware text")
        finally:
            machine.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
