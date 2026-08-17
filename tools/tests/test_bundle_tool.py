#!/usr/bin/env python3
import pathlib
import subprocess
import sys
import tempfile

HERE = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(HERE))
import aicon


def bundle(root):
    root.mkdir()
    (root / "bin").mkdir()
    (root / "resources").mkdir()
    (root / "bin" / "Terminal").write_bytes(b"elf")
    (root / "resources" / "Terminal.aicon").write_bytes(aicon.build())
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
               "1.0.0" / "m68k-68030")
    payload.mkdir(parents=True)
    (payload / "graphics.library").write_bytes(b"elf")


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
        run(tool, "copy", source, copied)
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
        (dependent / "manifest").write_text(
            (dependent / "manifest").read_text(encoding="utf-8").replace(
                "requires graphics.library 1 1.0.0\n", ""),
            encoding="utf-8")
        run(tool, "delete", graphics, apps, kits)
        assert not graphics.exists()
    print("bundle tool tests passed")


if __name__ == "__main__":
    main()
