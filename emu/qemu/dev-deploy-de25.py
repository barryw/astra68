#!/usr/bin/env python3
"""Build only requested targets and install their guest files on running DE25."""

import argparse
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", action="append", default=[], metavar="DIR[:TARGET]")
    parser.add_argument("--guest", action="append", default=[],
                        metavar="SOURCE=/GUEST/PATH")
    parser.add_argument("--remove-guest", action="append", default=[],
                        metavar="SOURCE=/GUEST/PATH")
    parser.add_argument("--host", action="append", default=[],
                        metavar="SOURCE=RELEASE/PATH")
    parser.add_argument("--board", default=BOARD)
    args = parser.parse_args()
    if not args.guest and not args.host and not args.remove_guest:
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
            stage_mapping(kind, source, target)
            if kind == "guest":
                for companion, companion_target in kit_companions(source, target):
                    if ("guest", companion_target) not in targets:
                        stage_mapping("guest", companion, companion_target)
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
