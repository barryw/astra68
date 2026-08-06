"""The ROM refuses a payload that carries debug information.

Every m68k object is built with -g now, so the only thing standing between
DWARF and a fixed 256 KiB ROM window is the strip step in the user image's
build. A rule nothing checks is a rule that lasts until someone reorders a
Makefile, and the failure it would produce -- a payload that no longer fits --
says nothing about its cause.
"""

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pack_payload import debug_sections, pack


def build_elf(section_names):
    """A minimal big-endian 32-bit ELF carrying the named sections."""
    entry_size = 40
    names = b"\0" + b"\0".join(name.encode() for name in section_names) + b"\0"
    count = len(section_names) + 1          # the section-name table itself
    header_size = 52
    names_offset = header_size + count * entry_size

    header = bytearray(header_size)
    header[0:4] = b"\x7fELF"
    header[4] = 1                            # 32-bit
    header[5] = 2                            # big-endian
    struct.pack_into(">H", header, 16, 1)    # type
    struct.pack_into(">H", header, 18, 4)    # machine: m68k
    struct.pack_into(">I", header, 32, header_size)   # section header offset
    struct.pack_into(">H", header, 46, entry_size)
    struct.pack_into(">H", header, 48, count)
    struct.pack_into(">H", header, 50, count - 1)     # names section index

    sections = bytearray()
    offset = 1
    for name in section_names:
        entry = bytearray(entry_size)
        struct.pack_into(">I", entry, 0, offset)
        sections += entry
        offset += len(name) + 1
    names_entry = bytearray(entry_size)
    struct.pack_into(">I", names_entry, 0, 0)
    struct.pack_into(">I", names_entry, 16, names_offset)
    sections += names_entry

    return bytes(header) + bytes(sections) + names


def test_stripped_image_is_accepted():
    assert debug_sections(build_elf([".text", ".rodata", ".bss"])) == []


def test_debug_sections_are_named():
    found = debug_sections(build_elf([".text", ".debug_info", ".debug_line"]))
    assert found == [".debug_info", ".debug_line"]


def test_raw_binary_is_not_mistaken_for_an_elf():
    """The kernel payload is a raw binary, not an ELF, and must pass."""
    assert debug_sections(b"\x00\x01\x02\x03" * 64) == []
    assert debug_sections(b"") == []
    assert debug_sections(b"\x7fELF") == []


def test_little_endian_elf_is_left_alone():
    """Not a payload this ROM can carry; refusing it here would be a lie."""
    image = bytearray(build_elf([".debug_info"]))
    image[5] = 1
    assert debug_sections(bytes(image)) == []


def test_pack_refuses_an_unstripped_image(tmp_path):
    source = tmp_path / "image.elf"
    source.write_bytes(build_elf([".text", ".debug_info"]))
    with pytest.raises(SystemExit) as refusal:
        pack("USER", source, tmp_path / "image.lz4")
    assert "debug information" in str(refusal.value)
    assert ".debug_info" in str(refusal.value)


def test_pack_accepts_a_stripped_image(tmp_path):
    source = tmp_path / "image.elf"
    source.write_bytes(build_elf([".text", ".rodata"]))
    result = pack("USER", source, tmp_path / "image.lz4")
    assert result["raw_bytes"] == len(source.read_bytes())
    assert result["compressed_bytes"] > 0


def test_pack_still_refuses_an_empty_payload(tmp_path):
    source = tmp_path / "empty.bin"
    source.write_bytes(b"")
    with pytest.raises(SystemExit) as refusal:
        pack("USER", source, tmp_path / "empty.lz4")
    assert "empty payload" in str(refusal.value)
