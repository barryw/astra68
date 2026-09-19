import struct

from tools.check_dynamic_relocations import violations


def elf_with_relocation(tmp_path, kind, target, symbol=0):
    phoff = 52
    phentsize = 32
    phnum = 2
    shoff = phoff + phentsize * phnum
    shentsize = 40
    shnum = 2
    rela_offset = shoff + shentsize * shnum
    image = bytearray(rela_offset + 12)
    image[:16] = b"\x7fELF\x01\x02\x01" + bytes(9)
    struct.pack_into(">HHIIIIIHHHHHH", image, 16,
                     2, 4, 1, 0, phoff, shoff, 0, 52,
                     phentsize, phnum, shentsize, shnum, 0)
    struct.pack_into(">IIIIIIII", image, phoff,
                     1, 0, 0x1000, 0, 0x100, 0x100, 5, 0x1000)
    struct.pack_into(">IIIIIIII", image, phoff + phentsize,
                     1, 0, 0x2000, 0, 0x100, 0x100, 6, 0x1000)
    struct.pack_into(">IIIIIIIIII", image, shoff + shentsize,
                     0, 4, 0, 0, rela_offset, 12, 0, 0, 4, 12)
    struct.pack_into(">IIi", image, rela_offset, target,
                     symbol << 8 | kind, 0)
    path = tmp_path / "dynamic.elf"
    path.write_bytes(image)
    return path


def test_supported_relocation_in_writable_segment_is_accepted(tmp_path):
    assert violations(elf_with_relocation(tmp_path, 20, 0x2004, 1)) == []


def test_relative_relocation_without_symbol_is_accepted(tmp_path):
    assert violations(elf_with_relocation(tmp_path, 22, 0x2004)) == []


def test_copy_relocation_is_rejected(tmp_path):
    problems = violations(elf_with_relocation(tmp_path, 19, 0x2004))
    assert problems == ["unsupported R_68K relocation 19 at 0x00002004"]


def test_supported_relocation_in_read_only_segment_is_rejected(tmp_path):
    problems = violations(elf_with_relocation(tmp_path, 20, 0x1004, 1))
    assert problems == [
        "R_68K_GLOB_DAT target 0x00001004 is not in a writable PT_LOAD"]


def test_relative_relocation_with_symbol_is_rejected(tmp_path):
    problems = violations(elf_with_relocation(tmp_path, 22, 0x2004, 1))
    assert problems == [
        "R_68K_RELATIVE at 0x00002004 has symbol 1, expected 0"]


def test_symbol_relocation_without_symbol_is_rejected(tmp_path):
    problems = violations(elf_with_relocation(tmp_path, 20, 0x2004))
    assert problems == ["R_68K_GLOB_DAT at 0x00002004 has no symbol"]
