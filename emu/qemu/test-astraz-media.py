#!/usr/bin/env python3
"""Run a synthetic Z6 picture/sound story through AstraZ's native engine.

The test application is assembled in a disposable copy of a release image.
It links the same private engine and SDL mixer code as AstraZ, and the story
and Blorb are generated locally without licensed game data.
"""

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE))
import astra_image

spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", HERE / "test-terminal.py")
terminal_gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal_gate)

MIXER_PROBE = ROOT / "sw/userspace/sdl2/tests/SDLMixerProbe.app"


def bundle(temporary, executable, mod):
    name = "AstraZMediaModProbe.app" if mod else "AstraZMediaProbe.app"
    destination = temporary / name
    program = destination / "bin/m68k-68040/astraz_media_probe"
    program.parent.mkdir(parents=True)
    shutil.copy2(executable, program)
    icon = destination / "resources/SDLMixerProbe.aicon"
    icon.parent.mkdir(parents=True)
    subprocess.run([sys.executable, str(ROOT / "tools/aicon.py"),
                    "gallery", str(icon)], check=True)
    subprocess.run([sys.executable, str(HERE / "astraz-media/fixture.py"),
                    str(destination)], check=True)
    if mod:
        shutil.copyfile(destination / "media-mod.blb",
                        destination / "media.blb")
    for extra in ("media-mod.blb", "media.mod", "media.png", "font3.z6",
                  "font3-display.z6"):
        (destination / extra).unlink()
    manifest = (MIXER_PROBE / "manifest").read_text(encoding="ascii")
    manifest = manifest.replace("org.astra.sdl-mixer-probe",
                                "org.astra.astraz-media-mod-probe" if mod else
                                "org.astra.astraz-media-probe")
    manifest = manifest.replace('name "SDL Mixer Probe"',
                                'name "AstraZ Media MOD Probe"' if mod else
                                'name "AstraZ Media Probe"')
    manifest = manifest.replace("bin/m68k-68040/SDLMixerProbe",
                                "bin/m68k-68040/astraz_media_probe")
    manifest += ("requires graphics.library 3 3.0.0\n"
                 "requires SDL2_image.library 2 2.8.12\n")
    (destination / "manifest").write_text(manifest, encoding="ascii")
    return destination


def run_case(args, temporary, mod):
    image = temporary / ("mod.img" if mod else "aiff.img")
    shutil.copyfile(args.image, image)
    app = bundle(temporary, args.probe, mod)
    astra_image.install_test_app(str(image), str(app))
    machine = terminal_gate.Machine(str(args.qemu), str(args.rom),
                                    str(image), str(temporary),
                                    source_root=os.environ.get(
                                        "ASTRA_TEST_SOURCE_ROOT", str(ROOT)))
    try:
        if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 120):
            raise RuntimeError("Astra did not reach desktop: %r" %
                               machine.recent_serial(40))
        expected = "ASTRAZ_MEDIA_MOD_PASS" if mod else "ASTRAZ_MEDIA_AIFF_PASS"
        failed = "ASTRAZ_MEDIA_MOD_FAIL" if mod else "ASTRAZ_MEDIA_AIFF_FAIL"
        deadline = time.monotonic() + 60
        while time.monotonic() < deadline:
            lines = machine.trace_with_syslog()
            if any(expected in line for line in lines):
                print("AstraZ synthetic Z6 %s: PASS" %
                      ("MOD" if mod else "AIFF"))
                return
            if any(failed in line for line in lines):
                raise RuntimeError("AstraZ %s media rejected: %r" %
                                   ("MOD" if mod else "AIFF", lines[-30:]))
            time.sleep(0.25)
        raise RuntimeError("AstraZ %s media timed out: %r" %
                           ("MOD" if mod else "AIFF",
                            machine.trace_with_syslog()[-40:]))
    finally:
        machine.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu", type=Path, nargs="?")
    parser.add_argument("rom", type=Path, nargs="?")
    parser.add_argument("image", type=Path, nargs="?")
    parser.add_argument("probe", type=Path, nargs="?")
    parser.add_argument("--prepare-bundles", nargs=2,
                        metavar=("DIRECTORY", "PROBE"),
                        help="Prepare both focused DE25 test bundles")
    parser.add_argument("--aiff-only", action="store_true",
                        help="Run baseline while generic MOD support is pending")
    args = parser.parse_args()
    if args.prepare_bundles:
        if any(item is not None for item in
               (args.qemu, args.rom, args.image, args.probe)):
            parser.error("bundle preparation takes no QEMU positional inputs")
        destination, executable = map(Path, args.prepare_bundles)
        if not executable.is_file():
            parser.error("missing probe executable: %s" % executable)
        destination.mkdir(parents=True, exist_ok=True)
        bundle(destination, executable, False)
        bundle(destination, executable, True)
        print("AstraZ DE25 media test bundles: READY")
        return
    if any(item is None for item in
           (args.qemu, args.rom, args.image, args.probe)):
        parser.error("QEMU, ROM, image, and probe are required")
    for item in (args.qemu, args.rom, args.image, args.probe):
        if not item.is_file():
            parser.error("missing input: %s" % item)
    with tempfile.TemporaryDirectory(prefix="astra-astraz-media-") as path:
        temporary = Path(path)
        run_case(args, temporary, False)
        if not args.aiff_only:
            run_case(args, temporary, True)


if __name__ == "__main__":
    main()
