#!/usr/bin/env python3
"""Focused path-policy checks for fast DE25 guest installs."""

import importlib.util
import fcntl
import hashlib
import os
from pathlib import Path
import tempfile
import threading
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
               "qemu/bin/qemu-system-m68k-astra", "rom/astra_boot.bin",
               "systemd/astra-remote-desktop.service"):
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
    try:
        module.checked_destination(root, "/commands/missing", create=False)
    except FileNotFoundError:
        pass
    else:
        raise AssertionError("missing removal target accepted")
    (root / "commands").mkdir()
    artifact = root / "commands/probe"
    artifact.write_bytes(b"expected")
    try:
        module.remove_file(root, artifact, hashlib.sha256(b"other").hexdigest())
    except RuntimeError:
        assert artifact.read_bytes() == b"expected"
    else:
        raise AssertionError("mismatched guest artifact removed")
    module.remove_file(root, artifact, hashlib.sha256(b"expected").hexdigest())
    assert not artifact.exists()
    assert (root / "commands").is_dir()
    bundle = root / "apps/Trial.app"
    payload = bundle / "bin/m68k-68040/Trial"
    payload.parent.mkdir(parents=True)
    payload.write_bytes(b"trial")
    module.remove_file(root, payload, hashlib.sha256(b"trial").hexdigest())
    assert not bundle.exists() and (root / "apps").is_dir()
    payload.parent.mkdir(parents=True)
    payload.write_bytes(b"trial")
    (bundle / "user-data").write_bytes(b"keep")
    module.remove_file(root, payload, hashlib.sha256(b"trial").hexdigest())
    assert (bundle / "user-data").read_bytes() == b"keep"
    assert not (bundle / "bin").exists()
    (root / "commands/link").symlink_to(root / "apps/Doom.app/Doom")
    try:
        module.checked_destination(root, "/commands/link", create=False)
    except ValueError:
        pass
    else:
        raise AssertionError("symlink removal target accepted")
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
    module.CONSOLE = Path(directory) / "console.log"
    module.QMP = SimpleNamespace(is_socket=lambda: True)
    module.run = lambda *args: "active"
    module.running_qemu = lambda: True
    for payload, expected in (
            (b"launch /services/hostfs status=0\n", "boot-service readiness"),
            (b"launch /services/hostfs status=0\nSystem degraded:\n", "degraded")):
        module.CONSOLE.write_bytes(payload)
        clock = iter((0, 0, 1))
        module.time = SimpleNamespace(monotonic=lambda: next(clock), sleep=lambda _: None)
        try:
            module.wait_for_boot(timeout=0.5)
        except RuntimeError as error:
            assert expected in str(error)
        else:
            raise AssertionError("failed boot falsely certified")
    module.CONSOLE.write_bytes(b"Initial image ....... stage 8\n")
    clock = iter((0, 0))
    module.time = SimpleNamespace(monotonic=lambda: next(clock), sleep=lambda _: None)
    module.wait_for_boot(timeout=0.5)

    states = iter(("activating", "active"))
    module.run = lambda *args: (next(states) if "ActiveState" in args
                                else "active")
    clock = iter((0, 0, 0))
    module.time = SimpleNamespace(monotonic=lambda: next(clock), sleep=lambda _: None)
    module.wait_for_boot(timeout=0.5)

    module.run = lambda *args: "failed"
    clock = iter((0, 0))
    module.time = SimpleNamespace(monotonic=lambda: next(clock), sleep=lambda _: None)
    try:
        module.wait_for_boot(timeout=0.5)
    except RuntimeError as error:
        assert "Astra service exited" in str(error)
    else:
        raise AssertionError("failed service falsely certified")

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

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    base = root / "releases/base"
    (base / "systemd").mkdir(parents=True)
    old_unit = b"[Service]\nExecStart=/bin/old\nEnvironment=ASTRA_RFB_LISTEN_ADDRESS=0.0.0.0\n"
    new_unit = old_unit.replace(b"0.0.0.0", b"127.0.0.1")
    records = []
    for unit in module.DROPINS:
        name = "systemd/" + unit
        (base / name).write_bytes(old_unit)
        records.append(f"{hashlib.sha256(old_unit).hexdigest()}  -  {name}\n")
    (base / "artifacts.sha256").write_text("".join(records))
    (root / "current").symlink_to("releases/base")
    artifact = root / "new-unit"
    artifact.write_bytes(new_unit)
    module.STORE = root
    module.DEV = root / "dev"
    module.SYSTEMD = root / "systemd"
    module.SYSTEMD.mkdir()
    module.run = lambda *_: ""
    original_chown = os.fchown
    os.fchown = lambda *_: None
    try:
        identity = module.shadow_release(
            [(artifact, "systemd/astra-remote-desktop.service",
              hashlib.sha256(new_unit).hexdigest())])
    finally:
        os.fchown = original_chown
    module.activate_shadow(identity)
    assert b"ASTRA_RFB_LISTEN_ADDRESS=127.0.0.1" in (
        module.SYSTEMD / "astra-remote-desktop.service").read_bytes()
    artifact.write_bytes(b"[Service]\nEnvironment=ASTRA_RFB_LISTEN_ADDRESS=127.0.0.1\n")
    original_chown = os.fchown
    os.fchown = lambda *_: None
    try:
        invalid = module.shadow_release(
            [(artifact, "systemd/astra-remote-desktop.service",
              hashlib.sha256(artifact.read_bytes()).hexdigest())])
    finally:
        os.fchown = original_chown
    try:
        module.activate_shadow(invalid)
    except ValueError:
        pass
    else:
        raise AssertionError("malformed unit activated")
    assert (module.DEV / "current").resolve() == module.DEV / "releases" / identity

