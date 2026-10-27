#!/usr/bin/env python3
"""Focused path-policy checks for fast DE25 guest installs."""

import importlib.util
import hashlib
import os
from pathlib import Path
import tempfile
from types import SimpleNamespace


source = Path(__file__).with_name("dev-install-de25.py")
spec = importlib.util.spec_from_file_location("astra_dev_install", source)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

for target in ("/libs/SDL.kit/libraries/SDL2_mixer.library/abi-3/3.0.0/m68k-68040/SDL2_mixer.library", "/services/desktop",
               "/apps/Doom.app/Doom", "/fonts/system/Topaz.afnt",
               "/startup/system"):
    module.guest_target(target)

for target in ("/home/save", "/config/system", "/fonts/installed/user.afnt",
               "/libs/SDL2_mixer.library.3",
               "/libs/../home/private", "/apps//Doom.app", "libs/SDL2.library.3"):
    try:
        module.guest_target(target)
    except ValueError:
        pass
    else:
        raise AssertionError("unsafe development target accepted: " + target)

for target in ("bin/astra-codec-worker", "bin/astra-audio-host",
               "qemu/bin/qemu-system-m68k-astra", "rom/astra_boot.bin"):
    module.host_target(target)
for target in ("kernel/package/Image", "storage-terminal.img", "../libs/evil"):
    try:
        module.host_target(target)
    except ValueError:
        pass
    else:
        raise AssertionError("unsafe host target accepted: " + target)

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    target = module.checked_destination(root, "/apps/Doom.app/Doom")
    assert target == root / "apps/Doom.app/Doom"
    (root / "libs").symlink_to(root / "apps")
    try:
        module.checked_destination(root, "/libs/SDL.kit/libraries/SDL2.library/abi-3/3.0.0/m68k-68040/SDL2.library")
    except ValueError:
        pass
    else:
        raise AssertionError("guest symlink traversal accepted")

for unit, (environment, executable) in module.DROPINS.items():
    source = Path(__file__).resolve().parents[2] / "fpga/de25" / unit
    value = module.dev_unit(source.read_text(), environment, executable,
                            Path("/var/lib/astra/dev"))
    assert "ExecStart=/var/lib/astra/dev/current/" + executable in value
    if environment:
        assert "Environment=" + environment + "=/var/lib/astra/dev/current" in value

with tempfile.TemporaryDirectory() as directory:
    module.CONSOLE = Path(directory) / "old-console.log"
    module.CONSOLE.write_bytes(b"launch /services/hostfs status=0\n")
    clock = iter((0, 0, 1))
    module.time = SimpleNamespace(monotonic=lambda: next(clock), sleep=lambda _: None)
    module.run = lambda *args: "active"
    module.running_qemu = lambda: False
    try:
        module.wait_for_boot(timeout=0.5)
    except RuntimeError:
        pass
    else:
        raise AssertionError("stale console falsely certified a new boot")

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    base = root / "releases/base"
    (base / "bin").mkdir(parents=True)
    (base / "bin/tool").write_bytes(b"old")
    (base / "bin/tool").chmod(0o555)
    old = hashlib.sha256(b"old").hexdigest()
    (base / "artifacts.sha256").write_text(f"{old}  x  bin/tool\n")
    (root / "current").symlink_to("releases/base")
    artifact = root / "new-tool"
    artifact.write_bytes(b"new")
    artifact.chmod(0o755)
    module.STORE = root
    module.DEV = root / "dev"
    original_chown = os.fchown
    os.fchown = lambda *_: None
    try:
        identity = module.shadow_release(
            [(artifact, "bin/tool", hashlib.sha256(b"new").hexdigest())])
    finally:
        os.fchown = original_chown
    assert (base / "bin/tool").read_bytes() == b"old"
    assert (module.DEV / "releases" / identity / "bin/tool").read_bytes() == b"new"
    assert identity != hashlib.sha256((base / "artifacts.sha256").read_bytes()).hexdigest()

print("DE25 development installer tests: PASS")
