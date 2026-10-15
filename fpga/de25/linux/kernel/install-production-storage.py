#!/usr/bin/env python3
"""Stage a complete DE25 kernel; switch its boot script only on explicit arm.

The package owns Image, DTBs and the entire distinctly named module tree.
Staging never replaces the running kernel's modules, runtime selector, fabric,
or selected boot script. Arming publishes one verified script last. The saved
script is sufficient to restore the previous Image/DTB selection because their
files remain untouched.
"""

import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile

DTB = "intel/socfpga_agilex5_de25_nano.dtb"


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            result.update(block)
    return result.hexdigest()


def sync_directory(path):
    descriptor = os.open(path, os.O_DIRECTORY | os.O_RDONLY)
    try:
        os.fsync(descriptor)
    finally:
        os.close(descriptor)


def copy_file(source, target):
    if source.is_symlink() or not source.is_file():
        raise RuntimeError("kernel input is not a regular file: %s" % source)
    with source.open("rb") as reader, target.open("xb") as writer:
        shutil.copyfileobj(reader, writer)
        writer.flush()
        os.fsync(writer.fileno())


def verified_package(kernel):
    package = kernel / "package"
    manifest = package / "SHA256SUMS"
    identity = digest(manifest)
    if (kernel / "boot.identity").read_text().strip() != identity:
        raise RuntimeError("boot script belongs to another kernel package")
    release = (package / "metadata/kernel.release").read_text().strip()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]*", release):
        raise RuntimeError("invalid kernel release")
    records = {}
    for line in manifest.read_text(encoding="ascii").splitlines():
        value, separator, relative = line.partition("  ")
        path = PurePosixPath(relative)
        if (not re.fullmatch(r"[0-9a-f]{64}", value) or not separator or
                path.is_absolute() or relative != path.as_posix() or
                any(part in (".", "..") for part in path.parts) or
                not relative or relative in records):
            raise RuntimeError("invalid kernel manifest entry")
        records[relative] = value
    actual = set()
    for parent, directories, files in os.walk(package, followlinks=False):
        for name in directories + files:
            entry = Path(parent) / name
            if entry.is_symlink():
                raise RuntimeError("kernel package contains symlink: %s" % entry)
        for name in files:
            entry = Path(parent) / name
            if not entry.is_file():
                raise RuntimeError("kernel package contains non-regular file")
            actual.add(entry.relative_to(package).as_posix())
    if actual != set(records) | {"SHA256SUMS"}:
        raise RuntimeError("kernel package inventory differs from manifest")
    required = ("boot/Image", "boot/dtbs/" + DTB,
                "lib/modules/%s/kernel/drivers/scsi/sr_mod.ko" % release,
                "lib/modules/%s/kernel/drivers/cdrom/cdrom.ko" % release,
                "lib/modules/%s/extra/astra/astra_graphics_arena.ko" % release,
                "lib/modules/%s/extra/astra/astra_display_capture.ko" % release,
                "lib/modules/%s/modules.dep" % release)
    if any(name not in records for name in required):
        raise RuntimeError("incomplete optical/graphics kernel package")
    for name, value in records.items():
        if digest(package / name) != value:
            raise RuntimeError("kernel package hash mismatch: %s" % name)
    return package, identity, release, records


def verify_modules(target, release, records):
    prefix = "lib/modules/%s/" % release
    expected = {name[len(prefix):]: value for name, value in records.items()
                if name.startswith(prefix)}
    actual = {}
    for parent, directories, files in os.walk(target, followlinks=False):
        for name in directories + files:
            if (Path(parent) / name).is_symlink():
                raise RuntimeError("installed modules contain symlink")
        for name in files:
            entry = Path(parent) / name
            actual[entry.relative_to(target).as_posix()] = digest(entry)
    if actual != expected:
        raise RuntimeError("installed kernel module tree differs from package")


def managed_boots(boot):
    result = {}
    directory = boot / "astra-kernels"
    if not directory.exists():
        return result
    for entry in directory.iterdir():
        if not re.fullmatch(r"[0-9a-f]{64}", entry.name):
            continue
        if entry.is_symlink() or not entry.is_dir():
            raise RuntimeError("invalid managed kernel boot directory")
        record = json.loads((entry / "kernel.json").read_text())
        if (record["identity"] != entry.name or
                not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]*", record["release"])):
            raise RuntimeError("invalid managed kernel metadata")
        if set(path.name for path in entry.iterdir()) != set(record["boot"]) | {"kernel.json"}:
            raise RuntimeError("managed kernel boot inventory changed")
        for name, value in record["boot"].items():
            if name not in ("Image", "socfpga_agilex5_de25_nano.dtb", "boot.scr.uimg") or digest(entry / name) != value:
                raise RuntimeError("managed kernel boot content changed")
        result[entry.name] = record
    return result


