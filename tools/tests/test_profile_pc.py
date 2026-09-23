#!/usr/bin/env python3

import argparse
import importlib.util
from pathlib import Path
import struct
import tempfile


path = Path(__file__).resolve().parents[1] / "profile_pc.py"
spec = importlib.util.spec_from_file_location("profile_pc", path)
profile_pc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile_pc)


image = profile_pc.parse_image("zsh@0+0x300000=/tmp/zsh")
assert image == {"name": "zsh", "base": 0, "span": 0x300000,
                 "path": "/tmp/zsh"}
try:
    profile_pc.parse_image("zsh=/tmp/zsh")
except argparse.ArgumentTypeError:
    pass
else:
    raise AssertionError("malformed image was accepted")
assert profile_pc.parse_stack_word("0x3fffd2ce+20") == (0x3fffd2ce, 20)
try:
    profile_pc.parse_stack_word("0x3fffd2ce")
except argparse.ArgumentTypeError:
    pass
else:
    raise AssertionError("stack word without an offset was accepted")

terminal = profile_pc.parse_image("terminal@0+0x109000=/tmp/terminal")
samples = [
    {"pc": 0x101000, "sr": 0, "urp": 0x1000, "srp": 0x9000},
    {"pc": 0x180000, "sr": 0, "urp": 0x2000, "srp": 0x9000},
    {"pc": 0x180004, "sr": 0, "urp": 0x2000, "srp": 0x9000},
    {"pc": 0x180008, "sr": 0x2000, "urp": 0x3000, "srp": 0x9000},
]
assert profile_pc.choose_root(samples, image, [terminal]) == 0x2000
assert profile_pc.choose_root(samples, image, [terminal], {0x1000}) == 0x2000
assert profile_pc.choose_root(
    samples, image, [terminal], signatures={"8192": "abcd"},
    target_signature="abcd") == 0x2000
exec_samples = [
    {"pc": 0x20001000, "sr": 0, "urp": 0x4000, "srp": 0x9000,
     "signature": "old"},
    {"pc": 0x20001004, "sr": 0, "urp": 0x4000, "srp": 0x9000,
     "signature": "new"},
]
assert profile_pc.choose_root(
    exec_samples, image, [], target_signature="new") == 0x4000
try:
    profile_pc.choose_root(
        samples, image, [terminal], signatures={"8192": "abcd"},
        target_signature="different")
except RuntimeError:
    pass
else:
    raise AssertionError("ambiguous target root was accepted")
try:
    profile_pc.choose_root(
        exec_samples, image, [], target_signature="missing")
except RuntimeError:
    pass
else:
    raise AssertionError("missing exec signature was accepted")

assert profile_pc.entering_loader(0x20000100, 0x00101000)
assert not profile_pc.entering_loader(0x20000104, 0x20000100)
assert not profile_pc.entering_loader(0x3ff42000, 0x00101000)


class StackMemory:
    def __init__(self, failure=False):
        self.failure = failure

    def virtual_memory(self, address, size):
        assert address == 0x707fec94
        assert size == 4
        if self.failure:
            raise RuntimeError("unmapped")
        return bytes.fromhex("0000005c")


assert profile_pc.read_stack_word(StackMemory(), 0x707fec80, 20) == 0x5c
assert profile_pc.read_stack_word(
    StackMemory(failure=True), 0x707fec80, 20) is None


class SampleQmp(StackMemory):
    def __init__(self, pc):
        super().__init__()
        self.pc = pc
        self.commands = []

    def registers(self):
        return self.pc, 0, 0x2000, 0x9000, 0, 0x707fec80

    def execute(self, command):
        self.commands.append(command)


ordinary_qmp = SampleQmp(0x00101000)
ordinary_samples = []
profile_pc.capture_sample(
    ordinary_qmp, ordinary_samples, {"8192": "target"}, 0x00100134,
    {0x2000: 0x00101000}, (0x3fffd2ce, 20))
