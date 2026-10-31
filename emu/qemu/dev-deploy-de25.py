#!/usr/bin/env python3
"""Build only requested targets and install their guest files on running DE25."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import stat
import subprocess
import tempfile

import astra_image


REPOSITORY = Path(__file__).resolve().parents[2]
BOARD = "root@192.168.1.52"


def checked_source(name):
    original = REPOSITORY / name
    if original.is_symlink():
        raise ValueError("artifact must not be a symlink: " + name)
    path = original.resolve()
    if not path.is_relative_to(REPOSITORY) or not path.is_file():
        raise ValueError("artifact must be a file inside Astra source: " + name)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_size == 0:
        raise ValueError("artifact must be a nonempty regular file: " + name)
    return path


def kit_companions(source, target):
    """Keep a Kit's manifest and provider record with its changed library."""
    components = source.relative_to(REPOSITORY).parts
    if len(components) < 11 or components[:4] != (
            "sw", "userspace", "kits", "build") or \
            not components[4].endswith(".kit") or components[5] != "libraries":
        return []
    relative = Path(*components[5:]).as_posix()
    expected = "/libs/%s/%s" % (components[4], relative)
    if target != expected:
        raise ValueError("Kit library target must be " + expected)
    abi = components[7]
    if not abi.startswith("abi-") or not abi[4:].isdigit():
        raise ValueError("invalid Kit library ABI path: " + str(source))
    userspace = REPOSITORY / "sw/userspace"
    candidates = list(userspace.glob(
        "*/build/m68k/libraries/" + source.name + "." + abi[4:]))
    candidates += list(userspace.glob(
        "*/build/m68k/" + source.name + "." + abi[4:]))
    if len(candidates) != 1 or not candidates[0].is_file():
        raise ValueError("cannot verify built Kit library: " + str(source))
    with source.open("rb") as staged, candidates[0].open("rb") as built:
        matching = hashlib.file_digest(staged, "sha256").digest() == \
            hashlib.file_digest(built, "sha256").digest()
    if not matching:
        raise ValueError("stale Kit library; rebuild " + components[4] +
                         " before deployment: " + str(source))
    bundle = REPOSITORY.joinpath(*components[:5])
    path = components[4] + "/" + relative
    for (name, abi), (version, abi_minor, build_id, provider_path) in \
            astra_image._providers([str(bundle)]).items():
        if provider_path == path:
            record = astra_image._provider_index_record(
                path, version, abi, abi_minor, build_id)
            return [
                (checked_source(str(bundle.relative_to(REPOSITORY) / "manifest")),
                 "/libs/%s/manifest" % components[4]),
                (record, "/libs/.providers/%s.abi-%u" % (name, abi)),
            ]
    raise ValueError("Kit manifest does not provide changed library: " + str(source))


