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
CONFIG_DIRECTORY = "config"
CONFIG_SCOPES = ("system", "services", "commands", "applications")
CONFIGURATION = {
    "system/network/resolv.conf": (
        "# Astra delegates getaddrinfo(3) to the host resolver.\n"
        "# DNS server selection remains host policy.\n"),
    "services/ntpd/settings.conf": (
        "astra-config 1\n"
        "schema 1\n"
        "pool pool.ntp.org\n"),
}
DEFAULT_SERVICES = os.path.join(
    REPOSITORY, "sw/userspace/services")
DEFAULT_KITS = os.path.join(REPOSITORY, "sw/userspace/kits/build")
DEFAULT_APPS = os.path.join(REPOSITORY, "sw/userspace/apps/build")
DEFAULT_TERMINFO = os.path.join(
    REPOSITORY,
    "sw/userspace/terminal/build/terminfo/a/astra-256color")
KIT_BUNDLES = ("Graphics.kit", "Filesystem.kit", "Interface.kit",
               "Events.kit", "Messaging.kit", "Network.kit",
               "Configuration.kit")
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
    "service SERVICES:hostfs grants HOST_DEVICE serves WORK:rw required\n"
    "service SERVICES:network grants NETWORK_DEVICE NETWORK_IRQ "
    "serves NETWORK NETWORK_LISTEN required\n"
    "service SERVICES:ntpd grants CLOCK CONFIG:r LIBS:r NETWORK "
    "serves NTP required\n"
    "service SERVICES:events grants SYS:r STORE:rw LIBS:r "
    "serves EVENTS:r EVENT_CONTROL required\n"
    "service SERVICES:input grants INPUT INPUT_IRQ "
    "serves INPUT_SERVICE required\n"
    "service SERVICES:display grants DISPLAY DISPLAY_IRQ "
    "INPUT_SERVICE serves GUI required\n"
    "application SERVICES:desktop grants GUI APP_LAUNCH APPS:r LIBS:r "
    "NETWORK NETWORK_LISTEN NTP "
    "required\n")
STARTUP_MANIFEST = DISPLAY_STARTUP_MANIFEST
DISPLAY_SERVICES = ("storage", "hostfs", "network", "ntpd", "events",
                    "input", "display", "desktop")
HOSTBENCH_SERVICES = DISPLAY_SERVICES + ("hostbench",)
HOSTBENCH_STARTUP_MANIFEST = DISPLAY_STARTUP_MANIFEST + (
    "application SERVICES:hostbench grants HOST_DEVICE required\n")


