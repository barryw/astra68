#!/usr/bin/env python3
"""Create, verify, and atomically activate exact Astra runtime releases."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile


MANIFEST = "artifacts.sha256"


def _relative_path(text):
    path = PurePosixPath(text)
    if (not text or text != path.as_posix() or path.is_absolute() or
            text == MANIFEST or any(part in ("", ".", "..")
                                    for part in path.parts)):
        raise RuntimeError("invalid release path: %r" % text)
    return path


def _digest(path):
    result = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            result.update(block)
    return result.hexdigest()


def _tree(root):
    files = []
    directories = []
    for parent, names, filenames in os.walk(root, followlinks=False):
        parent_path = Path(parent)
        for name in names:
            path = parent_path / name
            if path.is_symlink():
                raise RuntimeError("release contains symlink: %s" % path)
            if not path.is_dir():
                raise RuntimeError("release contains non-directory: %s" % path)
            directories.append(path.relative_to(root).as_posix())
        for name in filenames:
            path = parent_path / name
            if path.is_symlink() or not path.is_file():
                raise RuntimeError("release contains symlink or non-regular "
                                   "file: %s" % path)
            files.append(path.relative_to(root).as_posix())
    return sorted(files), sorted(directories)


def verify(root, installed=False):
    root = Path(root)
    manifest = root / MANIFEST
    try:
        data = manifest.read_bytes()
        lines = data.decode("ascii").splitlines()
    except (OSError, UnicodeError) as error:
        raise RuntimeError("cannot read release manifest: %s" % error) from error
    entries = []
    for line in lines:
        fields = line.split("  ", 2)
        if (len(fields) != 3 or len(fields[0]) != 64 or
                fields[1] not in ("-", "x") or
                any(character not in "0123456789abcdef"
                    for character in fields[0])):
            raise RuntimeError("invalid release manifest line: %r" % line)
        entries.append((fields[2], fields[0], fields[1]))
    names = [name for name, _digest_text, _mode in entries]
    if not names or names != sorted(names) or len(names) != len(set(names)):
        raise RuntimeError("release manifest is empty, unsorted, or duplicated")
    for name in names:
        _relative_path(name)

    files, directories = _tree(root)
    if files != sorted(names + [MANIFEST]):
        raise RuntimeError("release files do not exactly match the manifest")
    expected_directories = set()
    for name in names:
        parent = PurePosixPath(name).parent
        while parent != PurePosixPath("."):
            expected_directories.add(parent.as_posix())
            parent = parent.parent
    if directories != sorted(expected_directories):
        raise RuntimeError("release directories do not exactly match the manifest")
    for name, expected, mode in entries:
        path = root / name
        actual = _digest(path)
        if actual != expected:
            raise RuntimeError("release hash mismatch for %s" % name)
        if bool(path.stat().st_mode & 0o111) != (mode == "x"):
            raise RuntimeError("release executable mode mismatch for %s" % name)
    if installed:
        paths = [root] + [root / name for name in files + directories]
        if any(path.stat().st_mode & 0o222 for path in paths):
            raise RuntimeError("installed release is writable")
    return hashlib.sha256(data).hexdigest()


def create(destination, specifications):
    destination = Path(destination)
    if destination.exists() or destination.is_symlink():
        raise RuntimeError("release destination already exists: %s" % destination)
    mappings = []
    for specification in specifications:
        if "=" not in specification:
            raise RuntimeError("artifact must be RELEASE_PATH=SOURCE_PATH")
        name, source_text = specification.split("=", 1)
        _relative_path(name)
        source = Path(source_text)
        if source.is_symlink() or not source.is_file():
            raise RuntimeError("artifact is not a regular file: %s" % source)
        mappings.append((name, source))
    names = [name for name, _source in mappings]
    if not names or len(names) != len(set(names)):
        raise RuntimeError("release artifacts are empty or duplicated")

    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=".astra-release-",
                                      dir=destination.parent))
    try:
        records = []
        for name, source in sorted(mappings):
            target = temporary / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
            target.chmod(0o755 if source.stat().st_mode & 0o111 else 0o644)
            records.append("%s  %s  %s\n" % (
                _digest(target), "x" if target.stat().st_mode & 0o111 else "-",
                name))
        (temporary / MANIFEST).write_text("".join(records), encoding="ascii")
        identity = verify(temporary)
        os.replace(temporary, destination)
        return identity
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def _readonly(root):
    files, directories = _tree(root)
    for name in files:
        path = root / name
        path.chmod(0o555 if path.stat().st_mode & 0o111 else 0o444)
    for name in reversed(directories):
        (root / name).chmod(0o555)
    root.chmod(0o555)


def install(stage, store, activate=True):
    stage = Path(stage).resolve()
    store = Path(store).resolve()
    identity = verify(stage)
    releases = store / "releases"
    releases.mkdir(parents=True, exist_ok=True)
    destination = releases / identity
    if destination.exists():
        if verify(destination, installed=True) != identity:
            raise RuntimeError("installed release is corrupt: %s" % destination)
    else:
        if stage.stat().st_dev != releases.stat().st_dev:
            raise RuntimeError("release stage and store must share a filesystem")
        if verify(stage) != identity:
            raise RuntimeError("release changed while being installed")
        os.replace(stage, destination)
        _readonly(destination)
        if verify(destination, installed=True) != identity:
            raise RuntimeError("release changed while being installed")

    if activate:
        temporary = store / (".current.%u" % os.getpid())
        try:
            temporary.symlink_to(Path("releases") / identity)
            os.replace(temporary, store / "current")
        finally:
            if temporary.is_symlink():
                temporary.unlink()
    return identity


def select(store, selector, identity):
    store = Path(store).resolve()
    relative = _relative_path(selector)
    release = store / "releases" / identity
    if verify(release, installed=True) != identity:
        raise RuntimeError("selected release identity does not match")
    selected = store.joinpath(*relative.parts)
    selected.parent.mkdir(parents=True, exist_ok=True)
    if selected.exists() and not selected.is_symlink():
        raise RuntimeError("release selector is not a symbolic link: %s" %
                           selected)
    temporary = selected.parent / (".%s.%u" % (selected.name, os.getpid()))
    if temporary.exists() or temporary.is_symlink():
        raise RuntimeError("temporary release selector already exists: %s" %
                           temporary)
    try:
        temporary.symlink_to(os.path.relpath(release, selected.parent))
        os.replace(temporary, selected)
    finally:
        if temporary.is_symlink():
            temporary.unlink()
    if selected.resolve() != release.resolve():
        raise RuntimeError("release selector did not activate")
    return identity


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    verify_parser = commands.add_parser("verify")
    verify_parser.add_argument("root")
    verify_parser.add_argument("--installed", action="store_true")
    create_parser = commands.add_parser("create")
    create_parser.add_argument("destination")
    create_parser.add_argument("artifacts", nargs="+")
    install_parser = commands.add_parser("install")
    install_parser.add_argument("stage")
    install_parser.add_argument("store")
    install_parser.add_argument("--no-activate", action="store_true")
    select_parser = commands.add_parser("select")
    select_parser.add_argument("store")
    select_parser.add_argument("selector")
    select_parser.add_argument("identity")
    arguments = parser.parse_args()
    if arguments.command == "verify":
        identity = verify(arguments.root, arguments.installed)
    elif arguments.command == "create":
        identity = create(arguments.destination, arguments.artifacts)
    elif arguments.command == "install":
        identity = install(arguments.stage, arguments.store,
                           not arguments.no_activate)
    else:
        identity = select(arguments.store, arguments.selector,
                          arguments.identity)
    print(identity)


if __name__ == "__main__":
    main()
