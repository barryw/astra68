#!/usr/bin/env python3
"""Compare stopped reference and installed-copy user state without changing either."""

import hashlib
import os
import re
import stat
import subprocess
import sys
import tempfile

import astra_image
from storage_image import image_window


ROOTS = ("/home", "/local", "/config")


def digest(path):
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(descriptor, "rb") as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            raise RuntimeError("observer requires a regular file: %r" % path)
        checksum = hashlib.sha256()
        length = 0
        for block in iter(lambda: source.read(1 << 20), b""):
            checksum.update(block)
            length += len(block)
        return length, checksum.hexdigest()


def debugfs(volume, request):
    result = subprocess.run(["debugfs", "-R", request, volume],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            env={**os.environ, "LC_ALL": "C", "TZ": "UTC"})
    # debugfs commonly exits zero on failure. Only its version banner belongs
    # on stderr; a query's stdout is parsed completely by its caller.
    if result.returncode or any(not re.fullmatch(rb"debugfs [^\r\n]+", line)
                               for line in result.stderr.splitlines()):
        raise RuntimeError("debugfs refused %s: %r" % (request, result.stderr))
    return result.stdout


def directory_entries(volume, path):
    raw = debugfs(volume, "ls -p %s" % astra_image._debugfs_quote(path))
    expected = []
    dots = set()
    try:
        text = raw.decode("utf-8", "strict")
        for line in text.splitlines():
            if not line:
                continue
            fields = line.split("/")
            if (len(fields) != 8 or fields[0] or fields[-1] or
                    not all(value.isdigit() for value in fields[1:5]) or
                    (fields[6] and not fields[6].isdigit())):
                raise ValueError("invalid directory record")
            name = fields[5]
            if (not name or any(ord(char) < 32 or ord(char) == 127 for char in name)):
                raise ValueError("unrepresentable entry name")
            mode = int(fields[2], 8)
            if int(fields[1]) == 0:
                raise ValueError("unallocated directory entry")
            if name not in (".", ".."):
                expected.append((name, stat.S_ISDIR(mode)))
            else:
                dots.add(name)
    except (UnicodeError, ValueError) as error:
        raise RuntimeError("unrepresentable directory %r: %r" % (path, raw)) from error
    if len({name for name, _ in expected}) != len(expected):
        raise RuntimeError("duplicate directory entry in %r" % path)
    if dots != {".", ".."}:
        raise RuntimeError("incomplete directory observation: %r" % path)
    # Reuse the publisher's traversal contract after proving its text decoder
    # cannot replace bytes or silently ignore an observer entry.
    if astra_image._directory_entries(volume, path) != expected:
        raise RuntimeError("directory observation disagrees: %r" % path)
    return expected


def inode_metadata(volume, path):
    output = debugfs(volume, "stat %s" % astra_image._debugfs_quote(path))
    # stat prints a fast symlink's target verbatim, and attribute display text
    # is user-controlled too. Neither may inject apparent inode header fields.
    header = output.split(b"Fast link dest:", 1)[0].split(b"Extended attributes:", 1)[0]
    patterns = {
        "inode": rb"^Inode:\s+(\d+)",
        "type": rb"\bType:\s+(\S+)\s+Mode:",
        "mode": rb"\bMode:\s+(\S+)",
        "flags": rb"\bFlags:\s+(\S+)",
        "generation": rb"^Generation:\s+(\d+)",
        "uid": rb"\bUser:\s+(\d+)", "gid": rb"\bGroup:\s+(\d+)",
        "size": rb"^User:.*\bSize:\s+(\d+)",
    }
    metadata = {}
    for name, pattern in patterns.items():
        matches = re.findall(pattern, header, re.MULTILINE)
        if len(matches) != 1:
            raise RuntimeError("unreadable inode %r (%s): %r" % (path, name, output))
        metadata[name] = matches[0].decode("ascii")
    if metadata["type"] not in ("directory", "regular", "symlink"):
        raise RuntimeError("unsupported observer inode type %s: %r" % (metadata["type"], path))
    times = re.findall(rb"^\s*(ctime|atime|mtime|crtime):\s*(0x[0-9a-fA-F]+(?::[0-9a-fA-F]+)?)\s+--", header, re.MULTILINE)
    if len(times) != 4 or {name for name, _ in times} != {b"ctime", b"atime", b"mtime", b"crtime"}:
        raise RuntimeError("incomplete inode timestamps: %r" % path)
    metadata.update((name.decode("ascii"), value.decode("ascii")) for name, value in times)
    return metadata


def attributes(volume, inode, temporary):
    output = debugfs(volume, "ea_list <%s>" % inode)
    lines = output.splitlines()
    if not lines:
        return {}
    if lines.pop(0) != b"Extended attributes:":
        raise RuntimeError("invalid attribute listing for inode %s: %r" % (inode, output))
    result = {}
    for line in lines:
        # Attribute names requiring debugfs display escapes are refused, never
        # mistaken for the literal escaped spelling. Values remain arbitrary bytes.
        match = re.fullmatch(rb'  ([^\s\\"\x00-\x1f\x7f-\xff]+) \((\d+)\)(?: = .*)?', line)
        if match is None:
            raise RuntimeError("unrepresentable attribute name for inode %s: %r" % (inode, line))
        name = match[1].decode("ascii")
        if name in result:
            raise RuntimeError("duplicate attribute %r on inode %s" % (name, inode))
        target = os.path.join(temporary, "attribute")
        debugfs(volume, "ea_get -r -f %s <%s> %s" % (
            astra_image._debugfs_quote(target), inode, astra_image._debugfs_quote(name)))
        value = digest(target)
        if value[0] != int(match[2]):
            raise RuntimeError("short attribute read %r on inode %s" % (name, inode))
        result[name] = value
        os.unlink(target)
    return result


