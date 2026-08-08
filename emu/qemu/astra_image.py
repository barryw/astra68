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
import subprocess
import tempfile

REPOSITORY = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

CATALOG_NAME = "astra_events.cat"
DEFAULT_CATALOG = os.path.join(
    REPOSITORY, "sw/userspace/supervisor/build/m68k/astra_events.cat")

COMMANDS_DIRECTORY = "commands"
# The writable member of COMMANDS:, and the reason it exists in this gate: a
# command installed here shadows the one the system shipped, which is what a
# union is for and what nothing else on the volume can demonstrate.
LOCAL_COMMANDS_DIRECTORY = "local/commands"
DEFAULT_COMMANDS = os.path.join(REPOSITORY, "sw/userspace/commands/build/m68k")


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


def install(image, catalog=None, commands=None):
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
    if not os.path.exists(catalog):
        raise RuntimeError("no catalog at %s -- build the supervisor first" %
                           catalog)
    built = _commands(commands)
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
        _debugfs(volume, "mkdir /%s" % COMMANDS_DIRECTORY, "the commands "
                 "directory", optional=True)
        for name, path in built:
            target = "/%s/%s" % (COMMANDS_DIRECTORY, name)
            _debugfs(volume, "rm %s" % target, "an old command", optional=True)
            _debugfs(volume, "write %s %s" % (path, target), "command " + name)

        # The shadowing pair. `which` is installed on both members under two
        # names: the shipped one stays where it is, and a copy goes into the
        # writable member under the same name as a shipped command, so a
        # lookup has a real choice to make and the gate can see which it made.
        _debugfs(volume, "mkdir /local", "the local directory", optional=True)
        _debugfs(volume, "mkdir /%s" % LOCAL_COMMANDS_DIRECTORY,
                 "the local commands directory", optional=True)
        for name, path in built:
            if name != "which":
                continue
            target = "/%s/%s" % (LOCAL_COMMANDS_DIRECTORY, "devices")
            _debugfs(volume, "rm %s" % target, "the old local command",
                     optional=True)
            _debugfs(volume, "write %s %s" % (path, target),
                     "a shadowing command")
        _splice(image, offset, volume)