assert ordinary_qmp.commands == []
assert "stack_word" not in ordinary_samples[0]
stack_qmp = SampleQmp(0x3fffd2ce)
stack_samples = []
profile_pc.capture_sample(
    stack_qmp, stack_samples, {"8192": "target"}, 0x00100134,
    {0x2000: 0x3fffd2ce}, (0x3fffd2ce, 20))
assert stack_qmp.commands == ["stop", "cont"]
assert stack_samples[0]["stack_word"] == 0x5c

symbols = [(0x1000, "first"), (0x1100, "second")]
assert profile_pc.resolve(symbols, 0x10ff) == "first"
assert profile_pc.resolve(symbols, 0x1100) == "second"

# An executable linked at its runtime base must not be rebased twice.  Absolute
# linker constants and data symbols are not executable code and must never win
# a sampled-PC lookup.
nm_text = """\
00001000 A ASTRA_PAGE_SIZE
00100134 t main
00102850 T _start
00295000 D global_data
"""
assert profile_pc.parse_symbols(nm_text, 0) == [
    (0x00100134, "main"), (0x00102850, "_start")]
assert profile_pc.parse_symbols(nm_text, 0x3fe00000) == [
    (0x3ff00134, "main"), (0x3ff02850, "_start")]

with tempfile.TemporaryDirectory() as directory:
    elf = Path(directory) / "image.elf"
    data = bytearray(0x80)
    data[:6] = b"\x7fELF\x01\x02"
    struct.pack_into(">HHIIIIIHHHHHH", data, 16,
                     2, 4, 1, 0x00100134, 52, 0, 0,
                     52, 32, 1, 0, 0, 0)
    struct.pack_into(">IIIIIIII", data, 52,
                     1, 0, 0x00100000, 0x00100000,
                     len(data), len(data), 5, 0x1000)
    elf.write_bytes(data)
    assert min(entry[2] for entry in
               profile_pc.elf_load_segments(str(elf))) == 0x00100000

user_sample = {"pc": 0x101000, "sr": 0, "urp": 0x2000,
               "signature": "target"}
kernel_sample = {**user_sample, "sr": 0x2000}
assert profile_pc.sample_matches(user_sample, 0x2000, "target", False)
assert not profile_pc.sample_matches(kernel_sample, 0x2000, "target", False)
assert profile_pc.sample_matches(kernel_sample, 0x2000, "target", True)
assert not profile_pc.sample_matches(user_sample, 0x3000, "target", True)
assert not profile_pc.sample_matches(user_sample, 0x2000, "other", True)
assert profile_pc.sample_matches_new(user_sample, {0x1000}, False)
assert not profile_pc.sample_matches_new(user_sample, {0x2000}, True)
assert not profile_pc.sample_matches_new(kernel_sample, {0x1000}, False)
assert profile_pc.sample_matches_new(kernel_sample, {0x1000}, True)

registers = """
D0 = 0000005c   A0 = 00000000
D7 = 707fed60   A7 = 707fec80
PC = 00123456   SR = 0000 T:0 I:0 UI -----
SSW 00000441 TCR 00008000 URP 03d25000 SRP 02419000
"""
match = profile_pc.REGISTER_PATTERN.search(registers)
assert match is not None
assert tuple(int(value, 16) for value in match.groups()) == (
    0x5c, 0x707fec80, 0x00123456, 0, 0x03d25000, 0x02419000)
assert profile_pc.REGISTER_PATTERN.search("PC missing") is None
assert profile_pc.REGISTER_PATTERN.search(
    "D0 = 0000005c PC = 00123456 SR = 0000 "
    "URP 03d25000 SRP 02419000") is None

mmu = """
URP: 0x0481d000
00100000 - 00100fff -> 04824000 - 04824fff W (4 KiB)
00101000 - 0010afff -> 04826000 - 0482ffff W (40 KiB)
"""
assert profile_pc.translate_mmu_address(mmu, 0x00100134, 16) == 0x04824134
try:
    profile_pc.translate_mmu_address(mmu, 0x00100ff8, 16)
except RuntimeError:
    pass
else:
    raise AssertionError("cross-range signature was accepted")

print("profile_pc tests: PASS")
