#!/usr/bin/env python3
"""Small check for the provider index built into system images."""

import os
import struct
import tempfile

import astra_image


assert astra_image.HOSTBENCH_SERVICES == \
    astra_image.DISPLAY_SERVICES + ("hostbench",)
assert astra_image.HOSTBENCH_STARTUP_MANIFEST == \
    astra_image.DISPLAY_STARTUP_MANIFEST + \
    "application SERVICES:hostbench grants HOST_DEVICE required\n"
startup = astra_image.DISPLAY_STARTUP_MANIFEST.splitlines()
assert startup[0].startswith("service SERVICES:storage ")
assert startup[1] == \
    "service SERVICES:hostfs grants HOST_DEVICE serves WORK:rw required"


class Result:
    def __init__(self, output=b"", returncode=0):
        self.returncode = returncode
        self.stdout = output


original_run = astra_image.subprocess.run
commands = []


with tempfile.TemporaryDirectory() as directory:
    volume = os.path.join(directory, "volume.img")
    image = os.path.join(directory, "system.img")
    with open(volume, "wb") as handle:
        handle.write(b"V" * (1 << 20))
    astra_image._write_partitioned_image(image, volume, 2 << 20)
    assert astra_image.ext4_partition(image) == (1 << 20, 1 << 20)
    with open(image, "rb") as handle:
        handle.seek(1 << 20)
        assert handle.read(16) == b"V" * 16
    try:
        astra_image._write_partitioned_image(
            os.path.join(directory, "bad.img"), volume, (2 << 20) + 1)
        raise AssertionError("unaligned system image size was accepted")
    except RuntimeError as error:
        assert "multiple of 512" in str(error)


astra_image.subprocess.run = lambda command, **_kwargs: \
    commands.append(command) or Result()
astra_image._build_current_userspace()
userspace = os.path.join(astra_image.REPOSITORY, "sw/userspace")
assert commands == [["make", "-C", userspace, "clean"],
                    ["make", "-C", userspace, "all"]]

astra_image.subprocess.run = lambda *_args, **_kwargs: Result(b"failed", 2)
try:
    astra_image._build_current_userspace()
    raise AssertionError("failed userspace build was accepted")
except RuntimeError as error:
    assert "cannot clean current userspace products: failed" == str(error)
astra_image.subprocess.run = original_run
commands.clear()


def existing_run(command, **_kwargs):
    commands.append(command)
    return Result(b"Inode: 12\n")


astra_image.subprocess.run = existing_run
astra_image._mkdir("volume", "/existing", "test directory")
assert len(commands) == 1 and commands[0][2] == "stat /existing"

commands.clear()


def missing_run(command, **_kwargs):
    commands.append(command)
    return Result(b"File not found\n" if command[2].startswith("stat ") else b"")


astra_image.subprocess.run = missing_run
astra_image._mkdir("volume", "/missing", "test directory")
assert [command[2] if command[2] != "-R" else command[3]
        for command in commands] == ["stat /missing", "mkdir /missing"]
astra_image.subprocess.run = original_run

commands.clear()


def directory_run(command, **_kwargs):
    commands.append(command)
    if command[2] == "ls -p /libs":
        return Result(b"/2/040755/0/0/.//\n/2/040755/0/0/..//\n"
                      b"/12/100644/0/0/stale.library/4/\n"
                      b"/13/040755/0/0/Old.kit//\n")
    if command[2] == "ls -p /libs/Old.kit":
        return Result(b"/13/040755/0/0/.//\n/2/040755/0/0/..//\n"
                      b"/14/100644/0/0/manifest/4/\n")
    return Result()


astra_image.subprocess.run = directory_run
astra_image._clear_directory("volume", "/libs")
assert [command[2] if command[2] != "-R" else command[3]
        for command in commands] == [
            "ls -p /libs", "rm /libs/stale.library",
            "ls -p /libs/Old.kit", "rm /libs/Old.kit/manifest",
            "rmdir /libs/Old.kit"]
astra_image.subprocess.run = lambda *_args, **_kwargs: Result(
    b"/12/100644/0/0/not safe/4/\n")
try:
    astra_image._clear_directory("volume", "/libs")
    raise AssertionError("unsafe stale image name was accepted")
