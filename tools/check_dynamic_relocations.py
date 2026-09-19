#!/usr/bin/env python3
"""Validate the relocation subset shared by every Astra dynamic image."""

from __future__ import annotations

import struct
from pathlib import Path


SUPPORTED_RELOCATIONS = {
    0: "R_68K_NONE",
    1: "R_68K_32",
    4: "R_68K_PC32",
    20: "R_68K_GLOB_DAT",
    21: "R_68K_JMP_SLOT",
    22: "R_68K_RELATIVE",
    40: "R_68K_TLS_DTPMOD32",
    41: "R_68K_TLS_DTPREL32",
    42: "R_68K_TLS_TPREL32",
}
PT_LOAD = 1
PF_W = 2
SHT_RELA = 4


class DynamicRelocationError(Exception):
    pass


def _range(data: bytes, offset: int, size: int, what: str) -> memoryview:
    if offset < 0 or size < 0 or offset > len(data) or size > len(data) - offset:
        raise DynamicRelocationError(f"truncated {what}")
    return memoryview(data)[offset:offset + size]


def violations(path: Path | str) -> list[str]:
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:6] != b"\x7fELF\x01\x02":
        raise DynamicRelocationError("not a big-endian ELF32 image")
    phoff, shoff = struct.unpack_from(">II", data, 0x1c)
    phentsize, phnum, shentsize, shnum = struct.unpack_from(">HHHH", data, 0x2a)
    if phentsize < 32 or shentsize < 40:
        raise DynamicRelocationError("invalid ELF table entry size")

    writable: list[tuple[int, int]] = []
    for index in range(phnum):
        entry = _range(data, phoff + index * phentsize, phentsize,
                       "program-header table")
        p_type, _, vaddr, _, _, memsz, flags = struct.unpack_from(
            ">IIIIIII", entry, 0)
        if p_type == PT_LOAD and flags & PF_W:
            end = vaddr + memsz
            if end > 0x100000000:
                raise DynamicRelocationError("overflowing writable segment")
            writable.append((vaddr, end))

    problems: list[str] = []
    for section_index in range(shnum):
        entry = _range(data, shoff + section_index * shentsize, shentsize,
                       "section-header table")
        _, section_type, _, _, offset, size, _, _, _, entry_size = \
            struct.unpack_from(">IIIIIIIIII", entry, 0)
        if section_type != SHT_RELA:
            continue
        if entry_size != 12 or size % entry_size != 0:
            problems.append(f"relocation section {section_index} has invalid entries")
            continue
        relocations = _range(data, offset, size, "relocation section")
        for at in range(0, size, entry_size):
            target, info, _ = struct.unpack_from(">IIi", relocations, at)
            kind = info & 0xff
            symbol = info >> 8
            name = SUPPORTED_RELOCATIONS.get(kind)
            if name is None:
                problems.append(
                    f"unsupported R_68K relocation {kind} at 0x{target:08x}")
                continue
            if kind == 0:
                continue
            if kind == 22 and symbol != 0:
                problems.append(
                    f"{name} at 0x{target:08x} has symbol {symbol}, expected 0")
                continue
            if kind not in (22, 40, 41, 42) and symbol == 0:
                problems.append(
                    f"{name} at 0x{target:08x} has no symbol")
                continue
            target_end = target + 4
            if target_end > 0x100000000 or not any(
                    start <= target and target_end <= end
                    for start, end in writable):
                problems.append(
                    f"{name} target 0x{target:08x} is not in a writable PT_LOAD")
    return problems