def previous_state(state):
    return state.with_name(state.name + ".previous")


def read_state(path):
    record = json.loads(path.read_text())
    if (any(not re.fullmatch(r"[0-9a-f]{64}", record[name])
            for name in ("identity", "previous", "candidate")) or
            not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.+-]*", record["release"])):
        raise RuntimeError("invalid kernel boot transaction metadata")
    return record


def prune_kernels(root, boot, identity, state=None):
    records = managed_boots(boot)
    selected = digest(boot / "boot.scr.uimg")
    kept_scripts = {selected}
    kept = {identity}
    if state is not None:
        for marker in (state, previous_state(state)):
            if marker.exists():
                record = read_state(marker)
                kept.add(record["identity"])
                kept_scripts.add(record["previous"])
    kept.update(name for name, record in records.items()
                if record["boot"]["boot.scr.uimg"] in kept_scripts)
    kept_releases = {records[name]["release"] for name in kept if name in records}
    kept_releases.add(os.uname().release)
    for name, record in records.items():
        if name in kept:
            continue
        modules = root / "lib/modules" / record["release"]
        if record["release"] not in kept_releases and modules.exists():
            # Delete only an exact managed tree. Extra or changed files refuse
            # retirement rather than deleting a manually installed module set.
            expected = {"lib/modules/%s/%s" % (record["release"], relative): value
                        for relative, value in record["modules"].items()}
            verify_modules(modules, record["release"], expected)
            shutil.rmtree(modules)
            sync_directory(modules.parent)
        shutil.rmtree(boot / "astra-kernels" / name)
    for entry in (boot / "astra-kernels").iterdir():
        if re.fullmatch(r"rollback-[0-9a-f]{64}", entry.name) and entry.name[9:] not in kept_scripts:
            entry.unlink()
    sync_directory(boot / "astra-kernels")


def stage(kernel, root, boot, state=None):
    package, identity, release, records = verified_package(kernel)
    modules = root / "lib/modules"
    modules.mkdir(parents=True, exist_ok=True)
    destination = modules / release
    if destination.is_symlink():
        raise RuntimeError("module destination is a symbolic link")
    if destination.exists():
        verify_modules(destination, release, records)
    else:
        temporary = Path(tempfile.mkdtemp(prefix=".astra-kernel-", dir=modules))
        try:
            source = package / "lib/modules" / release
            for name, value in records.items():
                prefix = "lib/modules/%s/" % release
                if not name.startswith(prefix):
                    continue
                relative = name[len(prefix):]
                target = temporary / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                copy_file(source / relative, target)
                target.chmod(0o644)
            verify_modules(temporary, release, records)
            for parent, _dirs, _files in os.walk(temporary, topdown=False):
                Path(parent).chmod(0o755)
                sync_directory(Path(parent))
            os.rename(temporary, destination)
            sync_directory(modules)
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    kernels = boot / "astra-kernels"
    kernels.mkdir(exist_ok=True)
    target = kernels / identity
    boot_inputs = {"Image": package / "boot/Image",
                   "socfpga_agilex5_de25_nano.dtb": package / "boot/dtbs" / DTB,
                   "boot.scr.uimg": kernel / "boot.scr.uimg"}
    prefix = "lib/modules/%s/" % release
    metadata = {"identity": identity, "release": release,
                "boot": {name: digest(source) for name, source in boot_inputs.items()},
                "modules": {name[len(prefix):]: value for name, value in records.items()
                            if name.startswith(prefix)}}
    if target.is_symlink():
        raise RuntimeError("boot destination is a symbolic link")
    if not target.exists():
        temporary = Path(tempfile.mkdtemp(prefix=".astra-kernel-", dir=kernels))
        try:
            for name, source in boot_inputs.items():
                copy_file(source, temporary / name)
            with (temporary / "kernel.json").open("x") as handle:
                json.dump(metadata, handle, sort_keys=True)
                handle.write("\n")
                handle.flush()
                os.fsync(handle.fileno())
            sync_directory(temporary)
            os.rename(temporary, target)
            sync_directory(kernels)
        finally:
            if temporary.exists():
                shutil.rmtree(temporary)
    for name, source in boot_inputs.items():
        if digest(target / name) != digest(source):
            raise RuntimeError("staged boot file differs: %s" % name)
    if json.loads((target / "kernel.json").read_text()) != metadata:
        raise RuntimeError("staged kernel metadata differs")
    prune_kernels(root, boot, identity, state)
    return identity, release, target


def atomic_file(source, destination):
    temporary = destination.with_name(".%s.astra-new" % destination.name)
    try:
        copy_file(source, temporary)
        if digest(temporary) != digest(source):
            raise RuntimeError("boot selector copy differs")
        os.replace(temporary, destination)
        sync_directory(destination.parent)
    finally:
        if temporary.exists():
            temporary.unlink()


