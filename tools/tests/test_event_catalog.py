"""The catalog extractor, against ELFs built here rather than by a compiler.

The point of building the fixture by hand is that it pins the *contract*: the
descriptor's byte layout, the section's base becoming the message id, and the
refusals. A fixture produced by the cross-compiler would test whatever the
compiler happened to emit, which is the thing the contract exists to constrain.
"""

import struct
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import event_catalog  # noqa: E402


def descriptor(subsystem=2, level=3, line=66, count=1, types=(1, 0, 0, 0),
               source="src/vfs_host.c", fmt="refused with status %u",
               magic=event_catalog.DESCRIPTOR_MAGIC):
    record = struct.pack(">IHBBB4s3x", magic, line, subsystem, level, count,
                         bytes(types))
    record += source.encode("ascii").ljust(event_catalog.FILE_MAX, b"\0")
    record += fmt.encode("ascii").ljust(event_catalog.FORMAT_MAX, b"\0")
    assert len(record) == event_catalog.DESCRIPTOR_SIZE
    return record


def build_elf(path, section, base=0xE0000000, name=".astra_events"):
    """A minimal big-endian ELF32 carrying one named section plus .shstrtab."""
    names = b"\0" + name.encode("ascii") + b"\0.shstrtab\0"
    name_at = 1
    shstrtab_at = 1 + len(name) + 1
    header_size, entry_size = 52, 40
    section_offset = header_size
    names_offset = section_offset + len(section)
    table_offset = names_offset + len(names)

    elf = bytearray()
    elf += b"\x7fELF" + bytes([1, 2, 1]) + bytes(9)      # ELF32, big-endian
    elf += struct.pack(">HHI", 2, 4, 1)                   # ET_EXEC, m68k
    elf += struct.pack(">III", 0, 0, table_offset)        # entry, phoff, shoff
    elf += struct.pack(">IHHHHHH", 0, header_size, 0, 0, entry_size, 3, 2)
    assert len(elf) == header_size

    elf += section
    elf += names

    def entry(name, kind, addr, offset, size):
        return struct.pack(">IIIIIIIIII", name, kind, 0, addr, offset, size,
                           0, 0, 4, 0)

    elf += entry(0, 0, 0, 0, 0)                                    # SHT_NULL
    elf += entry(name_at, 1, base, section_offset, len(section))   # the section
    elf += entry(shstrtab_at, 3, 0, names_offset, len(names))      # .shstrtab
    path.write_bytes(bytes(elf))
    return path


def test_a_descriptor_round_trips(tmp_path):
    elf = build_elf(tmp_path / "one.elf", descriptor())
    base, blob = event_catalog.read_section(str(elf))
    assert base == 0xE0000000
    catalog = event_catalog.parse(base, blob)

    assert list(catalog) == ["0xe0000000"]
    entry = catalog["0xe0000000"]
    assert entry["subsystem"] == "supervisor"
    assert entry["level"] == "warning"
    assert entry["file"] == "src/vfs_host.c"
    assert entry["line"] == 66
    assert entry["format"] == "refused with status %u"
    assert entry["arguments"] == ["u32"]


def test_the_message_id_is_the_base_plus_the_offset(tmp_path):
    """Which is what makes an id stable for the life of a build, and what a
    reader adds nothing to in order to resolve it."""
    section = descriptor(line=1) + descriptor(line=2) + descriptor(line=3)
    elf = build_elf(tmp_path / "three.elf", section, base=0xE0001000)
    catalog = event_catalog.parse(*event_catalog.read_section(str(elf)))

    assert [entry["line"] for _, entry in sorted(catalog.items())] == [1, 2, 3]
    assert sorted(catalog) == ["0xe0001000", "0xe0001080", "0xe0001100"]


def test_arguments_are_reported_only_as_far_as_the_count(tmp_path):
    section = descriptor(count=2, types=(1, 2, 4, 4))
    elf = build_elf(tmp_path / "args.elf", section)
    catalog = event_catalog.parse(*event_catalog.read_section(str(elf)))

    assert catalog["0xe0000000"]["arguments"] == ["u32", "s32"]


def test_a_wrong_magic_is_reported_not_skipped(tmp_path):
    """A descriptor that does not say AEVD means the section is not what this
    tool thinks it is, and reading on would produce a plausible wrong catalog."""
    elf = build_elf(tmp_path / "bad.elf", descriptor(magic=0xDEADBEEF))
    with pytest.raises(event_catalog.CatalogError, match="magic"):
        event_catalog.parse(*event_catalog.read_section(str(elf)))


def test_a_ragged_section_is_refused(tmp_path):
    """The descriptor changed shape and the tool did not."""
    elf = build_elf(tmp_path / "ragged.elf", descriptor()[:-8])
    with pytest.raises(event_catalog.CatalogError, match="multiple"):
        event_catalog.parse(*event_catalog.read_section(str(elf)))


def test_an_unknown_subsystem_is_refused(tmp_path):
    """A newer build's number, read by an older tool. Naming it something is
    worse than saying the tool does not know."""
    elf = build_elf(tmp_path / "future.elf", descriptor(subsystem=99))
    with pytest.raises(event_catalog.CatalogError, match="subsystem"):
        event_catalog.parse(*event_catalog.read_section(str(elf)))


def test_an_elf_without_the_section_is_refused(tmp_path):
    plain = build_elf(tmp_path / "plain.elf", descriptor(), name=".rodata")
    with pytest.raises(event_catalog.CatalogError, match="astra_events"):
        event_catalog.read_section(str(plain))


def test_a_non_elf_is_refused(tmp_path):
    path = tmp_path / "not.elf"
    path.write_bytes(b"nothing to see here")
    with pytest.raises(event_catalog.CatalogError, match="not an ELF"):
        event_catalog.read_section(str(path))


def test_the_command_line_prints_for_a_person(tmp_path):
    """It writes no file. The only reader of the catalog it used to render was
    trace_decode.py, which reads the section itself now."""
    elf = build_elf(tmp_path / "cli.elf", descriptor())
    result = subprocess.run(
        [sys.executable,
         str(Path(__file__).resolve().parents[1] / "event_catalog.py"),
         str(elf)],
        capture_output=True, text=True)

    assert result.returncode == 0, result.stderr
    assert "0xe0000000" in result.stdout
    assert "refused with status %u" in result.stdout
    assert "src/vfs_host.c:66" in result.stdout
    assert list(tmp_path.glob("*.json")) == []
