#!/usr/bin/env python3
"""Aggregate QEMU -perfmap guest PCs into MC68030 source functions."""

import argparse
import re
import subprocess


GUEST = re.compile(r"^\s*([0-9.]+)%.*\bguest-0x([0-9a-fA-F]+)\b")


def guest_samples(lines):
    samples = {}
    for line in lines:
        match = GUEST.match(line)
        if match is None:
            continue
        address = int(match.group(2), 16)
        samples[address] = samples.get(address, 0.0) + float(match.group(1))
    return samples


def image_for(address, user, kernel, rom):
    if address >= 0xFFE00000:
        return "rom", rom
    if 0x02000000 <= address < 0x03000000:
        return "kernel", kernel
    return "user", user


def library_spec(value):
    try:
        base, image = value.split("=", 1)
        return int(base, 0), image
    except (ValueError, TypeError):
        raise argparse.ArgumentTypeError("expected BASE=ELF")


def resolve(addr2line, image, addresses):
    if image is None or not addresses:
        return {address: ("??", "??:0") for address in addresses}
    command = [addr2line, "-f", "-C", "-e", image]
    command.extend("0x%x" % address for address in addresses)
    result = subprocess.run(command, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        raise RuntimeError("addr2line failed: %s" % result.stderr.strip())
    lines = result.stdout.splitlines()
    if len(lines) != 2 * len(addresses):
        raise RuntimeError("addr2line returned an incomplete result")
    return {address: (lines[index * 2], lines[index * 2 + 1])
            for index, address in enumerate(addresses)}


def self_test():
    parsed = guest_samples([
        "  1.25% qemu [JIT] tid 7 [.] guest-0x111d04\n",
        "  0.75% qemu [JIT] tid 7 [.] guest-0x111d04  -  -\n",
        "  9.00% qemu qemu-system [.] cpu_exec_loop\n",
    ])
    assert parsed == {0x111D04: 2.0}
    assert image_for(0x111D04, "u", "k", "r") == ("user", "u")
    assert image_for(0x2044000, "u", "k", "r") == ("kernel", "k")
    assert image_for(0xFFE00000, "u", "k", "r") == ("rom", "r")
    assert library_spec("0x22000000=filesystem.library") == (
        0x22000000, "filesystem.library")
    print("guest perf symbolizer: PASS")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("report", nargs="?")
    parser.add_argument("--user")
    parser.add_argument("--kernel")
    parser.add_argument("--rom")
    parser.add_argument("--library", action="append", type=library_spec,
                        default=[], metavar="BASE=ELF")
    parser.add_argument("--addr2line", default="m68k-astra-addr2line")
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.report is None or args.user is None or args.kernel is None or \
            args.limit < 1:
        parser.error("REPORT, --user, --kernel, and a positive --limit are required")

    with open(args.report, encoding="utf-8") as stream:
        samples = guest_samples(stream)
    mappings = []
    for address in samples:
        mapping = next((("library", image, base)
                        for base, image in args.library
                        if base <= address < base + 0x01000000), None)
        if mapping is None:
            kind, image = image_for(address, args.user, args.kernel, args.rom)
            mapping = kind, image, 0
        mappings.append((address, mapping))
    grouped = {}
    for mapping in sorted(set(item[1] for item in mappings)):
        kind, image, base = mapping
        addresses = sorted(address for address, owner in mappings
                           if owner == mapping)
        relative = [address - base for address in addresses]
        resolved = resolve(args.addr2line, image, relative)
        for address in addresses:
            function, source = resolved[address - base]
            key = (kind, function, source)
            grouped[key] = grouped.get(key, 0.0) + samples[address]

    print("guest samples: %.2f%%" % sum(samples.values()))
    for (kind, function, source), percent in sorted(
            grouped.items(), key=lambda item: item[1], reverse=True)[:args.limit]:
        print("%6.2f%%  %-6s  %-40s %s" %
              (percent, kind, function, source))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
