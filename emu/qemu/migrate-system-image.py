#!/usr/bin/env python3
"""Refresh release-owned files in a stopped Astra image, retaining user state."""

import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from storage_image import ext4_partition


# Match astra_image._install_built: these trees are replaced on publication.
# /config, /home, /local, /fonts/installed and other user paths are untouched.
SYSTEM_TREES = ("commands", "services", "libs", "apps", "fonts/system")
SYSTEM_FILES = ("astra_events.cat", "startup/system", "media/startup.pcm")


def directory(root, relative, create=False):
    path = root
    for component in relative.split("/"):
        path = path / component
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            if not create:
                raise RuntimeError("missing image directory: " + relative)
            path.mkdir(mode=0o755)
            continue
        if not stat.S_ISDIR(mode):
            raise RuntimeError("image directory is not a directory: " + relative)
    return path


def replace_file(source, target):
    source_info = source.lstat()
    if not stat.S_ISREG(source_info.st_mode):
        raise RuntimeError("release file is not regular: " + str(source))
    try:
        target_info = target.lstat()
    except FileNotFoundError:
        target_info = None
    if target_info is not None and stat.S_ISDIR(target_info.st_mode):
        raise RuntimeError("system file conflicts with directory: " + str(target))
    with tempfile.NamedTemporaryFile(dir=target.parent, prefix=".astra-system-",
                                     delete=False) as handle:
        temporary = Path(handle.name)
        try:
            with source.open("rb") as stream:
                shutil.copyfileobj(stream, handle)
            os.fchown(handle.fileno(), source_info.st_uid, source_info.st_gid)
            os.fchmod(handle.fileno(), stat.S_IMODE(source_info.st_mode))
            handle.flush()
            os.fsync(handle.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def refresh(source, target):
    for tree in SYSTEM_TREES:
        origin = directory(source, tree)
        destination = directory(target, tree, create=True)
        subprocess.run(["rsync", "-a", "--checksum", "--no-times", "--delete",
                        "--one-file-system", "--",
                        str(origin) + "/", str(destination) + "/"], check=True)
    for name in SYSTEM_FILES:
        parent = name.rpartition("/")[0]
        if parent:
            directory(source, parent)
            directory(target, parent, create=True)
        replace_file(source / name, target / name)


def image(path):
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_uid != 0 or info.st_size > 4 << 30:
        raise RuntimeError("image must be root-owned regular file of at most 4 GiB")
    return ext4_partition(str(path))


def main(source_image, target_image):
    if os.geteuid() != 0:
        raise RuntimeError("image migration requires root")
    source_offset, source_length = image(source_image)
    target_offset, target_length = image(target_image)
    with tempfile.TemporaryDirectory(prefix="astra-image-migrate-", dir="/var/tmp") as temporary:
        source = Path(temporary) / "release"
        target = Path(temporary) / "state"
        source.mkdir()
        target.mkdir()
        mounted = []
        try:
            for path, point, offset, length, access in (
                    (source_image, source, source_offset, source_length, "ro,noload"),
                    (target_image, target, target_offset, target_length, "rw")):
                subprocess.run(["mount", "-t", "ext4", "-o",
                                "loop," + access + ",offset=" + str(offset) +
                                ",sizelimit=" + str(length), str(path), str(point)], check=True)
                mounted.append(point)
            refresh(source, target)
            subprocess.run(["sync", "-f", str(target)], check=True)
        finally:
            for point in reversed(mounted):
                subprocess.run(["umount", str(point)], check=True)
    subprocess.run(["sync", "-f", str(target_image)], check=True)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: migrate-system-image.py RELEASE_IMAGE STOPPED_STATE_IMAGE")
    main(Path(sys.argv[1]), Path(sys.argv[2]))
