#!/usr/bin/env python3
"""Preparing a card image for a gate.

Two jobs, both of them putting this build's own products where the machine
expects to find them.

The event catalog is the `.astra_events` section verbatim, and the ROM image
strips that section, so a running program does not carry its own text -- the
events service reads it from SYS:. Without it every line the events tree
renders is a message id, which is honest, useless as a check, and exactly what
a stale catalog would look like.

The commands are the programs in COMMANDS:. They are files on the volume rather
than anything the ROM carries, which is the whole claim task 4 makes, so a gate
that did not install them would be testing a machine with no programs on it.

Shared by both QEMU gates rather than copied into each: a second copy of this
would be a second answer to where these things live.
"""

import os
import shlex
import struct
import subprocess
import sys
import tempfile

REPOSITORY = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

CATALOG_NAME = "astra_events.cat"
DEFAULT_CATALOG = os.path.join(
    REPOSITORY, "sw/userspace/build/m68k/astra_events.cat")

COMMANDS_DIRECTORY = "commands"
# The writable member of COMMANDS:, and the reason it exists in this gate: a
# command installed here shadows the one the system shipped, which is what a
# union is for and what nothing else on the volume can demonstrate.
LOCAL_COMMANDS_DIRECTORY = "local/commands"
DEFAULT_COMMANDS = os.path.join(REPOSITORY, "sw/userspace/commands/build/m68k")
SERVICES_DIRECTORY = "services"
LIBS_DIRECTORY = "libs"
TERMINFO_DIRECTORY = "terminfo"
APPS_DIRECTORY = "apps"
STARTUP_DIRECTORY = "startup"
STARTUP_NAME = "system"
DEFAULT_SERVICES = os.path.join(
    REPOSITORY, "sw/userspace/services")
DEFAULT_KITS = os.path.join(REPOSITORY, "sw/userspace/kits/build")
DEFAULT_APPS = os.path.join(REPOSITORY, "sw/userspace/apps/build")
DEFAULT_TERMINFO = os.path.join(
    REPOSITORY,
    "sw/userspace/terminal/build/terminfo/a/astra-256color")
KIT_BUNDLES = ("Graphics.kit", "Filesystem.kit", "Interface.kit",
               "Events.kit", "Messaging.kit")
APPLICATION_BUNDLES = ("Terminal.app",)
PROVIDER_INDEX_MAGIC = 0x41505256  # "APRV"
PROVIDER_INDEX_HEADER = struct.Struct(">IHHHHHHHHI")
LIBRARY_IDENTITY_HEADER = struct.Struct(">IHHHHHHHHIII")
PROVIDER_INDEX_MAX = 192
# One startup profile, not two. The terminal stopped being a program that owns
# the screen and the keyboard when the window runtime landed: it is a window
# client now, so a profile that runs it has to run the display service that
# serves GUI, and the supervisor's own launch port has to have a holder --
# without APP_LAUNCH in somebody's grants the last sender is the supervisor's
# own, closing it leaves the port with no peer, and the watch loop exits
# PEER_DEAD as soon as the machine is up. The desktop is what holds it. So the
# desktop profile is the profile, and a gate that wants a terminal opens one
# from the desktop the way a person does.
DISPLAY_STARTUP_MANIFEST = (
    "service SERVICES:storage grants BLOCK_DEVICE BLOCK_IRQ "
    "serves SYS:r required\n"
    "service SERVICES:events grants SYS:r STORE:rw LIBS:r "
    "serves EVENTS:r required\n"
    "service SERVICES:input grants INPUT INPUT_IRQ "
    "serves INPUT_SERVICE required\n"
    "service SERVICES:display grants DISPLAY DISPLAY_IRQ "
    "INPUT_SERVICE serves GUI required\n"
    "application SERVICES:desktop grants GUI APP_LAUNCH APPS:r LIBS:r "
    "required\n")
STARTUP_MANIFEST = DISPLAY_STARTUP_MANIFEST
DISPLAY_SERVICES = ("storage", "events", "input", "display", "desktop")


