#!/usr/bin/env python3
"""Install selected guest files into stopped DE25 state; keep changes for the next fix."""

import errno
import fcntl
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import shutil
import stat
import subprocess
import sys
import tempfile
import time

from storage_image import ext4_partition


STORE = Path("/var/lib/astra")
STATE = STORE / "state/system/storage-terminal.img"
CONSOLE = STORE / "log/qemu-console.log"
QMP = Path("/run/astra/qemu/qmp.sock")
LOCK = Path("/run/astra/dev-install.lock")
SYSTEMD = Path("/etc/systemd/system")
SERVICES = ("astra.service", "astra-audio-host.service",
            "astra-remote-desktop.service")
OWNED = ("commands", "services", "libs", "apps", "fonts")
DEV = STORE / "dev"
DROPINS = {
    "astra.service": ("ASTRA_ROOT", "bin/run-arty.sh"),
    "astra-audio-host.service": (None, "bin/astra-audio-host"),
    "astra-remote-desktop.service": (None, "bin/astra-remote-desktop"),
}


def run(*args):
    result = subprocess.run(args, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError("%s: %s" % (" ".join(args),
                           (result.stderr or result.stdout).strip()))
    return result.stdout.strip()


def file_hash(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def parts(path, absolute=False):
    if not isinstance(path, str) or not path or "\x00" in path:
        raise ValueError("empty or invalid path")
    if path.startswith("/") != absolute:
        raise ValueError("unexpected path form: " + path)
    components = path.split("/")[1:] if absolute else path.split("/")
    if any(part in ("", ".", "..") for part in components):
        raise ValueError("unsafe path: " + path)
    return components


def guest_target(path):
    components = parts(path, absolute=True)
    if len(components) < 2 or components[0] not in OWNED:
        if path not in ("/astra_events.cat", "/startup/system",
                        "/media/startup.pcm"):
            raise ValueError("not release-owned guest file: " + path)
    if components[0] == "fonts" and components[1] != "system":
        raise ValueError("font target must be under /fonts/system")
    if components[0] == "libs" and len(components) == 2 and \
            re.fullmatch(r"[A-Za-z0-9_.-]+\.library\.[0-9]+", components[1]):
        raise ValueError("library must be installed inside its Kit")
    return components


def host_target(path):
    components = parts(path)
    if not (len(components) >= 2 and components[0] in ("bin", "qemu", "rom")) and \
            not (len(components) == 2 and components[0] == "systemd" and
                 components[1] in DROPINS):
        raise ValueError("host target must be a release file: " + path)
    return components


def regular(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode):
        raise ValueError("expected regular file: " + str(path))
    return info


def validate(stage, guest, host, remove_guest):
    if stage.parent != Path("/var/tmp") or not stage.name.startswith("astra-dev."):
        raise ValueError("stage must be /var/tmp/astra-dev.*")
    info = stage.lstat()
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != 0 or info.st_mode & 0o077:
        raise ValueError("stage must be a private root-owned directory")
    if not isinstance(guest, list) or not isinstance(host, list) or \
            not isinstance(remove_guest, list) or \
            not guest + host + remove_guest or \
            len(guest) + len(host) + len(remove_guest) > 64:
        raise ValueError("expected 1-64 file mappings")
    seen = set()
    validated_guest = []
    validated_host = []
    validated_remove = []
    for kind, mapping in [("guest", item) for item in guest] + \
                         [("host", item) for item in host] + \
                         [("remove_guest", item) for item in remove_guest]:
        if not isinstance(mapping, dict) or set(mapping) != {"source", "target"}:
            raise ValueError("invalid mapping")
        source_parts = parts(mapping["source"])
        if len(source_parts) != 2 or source_parts[0] != "files":
            raise ValueError("source must be staged under files/")
        source = stage.joinpath(*source_parts)
        source_info = regular(source)
        if source_info.st_uid != 0 or source_info.st_size == 0 or source_info.st_size > 128 << 20:
            raise ValueError("invalid staged artifact: " + str(source))
        destination = mapping["target"]
        guest_target(destination) if kind != "host" else host_target(destination)
        key = ("guest" if kind == "remove_guest" else kind, destination)
        if key in seen:
            raise ValueError("duplicate target: " + destination)
        seen.add(key)
        (validated_guest if kind == "guest" else validated_host
         if kind == "host" else validated_remove).append(
            (source, destination, file_hash(source)))
    for _, destination, _ in validated_guest:
        components = parts(destination, absolute=True)
        if len(components) != 7 or components[0] != "libs" or \
                not components[1].endswith(".kit") or \
                components[2] != "libraries" or \
                not components[4].startswith("abi-") or \
                components[5] != "m68k-68040" or \
                components[3] != components[6]:
            continue
        provider = "/libs/.providers/%s.%s" % (
            components[3], components[4])
        manifest = "/libs/%s/manifest" % components[1]
        if ("guest", provider) not in seen or ("guest", manifest) not in seen:
            raise ValueError("Kit library update needs manifest and provider index: " + destination)
    state_info = regular(STATE)
    if state_info.st_uid != 0 or state_info.st_size > 4 << 30:
        raise ValueError("invalid persistent system image")
    return validated_guest, validated_host, validated_remove


def checked_destination(root, name, create=True):
    path = root
    for component in guest_target(name)[:-1]:
        path /= component
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            if not create:
                raise
            path.mkdir(mode=0o755)
            continue
        if not stat.S_ISDIR(mode):
            raise ValueError("guest path crosses non-directory: " + name)
    target = path / PurePosixPath(name).name
    try:
        regular(target)
    except FileNotFoundError:
        if not create:
            raise
    return target


def remove_file(root, target, expected):
    if len(target.relative_to(root).parts) < 2:
        raise ValueError("guest removal must stay below a release-owned directory")
    regular(target)
    if file_hash(target) != expected:
        raise RuntimeError("guest removal digest mismatch: " + str(target))
    target.unlink()
    directory = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)
    parent = target.parent
    while parent.parent != root:
        try:
            parent.rmdir()
        except OSError as error:
            if error.errno in (errno.ENOTEMPTY, errno.EEXIST):
                break
            raise
        directory = os.open(parent.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        parent = parent.parent


def install_file(source, target, expected, readonly=False):
    source_info = regular(source)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".astra-dev-",
                                         delete=False) as output:
            temporary = Path(output.name)
            with source.open("rb") as stream:
                shutil.copyfileobj(stream, output)
            os.fchown(output.fileno(), 0, 0)
            mode = stat.S_IMODE(source_info.st_mode) & ~0o022
            os.fchmod(output.fileno(), mode & ~0o222 if readonly else mode)
            output.flush()
            os.fsync(output.fileno())
        if file_hash(temporary) != expected:
            raise RuntimeError("staged artifact changed during installation")
        temporary.replace(target)
        directory = os.open(target.parent, os.O_RDONLY | os.O_DIRECTORY)
        try:
            os.fsync(directory)
        finally:
            os.close(directory)
        if file_hash(target) != expected:
            raise RuntimeError("installed artifact digest mismatch: " + str(target))
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def shadow_release(files):
    source = DEV / "current" if (DEV / "current").is_symlink() else STORE / "current"
    source = source.resolve(strict=True)
    original_manifest = (source / "artifacts.sha256").read_text().splitlines()
    known = {line.split("  ", 2)[2] for line in original_manifest}
    for _artifact, name, _digest in files:
        if name not in known:
            raise ValueError("host target absent from selected release: " + name)
        regular(source / name)
    releases = DEV / "releases"
    releases.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".shadow-", dir=DEV) as temporary:
        shadow = Path(temporary) / "release"
        shutil.copytree(source, shadow, copy_function=os.link)
        records = {}
        for artifact, name, digest in files:
            install_file(artifact, shadow / name, digest, readonly=True)
            mode = "x" if artifact.stat().st_mode & 0o111 else "-"
            records[name] = f"{digest}  {mode}  {name}"
        lines = [records.get(line.split("  ", 2)[2], line)
                 for line in original_manifest]
        payload = ("\n".join(lines) + "\n").encode("ascii")
        manifest = shadow / "artifacts.sha256"
        with tempfile.NamedTemporaryFile(dir=shadow, prefix=".manifest-",
                                         delete=False) as output:
            temporary_manifest = Path(output.name)
            output.write(payload)
            output.flush()
            os.fsync(output.fileno())
        temporary_manifest.chmod(0o444)
        temporary_manifest.replace(manifest)
        identity = hashlib.sha256(payload).hexdigest()
        final = releases / identity
        if not final.exists():
            shadow.rename(final)
        return identity


