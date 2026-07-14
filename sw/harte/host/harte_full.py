"""Full architectural-state adapter for the pinned Motorola 68000 corpus.

The Harte corpus models an MC68000, including its exception frames.  This
adapter therefore runs Musashi in 68000 mode.  The existing register-only
adapter remains the portable 68030/RTL subset.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping, Sequence

from conformance.model import (
    ConformanceCase,
    ExecutionResult,
    MemoryRange,
    MemorySegment,
    RunConfig,
)
from conformance.target import TargetError
from conformance.targets import MusashiTarget


SR_ARCHITECTURAL_MASK = 0xA71F
CCR_X = 0x10
CCR_N = 0x08
CCR_Z = 0x04
CCR_V = 0x02
CCR_C = 0x01


class FullCaseError(ValueError):
    """A corpus vector cannot be represented by the full-state adapter."""


@dataclass(frozen=True)
class HarteState:
    d: tuple[int, ...]
    a: tuple[int, ...]
    usp: int
    ssp: int
    sr: int
    pc: int
    prefetch: tuple[int, int]
    ram: tuple[tuple[int, int], ...]


@dataclass(frozen=True)
class FullCase:
    name: str
    opcode: int
    initial: HarteState
    final: HarteState
    cycle_length: int
    transaction_kinds: tuple[str, ...]

    @property
    def classification(self) -> str:
        if self.opcode & 0xFFC0 == 0x4AC0:
            return "upstream-caveat-tas"
        if self.opcode == 0x4E76:
            return "upstream-caveat-trapv"
        if any(kind in ("re", "we") for kind in self.transaction_kinds):
            return "address-error"
        return "ordinary"


def _integer(state: Mapping[str, object], name: str, bits: int = 32) -> int:
    try:
        value = int(state[name])
    except (KeyError, TypeError, ValueError) as exc:
        raise FullCaseError(f"missing or invalid {name} state") from exc
    maximum = (1 << bits) - 1
    if not 0 <= value <= maximum:
        raise FullCaseError(f"{name} is outside the unsigned {bits}-bit range")
    return value


def _registers(state: Mapping[str, object], prefix: str,
               count: int) -> tuple[int, ...]:
    return tuple(_integer(state, f"{prefix}{index}") for index in range(count))


def _ram(state: Mapping[str, object]) -> tuple[tuple[int, int], ...]:
    try:
        entries: Sequence[object] = state.get("ram", [])  # type: ignore[assignment]
    except AttributeError as exc:
        raise FullCaseError("invalid RAM state") from exc
    memory: dict[int, int] = {}
    try:
        for entry in entries:
            address, value = entry  # type: ignore[misc]
            address = int(address)
            value = int(value)
            if not 0 <= address <= 0xFFFFFFFF or not 0 <= value <= 0xFF:
                raise FullCaseError("RAM address or byte is out of range")
            if address in memory and memory[address] != value:
                raise FullCaseError(f"conflicting RAM byte at 0x{address:08x}")
            memory[address] = value
    except (TypeError, ValueError) as exc:
        raise FullCaseError("invalid RAM state") from exc
    return tuple(sorted(memory.items()))


def _state(raw: Mapping[str, object]) -> HarteState:
    try:
        prefetch_raw = raw["prefetch"]
        if not isinstance(prefetch_raw, Sequence) or len(prefetch_raw) != 2:
            raise FullCaseError("prefetch must contain exactly two words")
        prefetch = tuple(int(value) for value in prefetch_raw)
        if any(not 0 <= value <= 0xFFFF for value in prefetch):
            raise FullCaseError("prefetch word is out of range")
    except (KeyError, TypeError, ValueError) as exc:
        raise FullCaseError("missing or invalid prefetch state") from exc
    return HarteState(
        d=_registers(raw, "d", 8),
        a=_registers(raw, "a", 7),
        usp=_integer(raw, "usp"),
        ssp=_integer(raw, "ssp"),
        sr=_integer(raw, "sr", 16),
        pc=_integer(raw, "pc"),
        prefetch=(prefetch[0], prefetch[1]),
        ram=_ram(raw),
    )


def build_full_case(raw: Mapping[str, object]) -> FullCase:
    try:
        initial_raw = raw["initial"]
        final_raw = raw["final"]
        if not isinstance(initial_raw, Mapping) or not isinstance(final_raw, Mapping):
            raise FullCaseError("initial and final states must be objects")
        initial = _state(initial_raw)
        final = _state(final_raw)
        cycle_length = int(raw.get("length", 0))
        transactions_raw = raw.get("transactions", [])
        if not isinstance(transactions_raw, Sequence):
            raise FullCaseError("transactions must be an array")
        transaction_kinds = tuple(str(transaction[0]) for transaction in transactions_raw)
    except (KeyError, TypeError, ValueError) as exc:
        raise FullCaseError(f"malformed full-state vector: {exc}") from exc
    if cycle_length < 0:
        raise FullCaseError("cycle length is negative")
    return FullCase(
        name=str(raw.get("name", f"{initial.prefetch[0]:04x}")),
        opcode=initial.prefetch[0],
        initial=initial,
        final=final,
        cycle_length=cycle_length,
        transaction_kinds=transaction_kinds,
    )


def _segments(entries: tuple[tuple[int, int], ...]) -> tuple[MemorySegment, ...]:
    if not entries:
        return ()
    segments: list[MemorySegment] = []
    start = entries[0][0]
    previous = start
    content = bytearray((entries[0][1],))
    for address, value in entries[1:]:
        if address == previous + 1:
            content.append(value)
        else:
            segments.append(MemorySegment(start, bytes(content)))
            start = address
            content = bytearray((value,))
        previous = address
    segments.append(MemorySegment(start, bytes(content)))
    return tuple(segments)


def _initial_cpu(state: HarteState) -> dict[str, int]:
    cpu = {f"d{index}": value for index, value in enumerate(state.d)}
    cpu.update({f"a{index}": value for index, value in enumerate(state.a)})
    cpu.update({
        "pc": state.pc,
        "sr": state.sr,
        "usp": state.usp,
        "isp": state.ssp,
    })
    return cpu


def _conformance_case(case: FullCase) -> ConformanceCase:
    observations = tuple(
        MemoryRange(segment.address, len(segment.data))
        for segment in _segments(case.final.ram)
    )
    return ConformanceCase(
        case_id=f"harte-full/{case.name}",
        description="Pinned Harte MC68000 full architectural state",
        authority=("Pinned SingleStepTests/m68000 vector",),
        requires=("mc68000",),
        reset=True,
        initial_cpu=_initial_cpu(case.initial),
        memory=_segments(case.initial.ram),
        run=RunConfig(mode="instruction", max_cycles=max(case.cycle_length, 1)),
        observe=observations,
        expected_cpu={},
        expected_memory=(),
        expected_terminal="instruction",
    )


def sr_mask(case: FullCase) -> int:
    """Return only architecturally defined MC68000 SR bits for this opcode."""
    opcode = case.opcode
    # ABCD, SBCD, and NBCD leave N and V undefined.
    if opcode & 0xF1F0 in (0x8100, 0xC100) or opcode & 0xFFC0 == 0x4800:
        return (SR_ARCHITECTURAL_MASK & ~0x1F) | CCR_X | CCR_Z | CCR_C
    # CHK defines N for the trapping comparisons and preserves X; Z/V/C are
    # undefined and must not turn an implementation difference into a failure.
    if opcode & 0xF1C0 == 0x4180:
        return (SR_ARCHITECTURAL_MASK & ~0x1F) | CCR_X | CCR_N
    # On divide overflow N and Z are undefined; X is unaffected and V/C are
    # defined.  A successful divide defines all five CCR bits.
    if opcode >> 12 == 8 and ((opcode >> 6) & 7) in (3, 7):
        if case.final.sr & CCR_V:
            return (SR_ARCHITECTURAL_MASK & ~0x1F) | CCR_X | CCR_V | CCR_C
    return SR_ARCHITECTURAL_MASK


def expected_pc(case: FullCase) -> int:
    """Convert the corpus's prefetch-relative PC to architectural PC."""
    # The binary corpus records MAME's next-prefetch address minus four.  A
    # successful STOP performs no next prefetch, so its normalized final PC is
    # left at the opcode even though the architectural PC has consumed both
    # words.  Privilege-violation STOP cases have normal exception fetches and
    # do not need this correction.
    if (
        case.opcode == 0x4E72
        and case.transaction_kinds == ("n",)
        and case.final.pc == case.initial.pc
    ):
        return (case.final.pc + 4) & 0xFFFFFFFF
    return case.final.pc


