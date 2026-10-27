#!/usr/bin/env python3
import pathlib
import struct
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
import aicon


def library(name, abi, version):
    major, minor, patch = map(int, version.split("."))
    identity = struct.pack(
        ">IHHHHHHHHIII24s32s40s", 0x414c4942, 2, 128,
        major, minor, patch, abi, 0, 0, 0x4d303430, 1, 0,
        (name + "." + str(abi)).encode("ascii"), b"test", b"test")
    return bytes(0x200) + identity


def bundle(root):
    root.mkdir()
    (root / "bin").mkdir()
    (root / "resources").mkdir()
    (root / "bin" / "Terminal").write_bytes(b"elf")
    (root / "resources" / "Terminal.aicon").write_bytes(
        aicon.build("terminal"))
    (root / "manifest").write_text(
        "astra-bundle 1\nkind application\nid org.astra.terminal\n"
        "name Terminal\nversion 0.1.0\nexecutable bin/Terminal\n"
        "icon resources/Terminal.aicon\n", encoding="utf-8")


def kit(root):
    root.mkdir()
    (root / "manifest").write_text(
        "astra-bundle 1\nkind kit\nid org.astra.graphics\n"
        "name Graphics\nversion 1.0.0\n"
        "provides graphics.library 1 1.0.0\n", encoding="utf-8")
    payload = (root / "libraries" / "graphics.library" / "abi-1" /
               "1.0.0" / "m68k-68040")
    payload.mkdir(parents=True)
    (payload / "graphics.library").write_bytes(
        library("graphics.library", 1, "1.0.0"))


def run(tool, *arguments):
    subprocess.run([tool, *map(str, arguments)], check=True,
                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    tool = sys.argv[1]
    with tempfile.TemporaryDirectory() as temporary:
        root = pathlib.Path(temporary)
        source = root / "Terminal.app"
        copied = root / "Terminal Copy.app"
        moved = root / "Terminal Moved.app"
        trash = root / "Trash"
        apps = root / "Apps"
        kits = root / "Kits"
        apps.mkdir()
        kits.mkdir()
        bundle(source)
        run(tool, "check", source)
        with (source / "manifest").open("a", encoding="utf-8") as handle:
            handle.write("provides private.library 1 1.2.3\n")
        invalid = subprocess.run([tool, "check", str(source)],
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        assert invalid.returncode != 0
        private = (source / "libs" / "private.library" /
                   "abi-1" / "1.2.3" / "m68k-68040")
        private.mkdir(parents=True)
        (private / "private.library").write_bytes(
            library("private.library", 1, "1.2.3"))
        run(tool, "check", source)
        (private / "private.library").write_bytes(
            library("private.library", 1, "1.2.4"))
        invalid = subprocess.run([tool, "check", str(source)],
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        assert invalid.returncode != 0
        (private / "private.library").write_bytes(
            library("private.library", 1, "1.2.3"))
        large = root / "Large.app"
        bundle(large)
        with (large / "manifest").open("a", encoding="utf-8") as handle:
            handle.write("requires runtime.library 1 1.0.0\n" * 300)
        assert (large / "manifest").stat().st_size > 4096
        run(tool, "check", large)
        with (large / "manifest").open("a", encoding="utf-8") as handle:
            handle.write("requires broken\n")
        invalid = subprocess.run([tool, "check", str(large)],
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        assert invalid.returncode != 0
        run(tool, "copy", source, copied)
        assert (copied / "libs" / "private.library" /
                "abi-1" / "1.2.3" / "m68k-68040" /
                "private.library").read_bytes() == library(
                    "private.library", 1, "1.2.3")
        occupied = root / "Occupied.app"
        bundle(occupied)
        refused = subprocess.run([tool, "move", str(copied), str(occupied)],
                                 stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        assert refused.returncode != 0 and copied.exists() and occupied.exists()
        run(tool, "move", copied, moved)
        run(tool, "trash", moved, trash)
        assert source.is_dir()
        assert not moved.exists()
        assert (trash / moved.name / "manifest").is_file()
        dependent = apps / "Dependent.app"
        bundle(dependent)
        with (dependent / "manifest").open("a", encoding="utf-8") as handle:
            handle.write("requires graphics.library 1 1.0.0\n")
        graphics = kits / "Graphics.kit"
        kit(graphics)
        refused = subprocess.run([tool, "delete", str(graphics), str(apps),
                                  str(kits)], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        assert refused.returncode != 0 and graphics.exists()
        private_app = apps / "Private.app"
        bundle(private_app)
        with (private_app / "manifest").open("a", encoding="utf-8") as handle:
            handle.write("provides graphics.library 1 1.0.0\n")
        private_payload = (private_app / "libs" / "graphics.library" /
                           "abi-1" / "1.0.0" / "m68k-68040")
        private_payload.mkdir(parents=True)
        (private_payload / "graphics.library").write_bytes(
            library("graphics.library", 1, "1.0.0"))
        run(tool, "check", private_app)
        refused = subprocess.run([tool, "delete", str(graphics), str(apps),
                                  str(kits)], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)
        assert refused.returncode != 0 and graphics.exists()
        with (dependent / "manifest").open("a", encoding="utf-8") as handle:
            handle.write("provides graphics.library 1 1.0.0\n")
        own_payload = (dependent / "libs" / "graphics.library" /
                       "abi-1" / "1.0.0" / "m68k-68040")
        own_payload.mkdir(parents=True)
        (own_payload / "graphics.library").write_bytes(
            library("graphics.library", 1, "1.0.0"))
        run(tool, "check", dependent)
        run(tool, "delete", graphics, apps, kits)
        assert not graphics.exists()
    print("bundle tool tests passed")


if __name__ == "__main__":
    main()