def dev_unit(source, environment, executable, dev_root):
    lines = source.splitlines()
    starts = [index for index, line in enumerate(lines)
              if line.startswith("ExecStart=")]
    if lines.count("[Service]") != 1 or len(starts) != 1:
        raise ValueError("unexpected service unit")
    lines[starts[0]] = f"ExecStart={dev_root}/current/{executable}"
    if environment:
        lines.insert(lines.index("[Service]") + 1,
                     f"Environment={environment}={dev_root}/current")
    return "\n".join(lines) + "\n"


def activate_shadow(identity):
    DEV.mkdir(parents=True, exist_ok=True)
    units = {}
    for unit, (environment, executable) in DROPINS.items():
        source = DEV / "releases" / identity / "systemd" / unit
        units[unit] = dev_unit(source.read_text(), environment, executable, DEV)
    link = DEV / (".current." + str(os.getpid()))
    link.symlink_to("releases/" + identity)
    link.replace(DEV / "current")
    for unit, value in units.items():
        directory = SYSTEMD
        target = directory / unit
        with tempfile.NamedTemporaryFile(dir=directory, prefix=".astra-dev-",
                                         delete=False) as output:
            temporary = Path(output.name)
            output.write(value.encode("ascii"))
            output.flush()
            os.fsync(output.fileno())
        temporary.chmod(0o644)
        temporary.replace(target)
        (directory / (unit + ".d") / "90-astra-dev.conf").unlink(missing_ok=True)
    run("systemctl", "daemon-reload")


