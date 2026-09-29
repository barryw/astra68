#!/usr/bin/env python3
"""Copy pinned, unmodified SDL2 add-ons into disposable build trees."""

import hashlib
import shutil
import subprocess
import sys
from pathlib import Path


REVISIONS = {
    "SDL2_net": "904600c6133e0435d627ec1878bfdfeac414a899",
    "SDL2_image": "12cb2e40330d256d9b1329647be8f366546d715c",
    "SDL2_mixer": "b208916aed9250fe434360e6c6a95f0697bb7b01",
    "SDL2_ttf": "2a891473eaf05ba1707a4b7913e6c4db7de7458a",
}
PORT = Path(__file__).resolve().parent


def prepare(name: str, source: Path, output: Path | None = None) -> Path:
    if name not in REVISIONS:
        raise ValueError(f"unknown SDL2 add-on: {name}")
    source = source.resolve()
    output = output or PORT / "build" / "vendor" / name
    revision = subprocess.check_output(
        ["git", "-C", str(source), "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != REVISIONS[name]:
        raise ValueError(f"{name} revision {revision} != pinned {REVISIONS[name]}")
    if subprocess.check_output(
        ["git", "-C", str(source), "status", "--porcelain"], text=True
    ).strip():
        raise ValueError(f"{name} source tree is modified")
    if name == "SDL2_ttf":
        freetype = source / "external" / "freetype"
        if not (freetype / "CMakeLists.txt").is_file():
            raise ValueError("SDL2_ttf FreeType submodule is missing")
        submodule = subprocess.check_output(
            ["git", "-C", str(freetype), "rev-parse", "HEAD"], text=True
        ).strip()
        if submodule != "12c5e620858bd503731091e9371d06c0a3e7c967":
            raise ValueError(f"SDL2_ttf FreeType revision {submodule} is not pinned")
    stamp = hashlib.sha256((name + revision + Path(__file__).read_text()).encode()
                           ).hexdigest()
    if (output / ".astra-source-stamp").exists() and (
            output / ".astra-source-stamp").read_text() == stamp:
        return output
    if output.exists():
        shutil.rmtree(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, output, ignore=shutil.ignore_patterns(".git"))
    (output / ".astra-source-stamp").write_text(stamp)
    return output


if __name__ == "__main__":
    try:
        print(prepare(sys.argv[1], Path(sys.argv[2])))
    except (IndexError, OSError, subprocess.CalledProcessError, ValueError) as error:
        raise SystemExit(f"SDL add-on preparation failed: {error}")
