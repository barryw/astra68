#!/usr/bin/env python3
"""Regression check for immutable, exact Astra runtime releases."""

import importlib.util
import os
from pathlib import Path
import shutil
import stat
import tempfile


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "astra_release", HERE / "astra_release.py")
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


def rejected(call, text):
    try:
        call()
    except RuntimeError as error:
        assert text in str(error), error
    else:
        raise AssertionError("invalid release was accepted")


with tempfile.TemporaryDirectory() as temporary_text:
    temporary = Path(temporary_text)
    sources = temporary / "sources"
    sources.mkdir()
    qemu = sources / "qemu"
    qemu.write_bytes(b"current qemu")
    qemu.chmod(0o755)
    image = sources / "image"
    image.write_bytes(b"current image")

    bundle = temporary / "bundle"
    identity = release.create(bundle, [
        "storage.img=%s" % image,
        "bin/qemu=%s" % qemu,
    ])
    assert release.verify(bundle) == identity
    assert (bundle / release.MANIFEST).read_text().splitlines()[0].endswith(
        "  x  bin/qemu")

    (bundle / "stale").write_bytes(b"old")
    rejected(lambda: release.verify(bundle), "do not exactly match")
    (bundle / "stale").unlink()
    (bundle / "empty-stale").mkdir()
    rejected(lambda: release.verify(bundle), "directories do not exactly")
    (bundle / "empty-stale").rmdir()
    (bundle / "storage.img").write_bytes(b"stale image")
    rejected(lambda: release.verify(bundle), "hash mismatch")
    (bundle / "storage.img").write_bytes(b"current image")
    (bundle / "bin/qemu").chmod(0o644)
    rejected(lambda: release.verify(bundle), "executable mode mismatch")
    (bundle / "bin/qemu").chmod(0o755)
    os.symlink("storage.img", bundle / "old-link")
    rejected(lambda: release.verify(bundle), "symlink")
    (bundle / "old-link").unlink()

    store = temporary / "store"
    incoming = store / "incoming"
    store.mkdir()
    shutil.copytree(bundle, incoming)
    assert release.install(incoming, store) == identity
    installed = store / "releases" / identity
    assert release.verify(store / "current", installed=True) == identity
    assert (installed.stat().st_mode & 0o222) == 0
    assert ((installed / "storage.img").stat().st_mode & 0o222) == 0
    assert (installed / "bin/qemu").stat().st_mode & 0o111
    (installed / "storage.img").chmod(0o644)
    rejected(lambda: release.verify(installed, installed=True),
             "installed release is writable")
    (installed / "storage.img").chmod(0o444)

    bad = store / "bad"
    shutil.copytree(installed, bad)
    for path in [bad] + list(bad.rglob("*")):
        if not path.is_symlink():
            path.chmod(path.stat().st_mode | stat.S_IWUSR)
    (bad / "storage.img").write_bytes(b"wrong")
    rejected(lambda: release.install(bad, store), "hash mismatch")
    assert (store / "current").resolve() == installed.resolve()

    inactive = store / "inactive"
    shutil.copytree(installed, inactive)
    for path in [inactive] + list(inactive.rglob("*")):
        if not path.is_symlink():
            path.chmod(path.stat().st_mode | stat.S_IWUSR)
    assert release.install(inactive, store, activate=False) == identity
    assert (store / "current").resolve() == installed.resolve()
    old_identity = release.create(
        store / "old-source", ["old=%s" % (sources / "image")])
    old_stage = store / "old-source"
    release.install(old_stage, store, activate=False)
    release.select(store, "by-boot/test", old_identity)
    assert (store / "by-boot/test").resolve() == \
        (store / "releases" / old_identity).resolve()
    release.select(store, "by-boot/test", identity)
    assert (store / "by-boot/test").resolve() == installed.resolve()

    for path in [store] + list(store.rglob("*")):
        if not path.is_symlink():
            path.chmod(path.stat().st_mode | stat.S_IWUSR)

print("Astra release integrity: PASS")
