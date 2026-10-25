#!/usr/bin/env python3
"""Stopped-image release refresh keeps user files and rejects redirected roots."""

import importlib.util
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import sys
import tempfile


module_path = Path(__file__).with_name("migrate-system-image.py")
spec = importlib.util.spec_from_file_location("astra_system_migration", module_path)
migration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(migration)

with tempfile.TemporaryDirectory(prefix="astra-migration-import-test-") as tmp:
    isolated = Path(tmp)
    shutil.copy2(module_path, isolated / module_path.name)
    shutil.copy2(module_path.with_name("storage_image.py"),
                 isolated / "storage_image.py")
    result = subprocess.run([sys.executable, str(isolated / module_path.name)],
                            capture_output=True, text=True)
    assert result.returncode != 0 and "usage:" in result.stderr
    assert not (isolated / "__pycache__").exists()


def seed(root, release):
    for tree in migration.SYSTEM_TREES:
        (root / tree).mkdir(parents=True)
    for name in migration.SYSTEM_FILES:
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(release + ":" + name)
    (root / "commands" / release).write_text(release)
    (root / "services/hostfs").write_text(release)
    (root / "libs/Filesystem.kit").write_text(release)
    (root / "apps/Atlas.app").write_text(release)
    (root / "fonts/system/system.afnt").write_text(release)
    (root / "fonts/installed").mkdir()
    (root / "fonts/installed/user.afnt").write_text(release + " font")
    (root / "config/services").mkdir(parents=True)
    (root / "config/services/hostfs.conf").write_text(release + " settings")
    (root / "home").mkdir()
    (root / "home/user.txt").write_text(release + " user")
    (root / "local").mkdir()
    (root / "local/app").write_text(release + " local")
    (root / "media/user.wav").write_text(release + " media")


def image(seed_dir, image_path):
    volume = image_path.with_suffix(".ext4")
    with volume.open("wb") as handle:
        handle.truncate(32 << 20)
    subprocess.run(["mkfs.ext4", "-q", "-F", "-O", "^orphan_file", "-d",
                    str(seed_dir), str(volume)], check=True)
    offset = 1 << 20
    length = volume.stat().st_size
    header = bytearray(512)
    header[446 + 4] = 0x83
    struct.pack_into("<II", header, 446 + 8, offset // 512, length // 512)
    header[510:512] = b"\x55\xaa"
    with image_path.open("wb") as target, volume.open("rb") as source:
        target.write(header)
        target.seek(offset)
        shutil.copyfileobj(source, target)
    volume.unlink()


if os.geteuid() != 0:
    raise SystemExit("run image migration test as root")

with tempfile.TemporaryDirectory(prefix="astra-image-migration-test-") as tmp:
    root = Path(tmp)
    source_seed = root / "release-seed"
    target_seed = root / "state-seed"
    source_seed.mkdir()
    target_seed.mkdir()
    seed(source_seed, "new")
    seed(target_seed, "old")
    source_image = root / "release.img"
    target_image = root / "state.img"
    image(source_seed, source_image)
    image(target_seed, target_image)
    migration.main(source_image, target_image)
    point = root / "check"
    point.mkdir()
    subprocess.run(["mount", "-t", "ext4", "-o",
                    "ro,noload,loop,offset=1048576,sizelimit=33554432",
                    str(target_image), str(point)], check=True)
    try:
        assert (point / "services/hostfs").read_text() == "new", repr(
            (point / "services/hostfs").read_text())
        assert (point / "commands/new").read_text() == "new"
        assert not (point / "commands/old").exists()
        assert (point / "fonts/system/system.afnt").read_text() == "new"
        assert (point / "fonts/installed/user.afnt").read_text() == "old font"
        assert (point / "config/services/hostfs.conf").read_text() == "old settings"
        assert (point / "home/user.txt").read_text() == "old user"
        assert (point / "local/app").read_text() == "old local"
        assert (point / "media/user.wav").read_text() == "old media"
        for name in migration.SYSTEM_FILES:
            assert (point / name).read_text() == "new:" + name
    finally:
        subprocess.run(["umount", str(point)], check=True)

    redirected = root / "redirected-seed"
    shutil.copytree(target_seed, redirected)
    shutil.rmtree(redirected / "commands")
    (redirected / "commands").symlink_to("/etc")
    redirected_image = root / "redirected.img"
    image(redirected, redirected_image)
    try:
        migration.main(source_image, redirected_image)
    except RuntimeError as error:
        assert "not a directory" in str(error)
    else:
        raise AssertionError("redirected system tree accepted")
    assert stat.S_ISREG(target_image.stat().st_mode)

print("system image migration: PASS")
