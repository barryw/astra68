#!/usr/bin/env python3
"""Preparing a card image for a gate.

The only thing here is placing this build's event catalog on the volume. The
catalog is the `.astra_events` section verbatim and the ROM image strips that
section, so a running program does not carry its own text -- the events service
reads it from SYS:. Without it every line the events tree renders is a message
id, which is honest, useless as a check, and exactly what a stale catalog would
look like.

Shared by both QEMU gates rather than copied into each: a second copy of this
would be a second answer to where the catalog lives.
"""

import os
import subprocess
import tempfile

CATALOG_NAME = "astra_events.cat"
DEFAULT_CATALOG = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "sw/userspace/supervisor/build/m68k/astra_events.cat")


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


def install_catalog(image, catalog=None):
    """Writes the catalog into the image's volume, replacing any older one.

    The volume is lifted out of the image, worked on, and put back. Two reasons
    it is not done in place: e2fsck cannot reopen an `image?offset=` target
    after it recovers a journal, and the journal has to be recovered first --
    an image a machine has run carries transactions that are replayed at the
    next mount, and a replay lands on top of whatever debugfs wrote. The file
    is then on the host and absent on the machine, with nothing anywhere saying
    why. That one cost an hour; the copy costs a second.
    """
    catalog = catalog or DEFAULT_CATALOG
    if not os.path.exists(catalog):
        raise RuntimeError("no catalog at %s -- build the supervisor first" %
                           catalog)
    offset, length = ext4_partition(image)
    with tempfile.TemporaryDirectory(prefix="astra-volume-") as temporary:
        volume = os.path.join(temporary, "volume.img")
        _slice(image, offset, length, volume)

        fsck = subprocess.run(["e2fsck", "-fy", volume],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if fsck.returncode > 2:
            raise RuntimeError("e2fsck refused the volume: %s" %
                               fsck.stdout.decode("utf-8", "replace").strip())
        subprocess.run(["debugfs", "-w", "-R", "rm /%s" % CATALOG_NAME,
                        volume], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)
        result = subprocess.run(
            ["debugfs", "-w", "-R", "write %s %s" % (catalog, CATALOG_NAME),
             volume], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if result.returncode != 0:
            raise RuntimeError("debugfs refused the catalog: %s" %
                               result.stdout.decode("utf-8", "replace").strip())
        _splice(image, offset, volume)
