"""The provenance reader, against ELFs built here rather than by a compiler.

Same reasoning as the catalog extractor's tests next door: a fixture built by
hand pins the *contract* -- the record's byte layout and the refusals -- while
one produced by the cross-compiler would only test whatever the compiler
happened to emit, which is the thing the contract exists to constrain.

The ELF builder is that file's, because it already takes the section name.
"""

import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import program_info  # noqa: E402
from test_event_catalog import build_elf  # noqa: E402


def record(name="events", major=1, minor=0, patch=0, build=0xC0FFEE01,
           author="Barry Walker", copyright="Copyright 2026 Barry Walker",
           magic=program_info.RECORD_MAGIC,
           version=program_info.RECORD_VERSION):
    blob = struct.pack(program_info._HEAD, magic, version, major, minor, patch,
                       build)
    blob += name.encode("ascii").ljust(program_info.NAME_MAX, b"\0")
    blob += author.encode("ascii").ljust(program_info.AUTHOR_MAX, b"\0")
    blob += copyright.encode("ascii").ljust(program_info.COPYRIGHT_MAX, b"\0")
    assert len(blob) == program_info.RECORD_SIZE
    return blob


def elf(tmp_path, blob, filename="one.elf"):
    return build_elf(tmp_path / filename, blob, base=0x00101000,
                     name=program_info.SECTION_NAME)


def test_a_record_round_trips(tmp_path):
    program = program_info.read(str(elf(tmp_path, record())))

    assert program["name"] == "events"
    assert program["version"] == "1.0.0"
    assert program["build_id"] == 0xC0FFEE01
    assert program["author"] == "Barry Walker"
    assert program["copyright"] == "Copyright 2026 Barry Walker"


def test_a_version_is_three_numbers_so_it_can_be_compared(tmp_path):
    """The reason the record does not carry a string: nobody can order "1.10"
    and "1.9" as text without first agreeing how."""
    program = program_info.read(
        str(elf(tmp_path, record(major=1, minor=10, patch=3))))

    assert (program["major"], program["minor"], program["patch"]) == (1, 10, 3)
    assert program["version"] == "1.10.3"
    assert (1, 10, 3) > (1, 9, 0)


def test_a_field_used_to_its_last_byte_is_still_read(tmp_path):
    """The macro's static assertions keep a string inside its field, so a field
    with no NUL is what a full-length one looks like, not a corrupt record."""
    full = "C" * program_info.COPYRIGHT_MAX
    program = program_info.read(str(elf(tmp_path, record(copyright=full))))

    assert program["copyright"] == full


def test_a_wrong_magic_is_reported_not_guessed_at(tmp_path):
    """The section is not what this tool thinks it is, and reading on would
    attribute a program to whoever is at the right offset."""
    with pytest.raises(program_info.ProgramError, match="magic"):
        program_info.read(str(elf(tmp_path, record(magic=0xDEADBEEF))))


def test_a_record_from_a_later_design_is_reported(tmp_path):
    with pytest.raises(program_info.ProgramError, match="version 2"):
        program_info.read(str(elf(tmp_path, record(version=2))))


def test_two_records_are_refused(tmp_path):
    """The linker asserts there is exactly one, so a section carrying two means
    an image this tool should not be answering about at all."""
    with pytest.raises(program_info.ProgramError, match="not one"):
        program_info.read(str(elf(tmp_path, record() + record())))


def test_a_truncated_record_is_refused(tmp_path):
    with pytest.raises(program_info.ProgramError, match="not one"):
        program_info.read(str(elf(tmp_path, record()[:-8])))


def test_an_image_with_no_record_says_so(tmp_path):
    """Which cannot happen through the linker, but can through a stray objcopy
    -- and it is the one failure a person is most likely to cause by hand."""
    other = build_elf(tmp_path / "bare.elf", b"\0" * 16, name=".rodata")
    with pytest.raises(program_info.CatalogError, match=".astra_program"):
        program_info.read(str(other))


def test_the_command_prints_what_the_record_says(tmp_path, capsys):
    assert program_info.main([str(elf(tmp_path, record()))]) == 0
    out = capsys.readouterr().out

    assert out.splitlines()[0] == "events 1.0.0"
    assert "0xc0ffee01" in out
    assert "Barry Walker" in out
