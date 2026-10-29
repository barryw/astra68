#!/usr/bin/env python3
"""Fetch verified Quake 1.06 shareware data for local application builds."""

import hashlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile


URL = "https://www.gamers.org/pub/idgames2/idstuff/quake/quake106.zip"
ARCHIVE_SHA256 = "ec6c9d34b1ae0252ac0066045b6611a7919c2a0d78a3a66d9387a8f597553239"
RESOURCE_SHA256 = "c192c9c71bee41750dd7d14c99378766d61e077977b9d13d1a457b8d9eabe34a"
PAK_SHA256 = "35a9c55e5e5a284a159ad2a62e0e8def23d829561fe2f54eb402dbc0a9a946af"
FILES = ("id1/pak0.pak", "licinfo.txt", "slicnse.txt")


def checked(data, expected, name):
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError(f"{name} checksum mismatch")


def main(output):
    cache = Path(os.environ.get(
        "ASTRA_DOWNLOAD_CACHE", Path.home() / ".cache/astra68/downloads"))
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / "quake106.zip"
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read()
        checked(data, ARCHIVE_SHA256, "quake106.zip")
        pending = archive.with_suffix(".zip.part")
        pending.write_bytes(data)
        pending.replace(archive)
    data = archive.read_bytes()
    checked(data, ARCHIVE_SHA256, "quake106.zip")
    with zipfile.ZipFile(io.BytesIO(data)) as package:
        resource = package.read("resource.1")
    checked(resource, RESOURCE_SHA256, "resource.1")
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="quake-shareware-",
                                     dir=output.parent) as temporary:
        staging = Path(temporary)
        packed = staging / "resource.1"
        packed.write_bytes(resource)
        subprocess.run(["lha", "x", str(packed), *FILES], cwd=staging,
                       stdout=subprocess.DEVNULL, check=True)
        checked((staging / FILES[0]).read_bytes(), PAK_SHA256, "pak0.pak")
        for name in FILES:
            target = output / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(staging / name, target)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: fetch_pak.py OUTPUT_DIRECTORY")
    main(Path(sys.argv[1]))