def observe(volume, temporary):
    check = subprocess.run(["e2fsck", "-fn", volume],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           env={**os.environ, "LC_ALL": "C"})
    if check.returncode:
        raise RuntimeError("state filesystem is not clean: %r" % check.stdout)
    entries = {}
    visited_directories = set()
    pending = [root for root in ROOTS if astra_image._inode_type(volume, root) is not None]
    while pending:
        path = pending.pop()
        metadata = inode_metadata(volume, path)
        inode, kind = metadata["inode"], metadata["type"]
        metadata["xattrs"] = attributes(volume, inode, temporary)
        if kind == "directory":
            if inode in visited_directories:
                raise RuntimeError("repeated directory inode %s: %r" % (inode, path))
            visited_directories.add(inode)
            pending.extend(path + "/" + name for name, _ in directory_entries(volume, path))
            # New default children can grow directory size; compare the existing
            # directory's identity, permissions, attributes and timestamps instead.
            del metadata["size"]
        elif kind == "regular":
            target = os.path.join(temporary, "content")
            debugfs(volume, "dump <%s> %s" % (inode, astra_image._debugfs_quote(target)))
            metadata["content"] = digest(target)
            os.unlink(target)
            if metadata["content"][0] != int(metadata["size"]):
                raise RuntimeError("short file read: %r" % path)
        else:
            # A single symlink inode, not a recursive extraction. readlink takes
            # a byte pathname and returns the target bytes without following it.
            target = os.path.join(temporary, "<%s>" % inode)
            debugfs(volume, "rdump <%s> %s" % (inode, astra_image._debugfs_quote(temporary)))
            if not stat.S_ISLNK(os.lstat(target).st_mode):
                raise RuntimeError("symlink extraction changed type: %r" % path)
            metadata["target"] = os.readlink(os.fsencode(target))
            os.unlink(target)
            if len(metadata["target"]) != int(metadata["size"]):
                raise RuntimeError("short symlink read: %r" % path)
        entries[path] = metadata
    return entries


def defaults():
    result = {"/home": None, "/local": None, "/local/commands": None,
              "/config": None}
    result.update(("/config/" + scope, None) for scope in astra_image.CONFIG_SCOPES)
    for path, contents in astra_image.CONFIGURATION.items():
        parts = ("/config/" + path).split("/")
        result.update(("/".join(parts[:count]), None) for count in range(2, len(parts)))
        result["/config/" + path] = contents.encode("ascii")
    return result


def verify(reference, candidate):
    if os.path.samefile(reference, candidate):
        raise RuntimeError("reference and candidate must be separate image copies")
    before_hashes = tuple(digest(path) for path in (reference, candidate))
    try:
        windows = tuple(image_window(path) for path in (reference, candidate))
        if windows[0] != windows[1]:
            raise RuntimeError("system volume identity/window changed")
        snapshots = []
        with tempfile.TemporaryDirectory(prefix="astra-state-observer-") as temporary:
            volume = os.path.join(temporary, "volume.ext4")
            for image, (offset, length, _) in zip((reference, candidate), windows):
                astra_image._slice(image, offset, length, volume)
                if os.path.getsize(volume) != length:
                    raise RuntimeError("short system partition extraction")
                snapshots.append(observe(volume, temporary))
        before, after = snapshots
        for path, metadata in before.items():
            if path not in after:
                raise RuntimeError("preserved entry disappeared: %r" % path)
            for field, value in metadata.items():
                if after[path].get(field) != value:
                    raise RuntimeError("preserved %s changed: %r" % (field, path))
        allowed = defaults()
        for path in after.keys() - before.keys():
            if path not in allowed:
                raise RuntimeError("unexpected state addition: %r" % path)
        for path, contents in allowed.items():
            if path in before:
                continue
            if path not in after:
                raise RuntimeError("missing installer default: %r" % path)
            if contents is None:
                if after[path]["type"] != "directory":
                    raise RuntimeError("default directory changed type: %r" % path)
            elif (after[path]["type"] != "regular" or
                  after[path]["content"] != (len(contents), hashlib.sha256(contents).hexdigest())):
                raise RuntimeError("incorrect installer default contents: %r" % path)
        return len(before), len(after) - len(before)
    finally:
        if tuple(digest(path) for path in (reference, candidate)) != before_hashes:
            raise RuntimeError("observer input image changed")


def main():
    if len(sys.argv) != 3:
        raise RuntimeError("usage: verify-state-preservation.py REFERENCE.img CANDIDATE.img")
    preserved, added = verify(*sys.argv[1:])
    print("state preservation: %d existing entries retained; %d missing defaults seeded; input hashes unchanged PASS" % (preserved, added))


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit("state preservation: " + str(error))