with tempfile.TemporaryDirectory() as directory:
    runtime = Path(directory) / "runtime.lock"
    runtime_fd = os.open(runtime, os.O_RDWR | os.O_CREAT, 0o600)
    fcntl.flock(runtime_fd, fcntl.LOCK_EX)
    module.LOCK = Path(directory) / module.LOCK.name
    module.validate = lambda *_: ([], [], [])
    module.run = lambda *_: ""
    module.wait_for_boot = lambda: None
    module.prune_shadows = lambda: None
    try:
        module.install(Path(directory), [], [], [])
    finally:
        fcntl.flock(runtime_fd, fcntl.LOCK_UN)
        os.close(runtime_fd)

with tempfile.TemporaryDirectory() as directory:
    first_stopped = threading.Event()
    release_first = threading.Event()
    calls = []
    failures = []

    def fake_run(*args):
        if args[:2] == ("systemctl", "stop"):
            calls.append(("stop", threading.current_thread().name))
            if threading.current_thread().name == "first-install":
                first_stopped.set()
                assert release_first.wait(5), "first installer did not resume"
        elif args[:2] == ("systemctl", "start"):
            calls.append(("start", threading.current_thread().name))
        return ""

    module.LOCK = Path(directory) / "dev-install.lock"
    module.validate = lambda *_: ([], [], [])
    module.run = fake_run
    module.wait_for_boot = lambda: None
    module.prune_shadows = lambda: None

    def first_install():
        try:
            module.install(Path(directory), [], [], [])
        except Exception as error:
            failures.append(error)

    first = threading.Thread(target=first_install, name="first-install")
    first.start()
    assert first_stopped.wait(5), "first installer did not stop services"
    try:
        try:
            module.install(Path(directory), [], [], [])
        except BlockingIOError:
            pass
        else:
            raise AssertionError("second installer entered active transaction")
    finally:
        release_first.set()
        first.join(5)
    assert not first.is_alive() and not failures
    assert calls == [("stop", "first-install"), ("start", "first-install")], calls

print("DE25 development installer tests: PASS")