def validate_application_target(source, target):
    """Install a built app only where its bundle manifest launches it."""
    parts = source.relative_to(REPOSITORY).parts
    if parts[:4] == ("sw", "userspace", "apps", "build") and \
            len(parts) >= 6 and parts[4].endswith(".app"):
        expected = "/apps/" + Path(*parts[4:]).as_posix()
        if target != expected:
            raise ValueError("staged application target must be " + expected)
        return
    direct = len(parts) == 7 and parts[:3] == (
        "sw", "userspace", "applications") and parts[4:6] == (
        "build", "m68k")
    terminal = parts == ("sw", "userspace", "services", "terminal",
                          "build", "m68k", "terminal")
    if not direct and not terminal:
        return
    target_parts = Path(target).parts
    if len(target_parts) < 4 or target_parts[1] != "apps" or \
            not target_parts[2].endswith(".app"):
        raise ValueError("application executable target must be inside /apps/NAME.app")
    bundle = target_parts[2]
    manifest = REPOSITORY / "sw/userspace/apps" / bundle / "manifest"
    if not manifest.is_file():
        raise ValueError("application bundle manifest missing: " + bundle)
    executable = []
    for line in manifest.read_text().splitlines():
        fields = shlex.split(line, comments=True)
        if fields and fields[0] == "executable":
            if len(fields) != 2:
                raise ValueError("invalid application executable in " + bundle)
            executable.append(fields[1])
    if len(executable) != 1 or Path(executable[0]).is_absolute() or \
            ".." in Path(executable[0]).parts:
        raise ValueError("invalid application executable in " + bundle)
    expected = "/apps/" + bundle + "/" + executable[0]
    if target != expected:
        raise ValueError("application executable target must be " + expected)
    normalized = lambda value: "".join(character.lower() for character in value
                                        if character.isalnum())
    if normalized(parts[3]) != normalized(bundle[:-4]) or \
            normalized(source.name) != normalized(Path(executable[0]).name):
        raise ValueError("built application does not match " + bundle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", action="append", default=[], metavar="DIR[:TARGET]")
    parser.add_argument("--guest", action="append", default=[],
                        metavar="SOURCE=/GUEST/PATH")
    parser.add_argument("--remove-guest", action="append", default=[],
                        metavar="SOURCE=/GUEST/PATH")
    parser.add_argument("--host", action="append", default=[],
                        metavar="SOURCE=RELEASE/PATH")
    parser.add_argument("--startup-manifest", action="store_true",
                        help="install current startup policy from astra_image.py")
    parser.add_argument("--board", default=BOARD)
    args = parser.parse_args()
    if not args.guest and not args.host and not args.remove_guest and \
            not args.startup_manifest:
        parser.error("at least one artifact or removal is required")
    if any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.@-"
           for character in args.board):
        parser.error("invalid board address")
    for build in args.make:
        directory, separator, target = build.partition(":")
        path = (REPOSITORY / directory).resolve()
        if not path.is_relative_to(REPOSITORY) or not path.is_dir() or \
                (separator and (not target or target.startswith("-") or
                                any(character.isspace() for character in target))):
            parser.error("invalid make target: " + build)
        command = ["make", "-j4", "-C", str(path)]
        command.append(target if separator else "all")
        subprocess.run(command, check=True)

    with tempfile.TemporaryDirectory(prefix="astra-dev-stage-") as temporary:
        stage = Path(temporary)
        files = stage / "files"
        files.mkdir()
        mappings = []
        targets = set()

        def stage_mapping(kind, source, target):
            key = "guest" if kind == "remove_guest" else kind, target
            if key in targets:
                raise ValueError("duplicate artifact target: " + target)
            targets.add(key)
            name = str(len(mappings))
            staged = files / name
            if isinstance(source, bytes):
                staged.write_bytes(source)
            else:
                shutil.copy2(source, staged)
            mappings.append((kind, {"source": "files/" + name,
                                    "target": target}))

        for kind, item in (
                [("guest", value) for value in args.guest] +
                [("remove_guest", value) for value in args.remove_guest] +
                [("host", value) for value in args.host]):
            source_name, separator, target = item.partition("=")
            if not separator:
                parser.error("artifact mapping needs SOURCE=TARGET")
            source = checked_source(source_name)
            if kind == "guest":
                validate_application_target(source, target)
            stage_mapping(kind, source, target)
            if kind == "guest":
                for companion, companion_target in kit_companions(source, target):
                    if ("guest", companion_target) not in targets:
                        stage_mapping("guest", companion, companion_target)
        if args.startup_manifest:
            stage_mapping("guest", astra_image.STARTUP_MANIFEST.encode("ascii"),
                          "/startup/system")
        shutil.copy2(REPOSITORY / "emu/qemu/dev-install-de25.py", stage)
        shutil.copy2(REPOSITORY / "emu/qemu/storage_image.py", stage)
        (stage / "manifest.json").write_text(json.dumps({
            "guest": [value for kind, value in mappings if kind == "guest"],
            "remove_guest": [value for kind, value in mappings
                             if kind == "remove_guest"],
            "host": [value for kind, value in mappings if kind == "host"],
        }))
        board_stage = subprocess.check_output(
            ["ssh", args.board, "mktemp -d /var/tmp/astra-dev.XXXXXX"],
            text=True).strip()
        if not board_stage.startswith("/var/tmp/astra-dev.") or \
                "/" in board_stage[len("/var/tmp/astra-dev."):]:
            raise RuntimeError("board returned unsafe staging directory")
        try:
            subprocess.run(["scp", "-l", "4096", "-r", str(stage) + "/.",
                            args.board + ":" + board_stage + "/"], check=True)
            command = "python3 " + shlex.quote(board_stage + "/dev-install-de25.py") + \
                " " + shlex.quote(board_stage)
            subprocess.run(["ssh", args.board, command], check=True)
        finally:
            subprocess.run(["ssh", args.board,
                            "rm -rf -- " + shlex.quote(board_stage)], check=True)


if __name__ == "__main__":
    main()
