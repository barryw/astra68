#!/usr/bin/env python3
"""Verify that bundle metadata matches the libraries it packages."""

import argparse
import pathlib
import sys

import library_info


def version(text):
    parts = text.split(".")
    if len(parts) != 3 or any(not part.isdigit() for part in parts):
        raise ValueError("invalid version %s" % text)
    return tuple(map(int, parts))


def manifest_records(path):
    records = []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        fields = line.split()
        if not fields or fields[0] not in ("provides", "requires"):
            continue
        if len(fields) != 4 or not fields[2].isdigit():
            raise ValueError("%s:%d: malformed %s" % (path, number, fields[0]))
        records.append((fields[0], fields[1], int(fields[2]),
                        version(fields[3]), path))
    return records


def validate(libraries, manifests):
    errors = []
    actual = {}
    providers = {}
    requirements = []

    for library, path in libraries:
        suffix = ".%d" % library["abi_major"]
        if not library["name"].endswith(suffix):
            errors.append("%s: SONAME does not end in ABI %s" % (path, suffix))
            continue
        key = (library["name"][:-len(suffix)], library["abi_major"])
        actual[(key, version(library["version"]))] = path

    for path in manifests:
        try:
            records = manifest_records(path)
        except ValueError as error:
            errors.append(str(error))
            continue
        for kind, name, abi, item_version, source in records:
            if kind == "provides":
                providers.setdefault((name, abi), []).append(
                    (item_version, source))
            else:
                requirements.append((name, abi, item_version, source))

    for (key, item_version), path in actual.items():
        matches = [provider for provider in providers.get(key, ())
                   if provider[0] == item_version]
        if len(matches) != 1:
            errors.append("%s: %s ABI %d version %s has %d exact providers" %
                          (path, key[0], key[1], ".".join(map(str, item_version)),
                           len(matches)))

    for key, entries in providers.items():
        if len(entries) != 1:
            errors.append("%s ABI %d has %d providers" %
                          (key[0], key[1], len(entries)))
        for item_version, source in entries:
            if (key, item_version) not in actual:
                errors.append("%s: provider %s ABI %d version %s has no "
                              "matching library" %
                              (source, key[0], key[1],
                               ".".join(map(str, item_version))))

    for name, abi, minimum, source in requirements:
        candidates = providers.get((name, abi), ())
        if not any(item_version >= minimum for item_version, _ in candidates):
            errors.append("%s: requires unavailable %s ABI %d version %s" %
                          (source, name, abi, ".".join(map(str, minimum))))
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", action="append", default=[], type=pathlib.Path)
    parser.add_argument("--manifest", action="append", default=[], type=pathlib.Path)
    arguments = parser.parse_args(argv)
    try:
        libraries = [(library_info.read(path), path)
                     for path in arguments.library]
        errors = validate(libraries, arguments.manifest)
    except (library_info.CatalogError, library_info.LibraryError,
            OSError, ValueError) as error:
        errors = [str(error)]
    if errors:
        for error in errors:
            print("bundle dependency coverage: %s" % error, file=sys.stderr)
        return 1
    print("astra bundle dependency coverage: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
