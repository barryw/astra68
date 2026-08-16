import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import library_info  # noqa: E402
from test_event_catalog import build_elf  # noqa: E402


def record(name="graphics.library", major=1, minor=0, patch=0,
           abi_major=1, abi_minor=0, magic=library_info.RECORD_MAGIC,
           record_version=library_info.RECORD_VERSION,
           size=library_info.RECORD_SIZE,
           target=library_info.TARGET_M68030):
    blob = struct.pack(library_info._HEAD, magic, record_version, size,
                       major, minor, patch, abi_major, abi_minor, 0, target,
                       0x10203040, 0x00F00000)
    blob += name.encode().ljust(library_info.NAME_MAX, b"\0")
    blob += b"Barry Walker\0".ljust(library_info.AUTHOR_MAX, b"\0")
    blob += b"Copyright 2026 Barry Walker\0".ljust(
        library_info.COPYRIGHT_MAX, b"\0")
    assert len(blob) == library_info.RECORD_SIZE
    return blob


def elf(tmp_path, blob):
    return build_elf(tmp_path / "library.elf", blob,
                     name=library_info.SECTION_NAME)


def test_library_identity_round_trips(tmp_path):
    result = library_info.read(str(elf(tmp_path, record())))
    assert result["name"] == "graphics.library"
    assert result["version"] == "1.0.0"
    assert (result["abi_major"], result["abi_minor"]) == (1, 0)
    assert result["build_id"] == 0x10203040
    assert result["exports_offset"] == 0x00F00000


@pytest.mark.parametrize("change,match", [
    ({"magic": 0xDEADBEEF}, "magic"),
    ({"record_version": 2}, "version 2"),
    ({"size": 64}, "size 64"),
    ({"target": 0}, "target"),
])
def test_invalid_identity_is_refused(tmp_path, change, match):
    with pytest.raises(library_info.LibraryError, match=match):
        library_info.read(str(elf(tmp_path, record(**change))))


def test_two_records_are_refused(tmp_path):
    with pytest.raises(library_info.LibraryError, match="not one"):
        library_info.read(str(elf(tmp_path, record() + record())))