except RuntimeError as error:
    assert "unsafe image directory entry" in str(error)
astra_image.subprocess.run = original_run

commands.clear()
astra_image.subprocess.run = existing_run
astra_image._reset_journal("volume")
assert commands == [
    ["tune2fs", "-f", "-O", "^has_journal", "volume"],
    ["e2fsck", "-fy", "volume"],
    ["tune2fs", "-j", "-J", "size=4", "volume"],
    ["e2fsck", "-fy", "volume"],
]
astra_image.subprocess.run = original_run

calls = []
original_commands = astra_image._commands
original_services = astra_image._services
original_bundles = astra_image._bundles
original_partition = astra_image.ext4_partition


class SelectionComplete(Exception):
    pass


astra_image._commands = lambda directory: \
    calls.append(("commands", directory))
astra_image._services = lambda directory, names: \
    calls.append(("services", directory, names)) or []
astra_image._bundles = lambda directory, names: \
    calls.append(("bundles", directory, names))
astra_image.ext4_partition = lambda _image: (_ for _ in ()).throw(
    SelectionComplete())
try:
    astra_image._install_built(
        "image", catalog=__file__, commands=None, services="services",
        kits=None, apps=None, terminfo=None, service_names=("hostbench",))
    raise AssertionError("selection check reached the image")
except SelectionComplete:
    pass
assert calls == [("services", "services", ("hostbench",))]
astra_image._commands = original_commands
astra_image._services = original_services
astra_image._bundles = original_bundles
astra_image.ext4_partition = original_partition

with tempfile.TemporaryDirectory() as directory:
    service = os.path.join(directory, "storage")
    image = os.path.join(service, "build", "m68k", "storage")
    os.makedirs(os.path.dirname(image))
    with open(image, "wb") as handle:
        handle.write(b"storage")

    checks = []

    def current_service(command, **_kwargs):
        checks.append(command)
        return Result()

    astra_image.subprocess.run = current_service
    assert astra_image._services(directory, ("storage",)) == [
        ("storage", image)]
    assert checks == [["make", "-q", "-C", service,
                       "build/m68k/storage"]]

    astra_image.subprocess.run = lambda *_args, **_kwargs: Result(
        b"storage needs rebuilding", 1)
    try:
        astra_image._services(directory, ("storage",))
        raise AssertionError("stale service was accepted")
    except RuntimeError as error:
        assert "stale service image" in str(error)

astra_image.subprocess.run = original_run

with tempfile.TemporaryDirectory() as directory:
    for name in ("status", "stale-orphan"):
        with open(os.path.join(directory, name), "wb") as image:
            image.write(name.encode("ascii"))
    with open(os.path.join(directory, ".commands"), "w",
              encoding="ascii") as manifest:
        manifest.write("status\n")
    assert astra_image._commands(directory) == [
        ("status", os.path.join(directory, "status"))]


with tempfile.TemporaryDirectory() as directory:
    bundles = []
    for kit, version in (("Old.kit", "1.2.0"), ("New.kit", "1.10.0")):
        bundle = os.path.join(directory, kit)
        os.mkdir(bundle)
        with open(os.path.join(bundle, "manifest"), "w", encoding="ascii") as manifest:
            manifest.write("kind kit\nprovides filesystem.library 1 %s\n" % version)
        library = os.path.join(bundle, "libraries", "filesystem.library",
                               "abi-1", version, "m68k-68030")
        os.makedirs(library)
        with open(os.path.join(library, "filesystem.library"), "wb") as image:
            identity = struct.pack(
                ">IHHHHHHHHIII24s32s40s", 0x414c4942, 1, 128,
                *(int(part) for part in version.split(".")), 1, 0, 0,
                0x4d303330, 0x12345678, 0x00f00000,
                b"filesystem.library", b"test", b"test")
            image.write(bytes(0x200))
            image.write(identity)
        bundles.append(bundle)

    providers = astra_image._providers(bundles)
    assert providers[("filesystem.library", 1)] == (
        (1, 10, 0), 0, 0x12345678,
        "LIBS:New.kit/libraries/filesystem.library/abi-1/1.10.0/"
        "m68k-68030/filesystem.library")

print("astra image provider index: PASS")
