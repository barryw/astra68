#!/usr/bin/env python3
"""Fetch the shareware Doom IWAD (v1.9, unmodified from idgames) from the
Ubuntu doom-wad-shareware package, checked by SHA-256 before and after.
The WAD is a build input, never a repository file (ARTIFACT_POLICY.md)."""

import hashlib
import io
import os
import sys
import tarfile
import urllib.request

URL = ("http://archive.ubuntu.com/ubuntu/pool/multiverse/d/"
       "doom-wad-shareware/doom-wad-shareware_1.9.fixed-2_all.deb")
PACKAGE_SHA256 = \
    "a46bf38d583bc820df9a6662f36f7e5db847cd83700393f8cc5cf374554cf7ca"
WAD_SHA256 = \
    "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771"
MEMBERS = {"./usr/share/games/doom/doom1.wad": "doom1.wad",
           "./usr/share/doc/doom-wad-shareware/copyright":
               "doom1.wad.license.txt"}


def ar_members(data):
    if data[:8] != b"!<arch>\n":
        raise ValueError("not a Debian package")
    at = 8
    while at + 60 <= len(data):
        name = data[at:at + 16].decode().strip().rstrip("/")
        size = int(data[at + 48:at + 58])
        yield name, data[at + 60:at + 60 + size]
        at += 60 + size + (size & 1)


def main():
    output = sys.argv[1]
    cache = os.environ.get("ASTRA_DOWNLOAD_CACHE",
                           os.path.expanduser("~/.cache/astra68/downloads"))
    os.makedirs(cache, exist_ok=True)
    package = os.path.join(cache, os.path.basename(URL))
    if not os.path.exists(package):
        with urllib.request.urlopen(URL, timeout=60) as reply:
            data = reply.read()
        with open(package + ".part", "wb") as handle:
            handle.write(data)
        os.replace(package + ".part", package)
    with open(package, "rb") as handle:
        data = handle.read()
    if hashlib.sha256(data).hexdigest() != PACKAGE_SHA256:
        raise SystemExit("doom-wad-shareware package checksum mismatch")
    payload = dict(ar_members(data))
    member = next(name for name in payload if name.startswith("data.tar"))
    os.makedirs(output, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(payload[member])) as archive:
        for name, target in MEMBERS.items():
            contents = archive.extractfile(name).read()
            if target == "doom1.wad" and \
                    hashlib.sha256(contents).hexdigest() != WAD_SHA256:
                raise SystemExit("doom1.wad checksum mismatch")
            with open(os.path.join(output, target), "wb") as handle:
                handle.write(contents)


if __name__ == "__main__":
    main()
