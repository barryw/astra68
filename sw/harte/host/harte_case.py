#!/usr/bin/env python3
"""Tom Harte 68000 JSON parsing and Astra register-only case filtering."""

from __future__ import annotations

from dataclasses import dataclass
import gzip
import json
from pathlib import Path
from typing import Iterator, Mapping, Sequence

try:
    from .m68000_bin import load_binary
except ImportError:  # Direct script execution from sw/harte/host.
    from m68000_bin import load_binary


CCR_MASK = 0x1F
MAX_INSTRUCTION_BYTES = 30


class CaseError(ValueError):
    """The vector cannot be represented by the current Astra harness."""


@dataclass(frozen=True)
class Case:
    name: str
    opcode: int
    d: tuple[int, ...]
    a: tuple[int, ...]
    ccr: int
    instr: bytes
    fd: tuple[int, ...]
    fa: tuple[int, ...]
    fccr: int
    initial_pc: int
    final_pc: int
    cycle_length: int

    @property
    def ilen(self) -> int:
        return len(self.instr)


def load(path: str | Path) -> Iterator[dict]:
    """Load a maintained m68000 binary or old-style JSON vector array."""
    vector_path = Path(path)
    if vector_path.name.endswith(".json.bin"):
        yield from load_binary(vector_path)
        return
    opener = gzip.open if vector_path.suffix == ".gz" else open
    with opener(vector_path, "rt", encoding="utf-8") as stream:
        vectors = json.load(stream)
    if not isinstance(vectors, list):
        raise CaseError(f"{vector_path}: expected a top-level JSON array")
    yield from vectors


def _registers(state: Mapping[str, object], prefix: str, count: int) -> tuple[int, ...]:
    try:
        return tuple(int(state[f"{prefix}{index}"]) & 0xFFFFFFFF for index in range(count))
    except (KeyError, TypeError, ValueError) as exc:
        raise CaseError(f"missing or invalid {prefix.upper()} register state") from exc


def _pc_delta(raw: Mapping[str, object]) -> int:
    try:
        initial_pc = int(raw["initial"]["pc"]) & 0xFFFFFFFF
        final_pc = int(raw["final"]["pc"]) & 0xFFFFFFFF
    except (KeyError, TypeError, ValueError) as exc:
        raise CaseError("missing or invalid PC state") from exc
    return (final_pc - initial_pc) & 0xFFFFFFFF


def _instruction_bytes(raw: Mapping[str, object], ilen: int) -> bytes:
    initial = raw["initial"]
    try:
        pc = int(initial["pc"]) & 0xFFFFFFFF
        prefetch = [int(word) & 0xFFFF for word in initial["prefetch"]]
        ram = {int(address) & 0xFFFFFFFF: int(value) & 0xFF
               for address, value in initial.get("ram", [])}
    except (KeyError, TypeError, ValueError) as exc:
        raise CaseError("invalid prefetch or RAM state") from exc

    encoded = bytearray()
    for word in prefetch[:2]:
        encoded.extend(word.to_bytes(2, "big"))

    while len(encoded) < ilen:
        address = (pc + len(encoded)) & 0xFFFFFFFF
        if address not in ram:
            raise CaseError(f"instruction byte at 0x{address:08x} is absent from initial.ram")
        encoded.append(ram[address])
    return bytes(encoded[:ilen])


def _ea_register_only(opcode: int) -> tuple[bool, bool]:
    """Return (register-only, uses-a7) for the six-bit effective address."""
    mode = (opcode >> 3) & 7
    reg = opcode & 7
    if mode == 0:
        return True, False
    if mode == 1:
        return True, reg == 7
    return False, False


