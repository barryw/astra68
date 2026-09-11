#!/usr/bin/env python3
"""Verify that one NDK archive is complete, relocatable, and reproducible."""

from hashlib import sha256
from pathlib import PurePosixPath
import argparse
import io
import re
import tarfile


REQUIRED = {
    "METADATA",
    "SHA256SUMS",
    "README.md",
    "include/astra/ndk.h",
    "include/astra/version.h",
    "include/posix/unistd.h",
    "lib/m68040/astra_library.ld",
    "lib/m68040/astra_user.ld",
    "lib/m68040/crt0-hosted.o",
    "lib/m68040/crt0.o",
    "lib/m68040/libastra.a",
    "lib/m68040/libastra-pic.a",
    "lib/m68040/libastraevents.a",
    "lib/m68040/libastragraphics.a",
    "lib/m68040/libastragraphics-pic.a",
    "lib/m68040/libastranetwork.a",
    "lib/m68040/libastraposix.a",
    "lib/m68040/libastrart.a",
    "lib/m68040/libastrart-pic.a",
    "lib/m68040/libastrastreams.a",
    "lib/m68040/libastravfs.a",
    "make/astra-native.mk",
    "make/astra-posix.mk",
    "src/astra-posix-program.c",
    "examples/undo.c",
    "docs/html/index.html",
    "docs/astra68-ndk.pdf",
}
VERSION_PATTERN = re.compile(
    rb'^#define ASTRA_OS_VERSION_STRING "([^"]+)"$', re.MULTILINE)


def payload(archive, member):
    stream = archive.extractfile(member)
    assert stream is not None
    return stream.read()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive")
    parser.add_argument("version")
    arguments = parser.parse_args()
    root = f"astra68-ndk-{arguments.version}"

    with tarfile.open(arguments.archive, "r:xz") as archive:
        members = archive.getmembers()
        names = [member.name for member in members]
        assert len(names) == len(set(names)), "duplicate archive member"
        for member in members:
            path = PurePosixPath(member.name)
            assert not path.is_absolute() and ".." not in path.parts
            assert path.parts and path.parts[0] == root
            assert member.isfile() or member.isdir(), member.name
            assert member.uid == 0 and member.gid == 0 and member.mtime == 0

        files = {
            member.name[len(root) + 1:]: member
            for member in members
            if member.isfile()
        }
        missing = REQUIRED - files.keys()
        assert not missing, f"missing NDK files: {sorted(missing)}"

        metadata = payload(archive, files["METADATA"]).decode("ascii")
        assert metadata == (
            "name=Astra 68 Native Developer Kit\n"
            f"version={arguments.version}\n"
            f"astra_os_version={arguments.version}\n"
            "target=m68k-astra\n"
            "cpu=mc68040\n"
        )
        match = VERSION_PATTERN.search(payload(archive, files[
            "include/astra/version.h"]))
        assert match is not None
        assert match.group(1).decode() == arguments.version

        checksums = {}
        for line in io.StringIO(payload(
                archive, files["SHA256SUMS"]).decode("ascii")):
            digest, path = line.rstrip("\n").split("  ", 1)
            assert re.fullmatch(r"[0-9a-f]{64}", digest)
            assert path not in checksums
            checksums[path] = digest
        expected = set(files) - {"SHA256SUMS"}
        assert checksums.keys() == expected
        for path, digest in checksums.items():
            assert sha256(payload(archive, files[path])).hexdigest() == digest

    print(f"Astra NDK archive {arguments.version}: PASS")


if __name__ == "__main__":
    main()