def compare_full_result(case: FullCase, result: ExecutionResult) -> list[str]:
    mismatches: list[str] = []
    expected_registers = {
        **{f"d{index}": value for index, value in enumerate(case.final.d)},
        **{f"a{index}": value for index, value in enumerate(case.final.a)},
        "pc": expected_pc(case),
        "usp": case.final.usp,
        "isp": case.final.ssp,
    }
    active_sp = case.final.ssp if case.final.sr & 0x2000 else case.final.usp
    expected_registers["a7"] = active_sp
    for name, expected in expected_registers.items():
        actual = result.cpu[name]
        if actual != expected:
            mismatches.append(f"{name.upper()}={actual:08x}, expected {expected:08x}")

    mask = sr_mask(case)
    actual_sr = result.cpu["sr"] & mask
    expected_sr = case.final.sr & mask
    if actual_sr != expected_sr:
        mismatches.append(
            f"SR={actual_sr:04x}, expected {expected_sr:04x}, mask {mask:04x}"
        )

    actual_memory: dict[int, int] = {}
    for segment in result.memory:
        for offset, value in enumerate(segment.data):
            actual_memory[segment.address + offset] = value
    memory_mismatches = []
    for address, expected in case.final.ram:
        actual = actual_memory.get(address)
        if actual != expected:
            rendered = "missing" if actual is None else f"{actual:02x}"
            memory_mismatches.append(
                f"[{address:08x}]={rendered}, expected {expected:02x}"
            )
    if memory_mismatches:
        mismatches.extend(memory_mismatches[:8])
        if len(memory_mismatches) > 8:
            mismatches.append(f"memory: {len(memory_mismatches) - 8} more mismatches")
    return mismatches


class MusashiFullHarteTarget:
    """Execute complete MC68000 architectural vectors on persistent Musashi."""

    def __init__(self, worker: str | Path | None = None):
        self.target = (
            MusashiTarget(worker, cpu_model="68000")
            if worker is not None else MusashiTarget(cpu_model="68000")
        )

    def __enter__(self) -> "MusashiFullHarteTarget":
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.close()

    def close(self) -> None:
        self.target.close()

    def manifest(self) -> dict:
        manifest = dict(self.target.manifest())
        manifest.update({
            "suite_adapter": "harte-full-architectural-v1",
            "oracle_cpu": "Motorola MC68000",
            "compared": ["registers", "architectural-sr", "memory"],
            "not_compared": ["prefetch-queue", "bus-transactions", "cycle-count"],
        })
        return manifest

    def execute(self, case: FullCase) -> tuple[ExecutionResult | None, str | None]:
        try:
            return self.target.execute(_conformance_case(case)), None
        except TargetError as exc:
            return None, str(exc)