def opcode_scope(opcode: int) -> tuple[bool, str]:
    """Classify the conservative register-only opcode subset supported by the harness."""
    opcode &= 0xFFFF

    if opcode == 0x4E71:
        return True, "NOP"
    if opcode & 0xF100 == 0x7000:
        return True, "MOVEQ"
    if opcode & 0xFFF8 in (0x4840, 0x4880, 0x48C0):
        return True, "SWAP/EXT"

    # Dynamic and immediate bit operations with a Dn destination. MOVEP shares
    # part of the dynamic encoding but uses address-register mode and is rejected.
    if opcode & 0xF100 == 0x0100 or opcode & 0xFF00 == 0x0800:
        return (True, "BIT") if ((opcode >> 3) & 7) == 0 else (False, "memory bit operation")

    # Register/immediate shifts and rotates. Memory forms have bits 7:6 == 11.
    if opcode & 0xF000 == 0xE000:
        if opcode & 0x00C0 != 0x00C0:
            return True, "SHIFT/ROTATE"
        return False, "memory shift/rotate"

    # MOVE/MOVEA with both effective addresses constrained to Dn/An.
    if opcode & 0xC000 == 0 and opcode & 0x3000:
        src_mode = (opcode >> 3) & 7
        src_reg = opcode & 7
        dst_mode = (opcode >> 6) & 7
        dst_reg = (opcode >> 9) & 7
        if src_mode not in (0, 1) or dst_mode not in (0, 1):
            return False, "memory MOVE"
        if (src_mode == 1 and src_reg == 7) or (dst_mode == 1 and dst_reg == 7):
            return False, "A7 operand"
        return True, "MOVE/MOVEA"

    # Immediate arithmetic/logical operations to Dn only.
    if opcode & 0xFF00 in (0x0000, 0x0200, 0x0400, 0x0600, 0x0A00, 0x0C00):
        register_only, _ = _ea_register_only(opcode)
        if register_only and ((opcode >> 3) & 7) == 0:
            return True, "IMMEDIATE"
        return False, "memory or special immediate operation"

    # ADDQ/SUBQ and Scc. DBcc and memory destinations are excluded.
    if opcode & 0xF000 == 0x5000:
        mode = (opcode >> 3) & 7
        reg = opcode & 7
        if opcode & 0x00C0 == 0x00C0:
            return (True, "Scc") if mode == 0 else (False, "DBcc or memory Scc")
        if mode in (0, 1):
            return (False, "A7 operand") if mode == 1 and reg == 7 else (True, "ADDQ/SUBQ")
        return False, "memory ADDQ/SUBQ"

    # Unary data operations on Dn. Size 11 selects MOVE SR/CCR or TAS rather
    # than the byte/word/long unary operation represented by the high byte.
    if opcode & 0xFF00 in (0x4000, 0x4200, 0x4400, 0x4600, 0x4A00):
        if opcode & 0x00C0 == 0x00C0:
            return False, "system-register or TAS operation"
        return (True, "UNARY") if ((opcode >> 3) & 7) == 0 else (False, "memory unary operation")

    # Register forms in the OR/SUB/CMP/AND/ADD groups. BCD is excluded because
    # N/V are undefined on 68000; DIV overflow/exception cases need finer masks.
    line = opcode >> 12
    if line in (0x8, 0x9, 0xB, 0xC, 0xD):
        opmode = (opcode >> 6) & 7
        mode = (opcode >> 3) & 7
        reg = opcode & 7
        destination = (opcode >> 9) & 7
        if opcode & 0xF1F8 in (0x8100, 0xC100):
            return False, "BCD flags are architecture-specific"

        exg_form = opcode & 0xF1F8
        if exg_form in (0xC140, 0xC148, 0xC188):
            uses_a7 = (
                (exg_form == 0xC148 and destination == 7)
                or (exg_form in (0xC148, 0xC188) and reg == 7)
            )
            return (False, "A7 operand") if uses_a7 else (True, "EXG")

        if line in (0x9, 0xB, 0xD) and opmode in (3, 7):
            if mode not in (0, 1):
                return False, "memory address-register ALU form"
            if destination == 7 or (mode == 1 and reg == 7):
                return False, "A7 operand"
            return True, "ADDA/SUBA/CMPA"

        if line == 0xC and opmode in (3, 7):
            return (True, "MUL") if mode == 0 else (False, "memory multiply")
        if line == 0x8 and opmode in (3, 7):
            return False, "DIV flags or exception not comparable"

        if mode == 0 and opmode in (0, 1, 2, 4, 5, 6):
            return True, "REGISTER ALU"
        return False, "memory or deferred ALU form"

    return False, "unsupported opcode"


def scope_reason(raw: Mapping[str, object]) -> str | None:
    try:
        initial = raw["initial"]
        final = raw["final"]
        sr = int(initial["sr"])
        prefetch: Sequence[object] = initial["prefetch"]
        opcode = int(prefetch[0]) & 0xFFFF
    except (KeyError, IndexError, TypeError, ValueError) as exc:
        return f"malformed vector: {exc}"

    supported, reason = opcode_scope(opcode)
    if not supported:
        return reason

    try:
        ilen = _pc_delta(raw)
    except CaseError as exc:
        return str(exc)
    if ilen < 2 or ilen > MAX_INSTRUCTION_BYTES or ilen & 1:
        return f"non-sequential PC delta {ilen}"

    # Register-only instructions cannot alter test RAM. This catches accidental
    # decoder over-inclusion without relying on cycle/prefetch transactions.
    if initial.get("ram", []) != final.get("ram", []):
        return "RAM changed"

    return None


def is_in_scope(raw: Mapping[str, object]) -> bool:
    return scope_reason(raw) is None


def build_case(raw: Mapping[str, object]) -> Case:
    reason = scope_reason(raw)
    if reason is not None:
        raise CaseError(reason)

    initial = raw["initial"]
    final = raw["final"]
    ilen = _pc_delta(raw)
    instr = _instruction_bytes(raw, ilen)
    opcode = int.from_bytes(instr[:2], "big")

    return Case(
        name=str(raw.get("name", f"{opcode:04x}")),
        opcode=opcode,
        d=_registers(initial, "d", 8),
        a=_registers(initial, "a", 7),
        ccr=int(initial["sr"]) & CCR_MASK,
        instr=instr,
        fd=_registers(final, "d", 8),
        fa=_registers(final, "a", 7),
        fccr=int(final["sr"]) & CCR_MASK,
        initial_pc=int(initial["pc"]) & 0xFFFFFFFF,
        final_pc=int(final["pc"]) & 0xFFFFFFFF,
        cycle_length=int(raw.get("length", 0)),
    )
