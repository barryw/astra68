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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", action="append", default=[], metavar="DIR[:TARGET]")
    parser.add_argument("--guest", action="append", default=[],
                        metavar="SOURCE=/GUEST/PATH")
    parser.add_argument("--host", action="append", default=[],
                        metavar="SOURCE=RELEASE/PATH")
    parser.add_argument("--board", default=BOARD)
    args = parser.parse_args()
    if not args.guest and not args.host:
        parser.error("at least one --guest or --host artifact is required")
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
        for index, (kind, item) in enumerate(
                [("guest", value) for value in args.guest] +
                [("host", value) for value in args.host]):
            source_name, separator, target = item.partition("=")
            if not separator:
                parser.error("artifact mapping needs SOURCE=TARGET")
            source = checked_source(source_name)
            name = str(index)
            shutil.copy2(source, files / name)
            mappings.append((kind, {"source": "files/" + name, "target": target}))
        shutil.copy2(REPOSITORY / "emu/qemu/dev-install-de25.py", stage)
        shutil.copy2(REPOSITORY / "emu/qemu/storage_image.py", stage)
        (stage / "manifest.json").write_text(json.dumps({
            "guest": [value for kind, value in mappings if kind == "guest"],
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