def arm(kernel, root, boot, state):
    identity, release, target = stage(kernel, root, boot, state)
    selected = boot / "boot.scr.uimg"
    current = digest(selected)
    candidate = digest(target / "boot.scr.uimg")
    if state.exists():
        previous = read_state(state)
        if previous["identity"] != identity:
            raise RuntimeError("another kernel is armed; confirm or roll it back first")
        if current not in (previous["previous"], candidate):
            raise RuntimeError("selected boot script changed outside kernel transaction")
    else:
        backup = boot / "astra-kernels" / ("rollback-" + current)
        if not backup.exists():
            atomic_file(selected, backup)
        if digest(backup) != current:
            raise RuntimeError("boot rollback copy differs")
        state.parent.mkdir(parents=True, exist_ok=True)
        temporary = state.with_name(".%s.astra-new" % state.name)
        with temporary.open("x") as handle:
            json.dump({"identity": identity, "release": release,
                       "previous": current, "candidate": candidate}, handle)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temporary, state)
        sync_directory(state.parent)
    atomic_file(target / "boot.scr.uimg", selected)
    return identity, release


def rollback(boot, state):
    if not state.exists():
        state = previous_state(state)
    previous = read_state(state)
    selected = boot / "boot.scr.uimg"
    if digest(selected) not in (previous["previous"], previous["candidate"]):
        raise RuntimeError("selected boot script changed outside kernel transaction")
    backup = boot / "astra-kernels" / ("rollback-" + previous["previous"])
    if digest(backup) != previous["previous"]:
        raise RuntimeError("boot rollback copy differs")
    atomic_file(backup, selected)
    state.unlink()
    sync_directory(state.parent)


def verify_running(kernel, root, boot, running_release):
    package, identity, release, records = verified_package(kernel)
    if running_release != release:
        raise RuntimeError("running kernel differs from armed candidate")
    verify_modules(root / "lib/modules" / release, release, records)
    staged = managed_boots(boot).get(identity)
    if staged is None or staged["release"] != release:
        raise RuntimeError("running kernel package was not staged")
    expected_boot = {"Image": digest(package / "boot/Image"),
                     "socfpga_agilex5_de25_nano.dtb": digest(package / "boot/dtbs" / DTB),
                     "boot.scr.uimg": digest(kernel / "boot.scr.uimg")}
    if staged["boot"] != expected_boot or digest(boot / "boot.scr.uimg") != expected_boot["boot.scr.uimg"]:
        raise RuntimeError("selected boot content differs from running kernel package")
    return identity, release


def confirm(kernel, root, boot, state, running_release):
    identity, release = verify_running(kernel, root, boot, running_release)
    if state.exists():
        record = read_state(state)
        if record["identity"] != identity or digest(boot / "boot.scr.uimg") != record["candidate"]:
            raise RuntimeError("accepted kernel differs from armed candidate")
        os.replace(state, previous_state(state))
        sync_directory(state.parent)
    prune_kernels(root, boot, identity, state)
    return identity, release


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("stage", "arm", "rollback", "verify", "confirm"))
    parser.add_argument("kernel", type=Path)
    parser.add_argument("boot", type=Path)
    parser.add_argument("state", type=Path)
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("kernel installation requires root")
    source = subprocess.check_output(["findmnt", "-n", "-o", "SOURCE,FSTYPE", "--mountpoint", str(args.boot)], text=True).strip()
    if source.split() != ["/dev/mmcblk0p1", "vfat"]:
        parser.error("boot mount must be /dev/mmcblk0p1 vfat")
    args.state.parent.mkdir(parents=True, exist_ok=True)
    with args.state.with_suffix(".lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.command == "rollback":
            rollback(args.boot, args.state)
            print("ASTRA_DE25_KERNEL_ROLLBACK PASS")
        elif args.command == "arm":
            identity, release = arm(args.kernel, Path("/"), args.boot, args.state)
            print("ASTRA_DE25_KERNEL_ARMED identity=%s release=%s" % (identity, release))
        elif args.command == "confirm":
            identity, release = confirm(args.kernel, Path("/"), args.boot, args.state, os.uname().release)
            print("ASTRA_DE25_KERNEL_CONFIRMED PASS identity=%s release=%s" % (identity, release))
        elif args.command == "verify":
            identity, release = verify_running(args.kernel, Path("/"), args.boot, os.uname().release)
            print("ASTRA_DE25_KERNEL_RUNNING PASS identity=%s release=%s" % (identity, release))
        else:
            identity, release, _target = stage(args.kernel, Path("/"), args.boot, args.state)
            print("ASTRA_DE25_KERNEL_STAGED identity=%s release=%s" % (identity, release))


if __name__ == "__main__":
    main()