def running_qemu():
    group = run("systemctl", "show", "-p", "ControlGroup", "--value",
                "astra.service")
    if not group.startswith("/"):
        return False
    processes = Path("/sys/fs/cgroup" + group + "/cgroup.procs")
    if not processes.is_file():
        return False
    release = DEV / "current" if (DEV / "current").is_symlink() else STORE / "current"
    expected = release.resolve() / "qemu/bin/qemu-system-m68k-astra"
    for pid in processes.read_text().splitlines():
        try:
            if Path("/proc").joinpath(pid, "exe").resolve() == expected:
                return True
        except (FileNotFoundError, PermissionError):
            pass
    return False


def wait_for_boot(timeout=120):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        state = run("systemctl", "show", "-p", "ActiveState", "--value",
                    "astra.service")
        if state == "activating":
            time.sleep(0.5)
            continue
        if state != "active":
            raise RuntimeError("Astra service exited after development install")
        try:
            if running_qemu() and QMP.is_socket():
                data = CONSOLE.read_bytes()
                if b"KERNEL PANIC" in data:
                    raise RuntimeError("kernel panic after development install")
                if b"System degraded:" in data or b"Initial image ....... EXITED" in data:
                    raise RuntimeError("Astra boot degraded after development install")
                # Supervisor reports this only after required boot services
                # (including Desktop) are ready; event logs can drain later.
                if b"Initial image ....... stage 8\n" in data:
                    run("systemctl", "is-active", "astra-remote-desktop.service")
                    run("systemctl", "is-active", "astra-audio-host.service")
                    return
        except FileNotFoundError:
            pass
        time.sleep(0.5)
    raise RuntimeError("Astra did not reach required boot-service readiness")


def prune_shadows():
    current = (DEV / "current").resolve() if (DEV / "current").is_symlink() else None
    releases = DEV / "releases"
    if current is None or not releases.is_dir():
        return
    for child in releases.iterdir():
        if child != current and stat.S_ISDIR(child.lstat().st_mode):
            shutil.rmtree(child)


def install(stage, guest, host, remove_guest):
    files, host_files, removals = validate(stage, guest, host, remove_guest)
    lock = os.open(LOCK, os.O_RDWR | os.O_CREAT, 0o600)
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        shadow = shadow_release(host_files) if host_files else None
        run("systemctl", "stop", *SERVICES)
        mounted = False
        try:
            if run("losetup", "-j", str(STATE)):
                raise RuntimeError("system image still leased after service stop")
            if files or removals:
                offset, length = ext4_partition(STATE)
                with tempfile.TemporaryDirectory(prefix="astra-dev-mount-", dir="/var/tmp") as mount:
                    root = Path(mount)
                    run("mount", "-t", "ext4", "-o",
                        f"loop,rw,offset={offset},sizelimit={length}", str(STATE), mount)
                    mounted = True
                    try:
                        for source, name, digest in files:
                            install_file(source, checked_destination(root, name), digest)
                        for _source, name, digest in removals:
                            remove_file(root,
                                        checked_destination(root, name, create=False),
                                        digest)
                        run("sync", "-f", mount)
                    finally:
                        run("umount", mount)
                        mounted = False
                run("sync", "-f", str(STATE))
            if shadow:
                activate_shadow(shadow)
        finally:
            if not mounted:
                run("systemctl", "start", *SERVICES)
        wait_for_boot()
        prune_shadows()
        print("ASTRA_DE25_DEV_INSTALL PASS guest=%d host=%d removed=%d%s" %
              (len(files), len(host_files), len(removals),
               " shadow=" + shadow if shadow else ""))
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        os.close(lock)


if __name__ == "__main__":
    if os.geteuid() != 0 or len(sys.argv) != 2:
        raise SystemExit("usage: dev-install-de25.py STAGE (root on DE25)")
    stage = Path(sys.argv[1])
    data = json.loads((stage / "manifest.json").read_text())
    if not isinstance(data, dict) or set(data) != {"guest", "host", "remove_guest"}:
        raise SystemExit("invalid development manifest")
    install(stage, data["guest"], data["host"], data["remove_guest"])
