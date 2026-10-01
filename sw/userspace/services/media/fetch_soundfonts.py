#!/usr/bin/env python3
"""Fetch Astra's shared SoundFont set, pinned and checked by SHA-256, into
OUT with each font's licence and the default list.

OUT becomes the SOUND volume's soundfonts/ directory on the Linux host (the
DE25 release and the QEMU gates put it there), where the audio host reads
the fonts in place. SoundFonts are build inputs, never repository files
(ARTIFACT_POLICY.md).

default: the fonts every MIDI voice starts with, one file name a line,
lowest first (a later font's preset hides the same one in an earlier).
Every other font is there for a program to stack by its file name.
"""

import hashlib
import io
import os
import sys
import tarfile
import urllib.request

GENERALUSER = ("https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/"
               "684543d5e5efaef08d02be50dcda8d552478fa60/")
TIMGM6MB = ("http://archive.ubuntu.com/ubuntu/pool/universe/t/"
            "timgm6mb-soundfont/timgm6mb-soundfont_1.3-5_all.deb")
# (url, SHA-256 of what it downloads)
DOWNLOADS = {
    "GeneralUser-GS.sf2": (
        GENERALUSER + "GeneralUser-GS.sf2",
        "9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe"),
    "GeneralUser-GS.license.txt": (
        GENERALUSER + "documentation/LICENSE.txt",
        "7b32efefdf95ce38a043799f0659853ddc00fbaa14d8c50f0aca16b9b8b405be"),
    "timgm6mb.deb": (
        TIMGM6MB,
        "b70bc29b8f27adef8f92a2d9c1e26cee977b84c68110f0dd4326d97730a7c3ed"),
}
PACKAGE_MEMBERS = {
    "./usr/share/sounds/sf2/TimGM6mb.sf2": (
        "TimGM6mb.sf2",
        "c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854"),
    "./usr/share/doc/timgm6mb-soundfont/copyright": (
        "TimGM6mb.license.txt",
        "6ef01652619cf29b289a9403cd2ab59550fa3e2c30844c67292eea5421f993e9"),
}
# The default stack, lowest first.
DEFAULT = ("GeneralUser-GS.sf2",)


def fetch(name, cache):
    url, digest = DOWNLOADS[name]
    path = os.path.join(cache, digest + "-" + name)
    if not os.path.exists(path):
        with urllib.request.urlopen(url, timeout=120) as reply:
            data = reply.read()
        if hashlib.sha256(data).hexdigest() != digest:
            raise SystemExit("%s checksum mismatch" % name)
        with open(path + ".part", "wb") as handle:
            handle.write(data)
        os.replace(path + ".part", path)
    with open(path, "rb") as handle:
        data = handle.read()
    if hashlib.sha256(data).hexdigest() != digest:
        raise SystemExit("%s checksum mismatch in the cache" % name)
    return data


def ar_members(data):
    if data[:8] != b"!<arch>\n":
        raise ValueError("not a Debian package")
    at = 8
    while at + 60 <= len(data):
        name = data[at:at + 16].decode().strip().rstrip("/")
        size = int(data[at + 48:at + 58])
        yield name, data[at + 60:at + 60 + size]
        at += 60 + size + (size & 1)


def write(path, data):
    with open(path + ".part", "wb") as handle:
        handle.write(data)
    os.replace(path + ".part", path)


def main():
    output = sys.argv[1]
    cache = os.environ.get("ASTRA_DOWNLOAD_CACHE",
                           os.path.expanduser("~/.cache/astra68/downloads"))
    os.makedirs(cache, exist_ok=True)
    os.makedirs(output, exist_ok=True)
    for name in ("GeneralUser-GS.sf2", "GeneralUser-GS.license.txt"):
        write(os.path.join(output, name), fetch(name, cache))
    payload = dict(ar_members(fetch("timgm6mb.deb", cache)))
    member = next(name for name in payload if name.startswith("data.tar"))
    with tarfile.open(fileobj=io.BytesIO(payload[member])) as archive:
        for name, (target, digest) in PACKAGE_MEMBERS.items():
            contents = archive.extractfile(name).read()
            if hashlib.sha256(contents).hexdigest() != digest:
                raise SystemExit("%s checksum mismatch" % target)
            write(os.path.join(output, target), contents)
    for name in DEFAULT:
        if not os.path.isfile(os.path.join(output, name)):
            raise SystemExit("default font %s was not fetched" % name)
    write(os.path.join(output, "default"),
          "".join(name + "\n" for name in DEFAULT).encode())


if __name__ == "__main__":
    main()
