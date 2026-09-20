import struct
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from check_shared_library_layout import check
from event_catalog import CatalogError


def shared_elf(tmp_path, sections):
    names = b"\0.shstrtab\0" + b"".join(
        name.encode("ascii") + b"\0" for name in sections)
    section_names = {name: names.index(name.encode("ascii"))
                     for name in sections}
    shoff = 52
    shentsize = 40
    shnum = 2 + len(sections)
    names_offset = shoff + shentsize * shnum
    data_offset = names_offset + len(names)
    image = bytearray(data_offset + len(sections))
    image[:16] = b"\x7fELF\x01\x02\x01" + bytes(9)
    struct.pack_into(">HHIIIIIHHHHHH", image, 16,
                     3, 4, 1, 0, 0, shoff, 0, 52,
                     32, 0, shentsize, shnum, 1)
    struct.pack_into(">IIIIIIIIII", image, shoff + shentsize,
                     1, 3, 0, 0, names_offset, len(names), 0, 0, 1, 0)
    image[names_offset:data_offset] = names
    for index, name in enumerate(sections, 2):
        offset = data_offset + index - 2
        struct.pack_into(">IIIIIIIIII", image, shoff + index * shentsize,
                         section_names[name], 1, 2, 0, offset, 1,
                         0, 0, 1, 0)
        image[offset] = index
    path = tmp_path / "shared.elf"
    path.write_bytes(image)
    return path


def test_registered_unwind_layout_is_accepted(tmp_path):
    check(shared_elf(tmp_path,
                     (".eh_frame", ".init_array", ".fini_array")))


def test_self_relocating_interpreter_needs_no_frame_registration(tmp_path):
    check(shared_elf(tmp_path, ()), require_frame_registration=False)


@pytest.mark.parametrize("missing",
                         (".eh_frame", ".init_array", ".fini_array"))
def test_missing_unwind_registration_is_rejected(tmp_path, missing):
    sections = tuple(section for section in
                     (".eh_frame", ".init_array", ".fini_array")
                     if section != missing)
    with pytest.raises(CatalogError, match=missing):
        check(shared_elf(tmp_path, sections))
