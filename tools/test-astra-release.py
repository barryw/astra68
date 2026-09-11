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

    second_source = store / "second-source"
    second_identity = release.create(
        second_source, ["second=%s" % (sources / "qemu")])
    release.install(second_source, store)
    assert (store / "current").resolve() == \
        (store / "releases" / second_identity).resolve()
    assert (store / "previous").resolve() == installed.resolve()

    third_source = store / "third-source"
    third_identity = release.create(
        third_source, ["third=%s" % (sources / "image")])
    release.install(third_source, store)
    assert (store / "current").resolve() == \
        (store / "releases" / third_identity).resolve()
    assert (store / "previous").resolve() == \
        (store / "releases" / second_identity).resolve()

    state = store / "state"
    for release_identity in (identity, old_identity, second_identity,
                             third_identity):
        (state / release_identity).mkdir(parents=True)
        (state / release_identity / "storage.img").write_bytes(b"state")
    assert release.prune(store) == (1, 1)
    assert not (store / "releases" / old_identity).exists()
    assert not (state / old_identity).exists()
    for retained in (identity, second_identity, third_identity):
        assert (store / "releases" / retained).is_dir()
        assert (state / retained).is_dir()
    assert (store / "by-boot/test").resolve() == installed.resolve()

    for path in [store] + list(store.rglob("*")):
        if not path.is_symlink():
            path.chmod(path.stat().st_mode | stat.S_IWUSR)

print("Astra release integrity: PASS")