def ext4_partition(image):
    """The (offset, length) of the Linux partition, from the partition table.

    Read rather than assumed: an image built with a different layout would
    otherwise have the catalog written into the middle of it, and that surfaces
    as a filesystem which is subtly wrong rather than as a step that refused.
    """
    with open(image, "rb") as handle:
        table = handle.read(512)[446:510]
    for index in range(4):
        entry = table[index * 16:(index + 1) * 16]
        if entry[4] == 0x83:
            start = int.from_bytes(entry[8:12], "little") * 512
            length = int.from_bytes(entry[12:16], "little") * 512
            return start, length
    raise RuntimeError("%s has no Linux partition" % image)


def _slice(image, offset, length, out):
    with open(image, "rb") as source, open(out, "wb") as target:
        source.seek(offset)
        remaining = length
        while remaining > 0:
            block = source.read(min(1 << 20, remaining))
            if not block:
                break
            target.write(block)
            remaining -= len(block)


def _splice(image, offset, source_path):
    with open(source_path, "rb") as source, open(image, "r+b") as target:
        target.seek(offset)
        while True:
            block = source.read(1 << 20)
            if not block:
                break
            target.write(block)


def _debugfs(volume, request, what, optional=False):
    result = subprocess.run(["debugfs", "-w", "-R", request, volume],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if optional:
        return
    # debugfs reports most refusals on stdout and still exits zero, so the
    # output has to be read rather than only the status. A step that silently
    # wrote nothing is the kind that surfaces as a machine behaving oddly.
    output = result.stdout.decode("utf-8", "replace").strip()
    if result.returncode != 0 or " while " in output:
        raise RuntimeError("debugfs refused %s: %s" % (what, output or request))


def _mkdir(volume, path, what):
    result = subprocess.run(["debugfs", "-R", "stat %s" % path, volume],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    if "Inode:" in result.stdout.decode("utf-8", "replace"):
        return
    _debugfs(volume, "mkdir %s" % path, what)


def _commands(directory):
    """The built command images: every regular file that is not an ELF kept
    for a debugger. `make` in sw/userspace/commands produces `status` beside
    `status.elf`, and the volume gets the stripped one."""
    if not os.path.isdir(directory):
        raise RuntimeError("no commands at %s -- build them first" % directory)
    found = []
    for name in sorted(os.listdir(directory)):
        path = os.path.join(directory, name)
        if os.path.isfile(path) and not name.endswith((".elf", ".d")):
            found.append((name, path))
    if not found:
        raise RuntimeError("no commands built in %s" % directory)
    return found


def _services(directory, names):
    found = []
    for name in names:
        path = os.path.join(directory, name, "build", "m68k", name)
        if not os.path.isfile(path):
            raise RuntimeError("no service image at %s -- build services "
                               "first" % path)
        found.append((name, path))
    return found


def _bundles(directory, names):
    found = []
    for name in names:
        path = os.path.join(directory, name)
        if not os.path.isdir(path) or not os.path.isfile(
                os.path.join(path, "manifest")):
            raise RuntimeError("no complete bundle at %s -- build bundles "
                               "first" % path)
        found.append(path)
    return found


def _providers(bundles):
    """Latest provider path for each (library, ABI), rebuilt from manifests."""
    found = {}
    for bundle in bundles:
        manifest = os.path.join(bundle, "manifest")
        with open(manifest, "r", encoding="ascii") as handle:
            for line in handle:
                fields = shlex.split(line, comments=True)
                if not fields or fields[0] != "provides":
                    continue
                if len(fields) != 4 or not fields[2].isdigit():
                    raise RuntimeError("invalid provider in %s: %s" %
                                       (manifest, line.strip()))
                name = fields[1]
                abi = int(fields[2])
                try:
                    version = tuple(int(part) for part in fields[3].split("."))
                except ValueError as error:
                    raise RuntimeError("invalid provider version in %s" %
                                       manifest) from error
                if (len(version) != 3 or abi <= 0 or
                        any(part < 0 or part > 65535 for part in version) or
                        not name or any(not (character.isalnum() or
                            character in "._-") for character in name)):
                    raise RuntimeError("invalid provider in %s: %s" %
                                       (manifest, line.strip()))
                key = (name, abi)
                if key not in found or version > found[key][0]:
                    relative = "libraries/%s/abi-%u/%s/m68k-68030/%s" % (
                        name, abi, fields[3], name)
                    image = os.path.join(bundle, relative)
                    with open(image, "rb") as library:
                        library.seek(0x200)
                        identity = library.read(128)
                    if len(identity) != 128:
                        raise RuntimeError("short library identity: %s" % image)
                    header = LIBRARY_IDENTITY_HEADER.unpack_from(identity)
                    actual_name = identity[32:56].split(b"\0", 1)[0].decode(
                        "ascii")
                    if (header[0] != 0x414c4942 or header[1:3] != (1, 128) or
                            header[3:6] != version or header[6] != abi or
                            header[8] != 0 or header[9] != 0x4d303330 or
                            header[11] != 0x00f00000 or actual_name != name):
                        raise RuntimeError("library identity disagrees with %s" %
                                           manifest)
                    found[key] = (version, header[7], header[10],
                        "LIBS:%s/%s" % (os.path.basename(bundle), relative))
    return found


def _install_bundle(volume, source, destination):
    for root, directories, files in os.walk(source):
        if any(os.path.islink(os.path.join(root, name))
               for name in directories + files):
            raise RuntimeError("bundle contains a symbolic link: %s" % source)
        relative = os.path.relpath(root, source)
        target = destination if relative == "." else \
            destination + "/" + relative
        _mkdir(volume, target, "bundle directory")
        for name in files:
            host = os.path.join(root, name)
            guest = target + "/" + name
            _debugfs(volume, "rm %s" % guest, "old bundle file",
                     optional=True)
            _debugfs(volume, "write %s %s" % (host, guest), "bundle file")


def install(image, catalog=None, commands=None, services=None, kits=None,
            apps=None, terminfo=None,
            service_names=DISPLAY_SERVICES,
            manifest_text=STARTUP_MANIFEST):
    """Writes this build's catalog and commands into the image's volume.

    The volume is lifted out of the image, worked on, and put back. Two reasons
    it is not done in place: e2fsck cannot reopen an `image?offset=` target
    after it recovers a journal, and the journal has to be recovered first --
    an image a machine has run carries transactions that are replayed at the
    next mount, and a replay lands on top of whatever debugfs wrote. The file
    is then on the host and absent on the machine, with nothing anywhere saying
    why. That one cost an hour; the copy costs a second.

    Both jobs happen inside the one lift, because two lifts would mean two
    journal recoveries and the second would replay over the first's work.
    """
    catalog = catalog or DEFAULT_CATALOG
    commands = commands or DEFAULT_COMMANDS
    services = services or DEFAULT_SERVICES
    kits = kits or DEFAULT_KITS
    apps = apps or DEFAULT_APPS
    terminfo = terminfo or DEFAULT_TERMINFO
    if not os.path.exists(catalog):
        raise RuntimeError("no catalog at %s -- build the supervisor first" %
                           catalog)
    if not os.path.isfile(terminfo):
        raise RuntimeError("no astra-256color terminfo at %s -- build the "
                           "terminal first" % terminfo)
    built = _commands(commands)
    service_images = _services(services, service_names)
    kit_bundles = _bundles(kits, KIT_BUNDLES)
    application_bundles = _bundles(apps, APPLICATION_BUNDLES)
    offset, length = ext4_partition(image)
    with tempfile.TemporaryDirectory(prefix="astra-volume-") as temporary:
        volume = os.path.join(temporary, "volume.img")
        _slice(image, offset, length, volume)

        fsck = subprocess.run(["e2fsck", "-fy", volume],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if fsck.returncode > 2:
            raise RuntimeError("e2fsck refused the volume: %s" %
                               fsck.stdout.decode("utf-8", "replace").strip())
        _debugfs(volume, "rm /%s" % CATALOG_NAME, "the old catalog",
                 optional=True)
        _debugfs(volume, "write %s %s" % (catalog, CATALOG_NAME),
                 "the catalog")

        # The supervisor makes this directory at boot if it is missing, but the
        # commands have to be here before the boot that runs them.
        _mkdir(volume, "/%s" % COMMANDS_DIRECTORY,
               "the commands directory")
        for name, path in built:
            target = "/%s/%s" % (COMMANDS_DIRECTORY, name)
            _debugfs(volume, "rm %s" % target, "an old command", optional=True)
            _debugfs(volume, "write %s %s" % (path, target), "command " + name)

        _mkdir(volume, "/%s" % SERVICES_DIRECTORY,
               "the services directory")
        for name, path in service_images:
            target = "/%s/%s" % (SERVICES_DIRECTORY, name)
            _debugfs(volume, "rm %s" % target, "an old service",
                     optional=True)
            _debugfs(volume, "write %s %s" % (path, target),
                     "service " + name)

        _mkdir(volume, "/%s" % LIBS_DIRECTORY,
               "the shared Kit directory")
        terminfo_directory = "/%s/%s" % (LIBS_DIRECTORY,
                                           TERMINFO_DIRECTORY)
        _mkdir(volume, terminfo_directory, "the terminfo directory")
        terminfo_letter = terminfo_directory + "/a"
        _mkdir(volume, terminfo_letter, "the terminfo letter directory")
        terminfo_target = terminfo_letter + "/astra-256color"
        _debugfs(volume, "rm %s" % terminfo_target,
                 "the old astra-256color terminfo", optional=True)
        _debugfs(volume, "write %s %s" % (terminfo, terminfo_target),
                 "the astra-256color terminfo")
        for bundle in kit_bundles:
            _install_bundle(volume, bundle,
                            "/%s/%s" % (LIBS_DIRECTORY,
                                         os.path.basename(bundle)))

        # One tiny lookup replaces a directory sweep and every Kit manifest
        # read in each new process. The manifests remain authoritative; these
        # entries are overwritten from them on every image installation.
        provider_directory = "/%s/.providers" % LIBS_DIRECTORY
        _mkdir(volume, provider_directory, "the provider index directory")
        for (name, abi), (version, abi_minor, build_id, path) in sorted(
                _providers(kit_bundles).items()):
            host = os.path.join(temporary, "%s.abi-%u" % (name, abi))
            encoded = path.encode("ascii")
            record = PROVIDER_INDEX_HEADER.pack(
                PROVIDER_INDEX_MAGIC, 1, PROVIDER_INDEX_HEADER.size,
                version[0], version[1], version[2], abi, abi_minor, 0,
                build_id) + \
                encoded
            if len(record) > PROVIDER_INDEX_MAX:
                raise RuntimeError("provider index path is too long: %s" % path)
            with open(host, "wb") as handle:
                handle.write(record)
            target = "%s/%s.abi-%u" % (provider_directory, name, abi)
            _debugfs(volume, "rm %s" % target, "the old provider index",
                     optional=True)
            _debugfs(volume, "write %s %s" % (host, target),
                     "provider index")

        _mkdir(volume, "/%s" % APPS_DIRECTORY,
               "the application directory")
        for bundle in application_bundles:
            _install_bundle(volume, bundle,
                            "/%s/%s" % (APPS_DIRECTORY,
                                         os.path.basename(bundle)))

        _mkdir(volume, "/%s" % STARTUP_DIRECTORY,
               "the startup directory")
        manifest = os.path.join(temporary, STARTUP_NAME)
        with open(manifest, "w", encoding="ascii", newline="\n") as handle:
            handle.write(manifest_text)
        target = "/%s/%s" % (STARTUP_DIRECTORY, STARTUP_NAME)
        _debugfs(volume, "rm %s" % target, "the old startup manifest",
                 optional=True)
        _debugfs(volume, "write %s %s" % (manifest, target),
                 "the startup manifest")

        # The shadowing pair. `which` is installed on both members under two
        # names: the shipped one stays where it is, and a copy goes into the
        # writable member under the same name as a shipped command, so a
        # lookup has a real choice to make and the gate can see which it made.
        _mkdir(volume, "/local", "the local directory")
        _mkdir(volume, "/%s" % LOCAL_COMMANDS_DIRECTORY,
               "the local commands directory")
        for name, path in built:
            if name != "which":
                continue
            target = "/%s/%s" % (LOCAL_COMMANDS_DIRECTORY, "devices")
            _debugfs(volume, "rm %s" % target, "the old local command",
                     optional=True)
            _debugfs(volume, "write %s %s" % (path, target),
                     "a shadowing command")
        _splice(image, offset, volume)


if __name__ == "__main__":
    if len(sys.argv) == 2:
        install(sys.argv[1])
    elif len(sys.argv) == 3 and sys.argv[1] == "--display":
        install(sys.argv[2], service_names=DISPLAY_SERVICES,
                manifest_text=DISPLAY_STARTUP_MANIFEST)
    else:
        raise SystemExit("usage: astra_image.py [--display] IMAGE")