def _build_current_userspace():
    """Publish every image input from the current source tree."""
    directory = os.path.join(REPOSITORY, "sw/userspace")
    for target in ("clean", "all"):
        result = subprocess.run(["make", "-C", directory, target],
                                stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        if result.returncode != 0:
            raise RuntimeError("cannot %s current userspace products: %s" %
                               (target, result.stdout.decode(
                                   "utf-8", "replace").strip()))


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


def _write_partitioned_image(image, volume, total_size):
    offset = 1 << 20
    if total_size <= offset or total_size % 512 != 0:
        raise RuntimeError("image size must be a multiple of 512 bytes and "
                           "larger than its partition offset")
    sectors = (total_size - offset) // 512
    if sectors > 0xffffffff:
        raise RuntimeError("image exceeds the MBR sector-address format")
    if os.path.getsize(volume) != total_size - offset:
        raise RuntimeError("formatted volume has the wrong size")
    table = bytearray(512)
    table[446:462] = struct.pack(
        "<B3sB3sII", 0, bytes(3), 0x83, bytes(3), offset // 512, sectors)
    table[510:512] = b"\x55\xaa"
    with open(image, "xb") as handle:
        handle.truncate(total_size)
        handle.seek(0)
        handle.write(table)
    _splice(image, offset, volume)


def publish(image, total_size):
    """Atomically publish a clean system image from current source products."""
    destination = os.path.abspath(image)
    if os.path.exists(destination):
        raise RuntimeError("refusing to replace existing image %s" % image)
    if total_size <= 1 << 20 or total_size % 512 != 0:
        raise RuntimeError("image size must be a multiple of 512 bytes and "
                           "larger than its partition offset")
    directory = os.path.dirname(destination)
    storage = os.path.join(REPOSITORY, "sw/userspace/storage")
    with tempfile.TemporaryDirectory(prefix="astra-publish-",
                                     dir=directory) as temporary:
        volume = os.path.join(temporary, "volume.img")
        candidate = os.path.join(temporary, "system.img")
        result = subprocess.run(
            ["make", "-C", storage, "format-volume",
             "VOLUME=%s" % volume,
             "VOLUME_BYTES=%u" % (total_size - (1 << 20))],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode != 0:
            raise RuntimeError("cannot format clean Astra volume: %s" %
                               result.stdout.decode(
                                   "utf-8", "replace").strip())
        _write_partitioned_image(candidate, volume, total_size)
        _build_current_userspace()
        _install_built(candidate)
        os.replace(candidate, destination)


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


def _directory_entries(volume, path):
    result = subprocess.run(["debugfs", "-R", "ls -p %s" % path, volume],
                            stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT)
    output = result.stdout.decode("utf-8", "replace")
    if result.returncode != 0:
        raise RuntimeError("cannot list image directory %s: %s" %
                           (path, output.strip()))
    entries = []
    for line in output.splitlines():
        if not line.startswith("/"):
            continue
        fields = line.split("/")
        if len(fields) < 7:
            raise RuntimeError("invalid debugfs directory entry: %s" % line)
        name = fields[5]
        if name in (".", ".."):
            continue
        if (not name or any(character not in
                            "abcdefghijklmnopqrstuvwxyz"
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                            "0123456789._+-" for character in name)):
            raise RuntimeError("unsafe image directory entry %r in %s" %
                               (name, path))
        try:
            mode = int(fields[2], 8)
        except ValueError as error:
            raise RuntimeError("invalid debugfs directory entry: %s" %
                               line) from error
        entries.append((name, mode & 0o170000 == 0o040000))
    return entries


def _clear_directory(volume, path):
    """Remove every old publisher-owned entry beneath path."""
    for name, directory in _directory_entries(volume, path):
        target = "%s/%s" % (path, name)
        if directory:
            _clear_directory(volume, target)
            _debugfs(volume, "rmdir %s" % target, "old image directory")
        else:
            _debugfs(volume, "rm %s" % target, "old image file")


def _reset_journal(volume):
    """Discard journal records made obsolete by debugfs' direct writes."""
    commands = (
        (["tune2fs", "-f", "-O", "^has_journal", volume], 0),
        (["e2fsck", "-fy", volume], 2),
        (["tune2fs", "-j", "-J", "size=4", volume], 0),
        (["e2fsck", "-fy", volume], 2),
    )
    for command, maximum_status in commands:
        result = subprocess.run(command, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        if result.returncode > maximum_status:
            raise RuntimeError("cannot reset ext4 journal: %s" %
                               result.stdout.decode(
                                   "utf-8", "replace").strip())


def _commands(directory):
    """The exact command images published by the commands build."""
    if not os.path.isdir(directory):
        raise RuntimeError("no commands at %s -- build them first" % directory)
    manifest = os.path.join(directory, ".commands")
    try:
        with open(manifest, "r", encoding="ascii") as handle:
            names = [line.strip() for line in handle if line.strip()]
    except OSError as error:
        raise RuntimeError("no command manifest at %s -- build commands first" %
                           manifest) from error
    if not names or len(names) != len(set(names)):
        raise RuntimeError("invalid command manifest at %s" % manifest)
    found = []
    for name in names:
        if name in (".", "..") or os.path.basename(name) != name:
            raise RuntimeError("invalid command name %r in %s" %
                               (name, manifest))
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            raise RuntimeError("command manifest names missing image %s" % path)
        found.append((name, path))
    return found


def _services(directory, names):
    found = []
    for name in names:
        service = os.path.join(directory, name)
        target = os.path.join("build", "m68k", name)
        path = os.path.join(service, target)
        if not os.path.isfile(path):
            raise RuntimeError("no service image at %s -- build services "
                               "first" % path)
        current = subprocess.run(
            ["make", "-q", "-C", service,
             "ASTRA_PROGRAM_OWNERS_READY=1", target],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if current.returncode != 0:
            output = current.stdout.decode("utf-8", "replace").strip()
            if current.returncode == 1:
                raise RuntimeError("stale service image at %s -- build "
                                   "services first" % path)
            raise RuntimeError("cannot verify service image %s: %s" %
                               (path, output or "make -q failed"))
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


def install(image, catalog=DEFAULT_CATALOG, commands=DEFAULT_COMMANDS,
            services=DEFAULT_SERVICES, kits=DEFAULT_KITS,
            apps=DEFAULT_APPS, terminfo=DEFAULT_TERMINFO,
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
    _build_current_userspace()
    _install_built(image, catalog, commands, services, kits, apps, terminfo,
                   service_names, manifest_text)


def _install_built(image, catalog=DEFAULT_CATALOG,
                   commands=DEFAULT_COMMANDS,
                   services=DEFAULT_SERVICES, kits=DEFAULT_KITS,
                   apps=DEFAULT_APPS, terminfo=DEFAULT_TERMINFO,
                   service_names=DISPLAY_SERVICES,
                   manifest_text=STARTUP_MANIFEST):
    """Install built products; private seam for isolated host tests."""
    if not os.path.exists(catalog):
        raise RuntimeError("no catalog at %s -- build the supervisor first" %
                           catalog)
    if terminfo is not None and not os.path.isfile(terminfo):
        raise RuntimeError("no astra-256color terminfo at %s -- build the "
                           "terminal first" % terminfo)
    built = [] if commands is None else _commands(commands)
    service_images = _services(services, service_names)
    kit_bundles = [] if kits is None else _bundles(kits, KIT_BUNDLES)
    application_bundles = [] if apps is None else \
        _bundles(apps, APPLICATION_BUNDLES)
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
        _clear_directory(volume, "/%s" % COMMANDS_DIRECTORY)
        for name, path in built:
            target = "/%s/%s" % (COMMANDS_DIRECTORY, name)
            _debugfs(volume, "rm %s" % target, "an old command", optional=True)
            _debugfs(volume, "write %s %s" % (path, target), "command " + name)

        _mkdir(volume, "/%s" % SERVICES_DIRECTORY,
               "the services directory")
        _clear_directory(volume, "/%s" % SERVICES_DIRECTORY)
        for name, path in service_images:
            target = "/%s/%s" % (SERVICES_DIRECTORY, name)
            _debugfs(volume, "rm %s" % target, "an old service",
                     optional=True)
            _debugfs(volume, "write %s %s" % (path, target),
                     "service " + name)

        _mkdir(volume, "/%s" % LIBS_DIRECTORY,
               "the shared Kit directory")
        _clear_directory(volume, "/%s" % LIBS_DIRECTORY)
        if terminfo is not None:
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
        _clear_directory(volume, "/%s" % APPS_DIRECTORY)
        for bundle in application_bundles:
            _install_bundle(volume, bundle,
                            "/%s/%s" % (APPS_DIRECTORY,
                                         os.path.basename(bundle)))

        _mkdir(volume, "/%s" % STARTUP_DIRECTORY,
               "the startup directory")
        _clear_directory(volume, "/%s" % STARTUP_DIRECTORY)
        manifest = os.path.join(temporary, STARTUP_NAME)
        with open(manifest, "w", encoding="ascii", newline="\n") as handle:
            handle.write(manifest_text)
        target = "/%s/%s" % (STARTUP_DIRECTORY, STARTUP_NAME)
        _debugfs(volume, "rm %s" % target, "the old startup manifest",
                 optional=True)
        _debugfs(volume, "write %s %s" % (manifest, target),
                 "the startup manifest")

        _mkdir(volume, "/%s" % CONFIG_DIRECTORY,
               "the configuration directory")
        _clear_directory(volume, "/%s" % CONFIG_DIRECTORY)
        for scope in CONFIG_SCOPES:
            _mkdir(volume, "/%s/%s" % (CONFIG_DIRECTORY, scope),
                   "configuration scope")
        for path, contents in CONFIGURATION.items():
            components = path.split("/")
            for count in range(1, len(components)):
                directory = "/%s/%s" % (
                    CONFIG_DIRECTORY, "/".join(components[:count]))
                _mkdir(volume, directory, "configuration directory")
            host = os.path.join(temporary, path.replace("/", "-"))
            with open(host, "w", encoding="ascii", newline="\n") as handle:
                handle.write(contents)
            target = "/%s/%s" % (CONFIG_DIRECTORY, path)
            _debugfs(volume, "rm %s" % target, "old configuration",
                     optional=True)
            _debugfs(volume, "write %s %s" % (host, target),
                     "configuration " + path)

        # The shadowing pair. `which` is installed on both members under two
        # names: the shipped one stays where it is, and a copy goes into the
        # writable member under the same name as a shipped command, so a
        # lookup has a real choice to make and the gate can see which it made.
        _mkdir(volume, "/local", "the local directory")
        _mkdir(volume, "/%s" % LOCAL_COMMANDS_DIRECTORY,
               "the local commands directory")
        _clear_directory(volume, "/%s" % LOCAL_COMMANDS_DIRECTORY)
        for name, path in built:
            if name != "which":
                continue
            target = "/%s/%s" % (LOCAL_COMMANDS_DIRECTORY, "devices")
            _debugfs(volume, "rm %s" % target, "the old local command",
                     optional=True)
            _debugfs(volume, "write %s %s" % (path, target),
                     "a shadowing command")
        _reset_journal(volume)
        _splice(image, offset, volume)


if __name__ == "__main__":
    if len(sys.argv) == 2:
        install(sys.argv[1])
    elif len(sys.argv) == 3 and sys.argv[1] == "--display":
        install(sys.argv[2], service_names=DISPLAY_SERVICES,
                manifest_text=DISPLAY_STARTUP_MANIFEST)
    elif len(sys.argv) == 3 and sys.argv[1] == "--hostbench":
        install(sys.argv[2], service_names=HOSTBENCH_SERVICES,
                manifest_text=HOSTBENCH_STARTUP_MANIFEST)
    elif len(sys.argv) == 4 and sys.argv[1] == "--create":
        try:
            size_mib = int(sys.argv[3], 10)
        except ValueError as error:
            raise SystemExit("image size must be an integer MiB value") from error
        publish(sys.argv[2], size_mib * (1 << 20))
    else:
        raise SystemExit(
            "usage: astra_image.py [--display|--hostbench] IMAGE | "
            "--create IMAGE SIZE_MIB")
