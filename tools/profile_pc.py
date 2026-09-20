#!/usr/bin/env python3
"""Capture QEMU guest-PC samples and turn them into a flat symbol profile."""

import argparse
import bisect
import json
import re
import socket
import struct
import subprocess
import time
from pathlib import Path


REGISTER_PATTERN = re.compile(
    r"PC = ([0-9a-fA-F]{8}).*?SR = ([0-9a-fA-F]{4}).*?"
    r"URP ([0-9a-fA-F]{8}) SRP ([0-9a-fA-F]{8})", re.DOTALL)
MMU_RANGE_PATTERN = re.compile(
    r"^([0-9a-fA-F]{8}) - ([0-9a-fA-F]{8}) -> "
    r"([0-9a-fA-F]{8}) - [0-9a-fA-F]{8}", re.MULTILINE)
LOADER_START = 0x20000000
LOADER_END = 0x20100000


class Qmp:
    def __init__(self, path):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.connect(path)
        self.file = self.socket.makefile("rw")
        json.loads(self.file.readline())
        self.execute("qmp_capabilities")

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write(json.dumps(request) + "\n")
        self.file.flush()
        while True:
            reply = json.loads(self.file.readline())
            if "event" in reply:
                continue
            if "return" in reply:
                return reply["return"]
            raise RuntimeError(reply["error"])

    def registers(self):
        text = self.execute("human-monitor-command",
                            {"command-line": "info registers"})
        match = REGISTER_PATTERN.search(text)
        if match is None:
            raise RuntimeError("QEMU returned no MC68040 PC/SR/URP registers")
        return tuple(int(value, 16) for value in match.groups())

    def double_click(self, x, y):
        self.execute("input-send-event", {"events": [
            {"type": "abs", "data": {"axis": "x", "value": x}},
            {"type": "abs", "data": {"axis": "y", "value": y}},
        ]})
        for _ in range(2):
            for down in (True, False):
                self.execute("input-send-event", {"events": [{
                    "type": "btn",
                    "data": {"button": "left", "down": down},
                }]})

    def physical_memory(self, address, size):
        text = self.execute("human-monitor-command", {"command-line":
            "xp /%dbx 0x%x" % (size, address)})
        values = re.findall(r"0x([0-9a-fA-F]{2})(?![0-9a-fA-F])", text)
        if len(values) != size:
            raise RuntimeError("QEMU returned %d of %d signature bytes" %
                               (len(values), size))
        return bytes(int(value, 16) for value in values)

    def virtual_memory(self, address, size):
        text = self.execute("human-monitor-command",
                            {"command-line": "info tlb"})
        physical = translate_mmu_address(text, address, size)
        return self.physical_memory(physical, size)


def parse_image(value):
    match = re.fullmatch(
        r"([A-Za-z0-9_.-]+)@(0x[0-9a-fA-F]+|[0-9]+)"
        r"\+(0x[0-9a-fA-F]+|[0-9]+)=(.+)", value)
    if match is None:
        raise argparse.ArgumentTypeError(
            "image must be NAME@BASE+SPAN=ELF")
    name, base, span, path = match.groups()
    base = int(base, 0)
    span = int(span, 0)
    if span == 0:
        raise argparse.ArgumentTypeError("image span must be nonzero")
    return {"name": name, "base": base, "span": span, "path": path}


def translate_mmu_address(text, address, size):
    for match in MMU_RANGE_PATTERN.finditer(text):
        virtual_start, virtual_end, physical_start = (
            int(value, 16) for value in match.groups())
        if virtual_start <= address and \
                address + size - 1 <= virtual_end:
            return physical_start + address - virtual_start
    raise RuntimeError("0x%x is absent from QEMU's current MMU map" % address)


def parse_symbols(text, bias):
    symbols = []
    for line in text.splitlines():
        fields = line.split(None, 2)
        if len(fields) == 3 and fields[1] in "TtWw" and \
                re.fullmatch(r"[0-9a-fA-F]+", fields[0]):
            symbols.append((int(fields[0], 16) + bias, fields[2]))
    return symbols


def elf_load_segments(path):
    data = Path(path).read_bytes()
    if data[:6] != b"\x7fELF\x01\x02":
        raise RuntimeError(path + " is not a big-endian ELF32 image")
    header = struct.unpack_from(">HHIIIIIHHHHHH", data, 16)
    program_offset, program_size, program_count = header[4], header[8], header[9]
    segments = []
    for index in range(program_count):
        entry = struct.unpack_from(">IIIIIIII", data,
                                   program_offset + index * program_size)
        if entry[0] == 1:
            segments.append(entry)
    if not segments:
        raise RuntimeError(path + " has no loadable segments")
    return segments


def read_symbols(image, nm):
    result = subprocess.run(
        [nm, "-n", "--defined-only", image["path"]], text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode != 0:
        raise RuntimeError(result.stderr.strip() or
                           "nm failed for " + image["path"])
    link_base = min(entry[2] for entry in elf_load_segments(image["path"]))
    symbols = parse_symbols(result.stdout, image["base"] - link_base)
    if not symbols:
        raise RuntimeError("no symbols in " + image["path"])
    return symbols


def image_contains(image, pc):
    return image["base"] <= pc < image["base"] + image["span"]


def choose_root(samples, target, excludes, baseline_roots=(),
                signatures=None, target_signature=None):
    counts = {}
    for sample in samples:
        pc = sample["pc"]
        if sample["sr"] & 0x2000:
            continue
        root = sample["urp"]
        if root in baseline_roots:
            continue
        if target_signature is not None:
            signature = sample.get("signature",
                                   (signatures or {}).get(str(root)))
            if signature != target_signature:
                continue
        elif not image_contains(target, pc) or \
                any(image_contains(image, pc) for image in excludes):
            continue
        counts[root] = counts.get(root, 0) + 1
    if not counts:
        raise RuntimeError("no target-unique user samples identified a process")
    return max(counts, key=counts.get)


def resolve(symbols, pc):
    addresses = [entry[0] for entry in symbols]
    index = bisect.bisect_right(addresses, pc) - 1
    return symbols[index][1] if index >= 0 else "<before first symbol>"


def sample_matches(sample, root, target_signature, include_kernel):
    if sample["urp"] != root or \
            (sample["sr"] & 0x2000 and not include_kernel):
        return False
    return target_signature is None or \
        sample.get("signature", target_signature) == target_signature


def sample_matches_new(sample, baseline_roots, include_kernel):
    return sample["urp"] not in baseline_roots and \
        (include_kernel or not sample["sr"] & 0x2000)


def elf_bytes_at(path, address, size):
    data = Path(path).read_bytes()
    for entry in elf_load_segments(path):
        _, file_offset, virtual, _, file_size = entry[:5]
        if virtual <= address and \
                address + size <= virtual + file_size:
            at = file_offset + address - virtual
            return data[at:at + size]
    raise RuntimeError("0x%x is not backed by %s" % (address, path))


def entering_loader(pc, previous_pc):
    return LOADER_START <= pc < LOADER_END and not (
        previous_pc is not None and LOADER_START <= previous_pc < LOADER_END)


def capture_sample(qmp, samples, signatures, signature_address, previous):
    pc, sr, urp, srp = qmp.registers()
    if str(urp) not in signatures or entering_loader(pc, previous.get(urp)):
        qmp.execute("stop")
        try:
            pc, sr, urp, srp = qmp.registers()
            if str(urp) not in signatures or \
                    entering_loader(pc, previous.get(urp)):
                signatures[str(urp)] = qmp.virtual_memory(
                    signature_address, 16).hex()
        finally:
            qmp.execute("cont")
    previous[urp] = pc
    samples.append({"pc": pc, "sr": sr, "urp": urp, "srp": srp,
                    "signature": signatures[str(urp)]})
    return urp


def capture(arguments):
    qmp = Qmp(arguments.qmp)
    samples = []
    signatures = {}
    previous = {}
    baseline_roots = set()
    if arguments.double_click is not None:
        deadline = time.monotonic() + arguments.baseline
        while time.monotonic() < deadline:
            baseline_roots.add(capture_sample(
                qmp, samples, signatures, arguments.signature_address,
                previous))
            if arguments.interval != 0:
                time.sleep(arguments.interval / 1000.0)
    if arguments.double_click is not None:
        qmp.double_click(*arguments.double_click)
    started = time.monotonic()
    deadline = started + arguments.duration
    while time.monotonic() < deadline:
        capture_sample(qmp, samples, signatures, arguments.signature_address,
                       previous)
        if arguments.interval != 0:
            time.sleep(arguments.interval / 1000.0)
    elapsed = time.monotonic() - started
    Path(arguments.output).write_text(json.dumps({
        "format": "astra-pc-profile-v1",
        "elapsed_seconds": elapsed,
        "baseline_roots": sorted(baseline_roots),
        "signature_address": arguments.signature_address,
        "signatures": signatures,
        "samples": samples,
    }) + "\n", encoding="utf-8")
    print("captured %d samples in %.3f seconds (%.1f samples/s)" %
          (len(samples), elapsed, len(samples) / elapsed))


def report(arguments):
    profile = json.loads(Path(arguments.profile).read_text(encoding="utf-8"))
    if profile.get("format") != "astra-pc-profile-v1" or \
            not isinstance(profile.get("samples"), list):
        raise RuntimeError("not an Astra PC profile")
    baseline_roots = set(profile.get("baseline_roots", []))
    signature_address = profile.get("signature_address")
    target_signature = None if arguments.all_new or \
        signature_address is None else elf_bytes_at(
            arguments.target["path"], signature_address, 16).hex()
    root = arguments.root
    if root is None and not arguments.all_new:
        root = choose_root(profile["samples"], arguments.target,
                           arguments.exclude,
                           baseline_roots,
                           profile.get("signatures"), target_signature)
    images = [arguments.target] + arguments.image
    symbol_tables = {image["name"]: read_symbols(image, arguments.nm)
                     for image in images}
    counts = {}
    selected_samples = 0
    for sample in profile["samples"]:
        matches = sample_matches_new(
            sample, baseline_roots, arguments.include_kernel) \
            if arguments.all_new else sample_matches(
                sample, root, target_signature, arguments.include_kernel)
        if not matches:
            continue
        selected_samples += 1
        pc = sample["pc"]
        image = next((item for item in images if image_contains(item, pc)),
                     None)
        if image is None:
            key = ("<unmapped>", "0x%08x" % pc)
        else:
            key = (image["name"], resolve(symbol_tables[image["name"]], pc))
        counts[key] = counts.get(key, 0) + 1
    if selected_samples == 0:
        raise RuntimeError("selected process has no matching samples")
    label = "samples" if arguments.include_kernel else "user_samples"
    selected = "roots=all-new" if arguments.all_new else "root=0x%08x" % root
    print("%s %s=%d total_samples=%d" %
          (selected, label, selected_samples, len(profile["samples"])))
    print(" samples percent image                 symbol")
    for (image, symbol), count in sorted(
            counts.items(), key=lambda item: (-item[1], item[0])):
        print("%8d %6.2f%% %-21s %s" %
              (count, count * 100.0 / selected_samples, image, symbol))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    capture_parser = subparsers.add_parser("capture")
    capture_parser.add_argument("output")
    capture_parser.add_argument("--qmp", default="/run/astra/qmp.sock")
    capture_parser.add_argument("--duration", type=float, default=2.0)
    capture_parser.add_argument("--baseline", type=float, default=0.25,
                                help="seconds sampled before the launch trigger")
    capture_parser.add_argument("--interval", type=float, default=1.0,
                                help="milliseconds between samples; zero is continuous")
    capture_parser.add_argument("--double-click", nargs=2, type=int,
                                metavar=("X", "Y"))
    capture_parser.add_argument("--signature-address", type=lambda value:
                                int(value, 0), default=0x00100134)
    capture_parser.set_defaults(action=capture)

    report_parser = subparsers.add_parser("report")
    report_parser.add_argument("profile")
    report_parser.add_argument("--target", required=True, type=parse_image)
    report_parser.add_argument("--exclude", action="append", default=[],
                               type=parse_image)
    report_parser.add_argument("--image", action="append", default=[],
                               type=parse_image)
    report_parser.add_argument("--root", type=lambda value: int(value, 0))
    report_parser.add_argument("--all-new", action="store_true",
                               help="aggregate roots absent before the trigger")
    report_parser.add_argument("--nm", default="m68k-astra-nm")
    report_parser.add_argument("--include-kernel", action="store_true",
                               help="include supervisor-mode samples")
    report_parser.set_defaults(action=report)

    arguments = parser.parse_args()
    if arguments.command == "report" and arguments.root is not None and \
            arguments.all_new:
        parser.error("--root and --all-new are mutually exclusive")
    if arguments.command == "capture" and arguments.duration <= 0:
        parser.error("--duration must be positive")
    if arguments.command == "capture" and arguments.interval < 0:
        parser.error("--interval cannot be negative")
    if arguments.command == "capture" and arguments.baseline < 0:
        parser.error("--baseline cannot be negative")
    arguments.action(arguments)


if __name__ == "__main__":
    main()
