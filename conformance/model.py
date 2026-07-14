"""Versioned, backend-neutral conformance case and result model.

The JSON fixture is the source of truth. Backends may translate it into a
Musashi request, an RTL simulator memory file, or an FPGA harness command, but
they must return the same normalized :class:`ExecutionResult`.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import json
from pathlib import Path
from typing import Mapping, Sequence


SCHEMA_VERSION = 1
CPU_REGISTERS = (
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7",
    "pc", "sr", "usp", "isp", "msp", "sfc", "dfc", "vbr", "cacr", "caar",
)
RUN_MODES = ("instruction", "cycles", "memory")
TERMINALS = ("instruction", "cycle-limit", "memory")


class CaseError(ValueError):
    """A conformance fixture is malformed or internally inconsistent."""


def integer(value: object, label: str) -> int:
    if isinstance(value, bool):
        raise CaseError(f"{label} must be an integer, not a boolean")
    try:
        parsed = int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError) as exc:
        raise CaseError(f"{label} is not an integer: {value!r}") from exc
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise CaseError(f"{label} is outside the unsigned 32-bit range")
    return parsed


def hex_bytes(value: object, label: str) -> bytes:
    if not isinstance(value, str):
        raise CaseError(f"{label} must be a hexadecimal string")
    encoded = value.strip().lower().replace("0x", "").replace("_", "")
    encoded = "".join(encoded.split())
    if len(encoded) & 1:
        raise CaseError(f"{label} contains an odd number of hexadecimal digits")
    try:
        return bytes.fromhex(encoded)
    except ValueError as exc:
        raise CaseError(f"{label} is not valid hexadecimal") from exc


@dataclass(frozen=True)
class MemorySegment:
    address: int
    data: bytes


@dataclass(frozen=True)
class MemoryRange:
    address: int
    length: int


@dataclass(frozen=True)
class MaskedValue:
    value: int
    mask: int = 0xFFFFFFFF


@dataclass(frozen=True)
class MemoryExpectation:
    address: int
    data: bytes
    mask: bytes


@dataclass(frozen=True)
class RunConfig:
    mode: str
    max_cycles: int
    stop_address: int = 0
    stop_mask: int = 0xFFFFFFFF
    stop_value: int = 0


@dataclass(frozen=True)
class ConformanceCase:
    case_id: str
    description: str
    authority: tuple[str, ...]
    requires: tuple[str, ...]
    reset: bool
    initial_cpu: Mapping[str, int]
    memory: tuple[MemorySegment, ...]
    run: RunConfig
    observe: tuple[MemoryRange, ...]
    expected_cpu: Mapping[str, MaskedValue]
    expected_memory: tuple[MemoryExpectation, ...]
    expected_terminal: str
    derived_from: tuple[str, ...] = ()
    source: Path | None = field(default=None, compare=False)

    def observation_ranges(self) -> tuple[MemoryRange, ...]:
        ranges = list(self.observe)
        for expected in self.expected_memory:
            candidate = MemoryRange(expected.address, len(expected.data))
            if candidate not in ranges:
                ranges.append(candidate)
        return tuple(ranges)


@dataclass(frozen=True)
class ExecutionResult:
    terminal: str
    cycles: int
    cpu: Mapping[str, int]
    memory: tuple[MemorySegment, ...]

    def memory_bytes(self, address: int, length: int) -> bytes | None:
        end = address + length
        for segment in self.memory:
            segment_end = segment.address + len(segment.data)
            if segment.address <= address and end <= segment_end:
                offset = address - segment.address
                return segment.data[offset:offset + length]
        return None


def _mapping(value: object, label: str) -> Mapping[str, object]:
    if not isinstance(value, Mapping):
        raise CaseError(f"{label} must be an object")
    return value


def _only(raw: Mapping[str, object], allowed: set[str], label: str) -> None:
    unknown = sorted(set(raw) - allowed)
    if unknown:
        raise CaseError(f"{label} contains unknown fields: {', '.join(unknown)}")


def _sequence(value: object, label: str) -> Sequence[object]:
    if isinstance(value, (str, bytes)) or not isinstance(value, Sequence):
        raise CaseError(f"{label} must be an array")
    return value


def _parse_cpu(value: object, label: str) -> dict[str, int]:
    raw = _mapping(value, label)
    unknown = sorted(set(raw) - set(CPU_REGISTERS))
    if unknown:
        raise CaseError(f"{label} contains unknown registers: {', '.join(unknown)}")
    return {name: integer(raw_value, f"{label}.{name}") for name, raw_value in raw.items()}


def _parse_segments(value: object, label: str) -> tuple[MemorySegment, ...]:
    segments = []
    for index, item in enumerate(_sequence(value, label)):
        raw = _mapping(item, f"{label}[{index}]")
        _only(raw, {"address", "data"}, f"{label}[{index}]")
        address = integer(raw.get("address"), f"{label}[{index}].address")
        data = hex_bytes(raw.get("data"), f"{label}[{index}].data")
        if address + len(data) > 0x1_0000_0000:
            raise CaseError(f"{label}[{index}] wraps the 32-bit address space")
        segments.append(MemorySegment(address, data))
    return tuple(segments)


def _parse_ranges(value: object, label: str) -> tuple[MemoryRange, ...]:
    ranges = []
    for index, item in enumerate(_sequence(value, label)):
        raw = _mapping(item, f"{label}[{index}]")
        _only(raw, {"address", "length"}, f"{label}[{index}]")
        address = integer(raw.get("address"), f"{label}[{index}].address")
        length = integer(raw.get("length"), f"{label}[{index}].length")
        if length == 0 or address + length > 0x1_0000_0000:
            raise CaseError(f"{label}[{index}] must be a nonempty 32-bit range")
        ranges.append(MemoryRange(address, length))
    return tuple(ranges)


def _parse_cpu_expectations(value: object, label: str) -> dict[str, MaskedValue]:
    raw = _mapping(value, label)
    unknown = sorted(set(raw) - set(CPU_REGISTERS))
    if unknown:
        raise CaseError(f"{label} contains unknown registers: {', '.join(unknown)}")
    result = {}
    for name, expectation in raw.items():
        if isinstance(expectation, Mapping):
            _only(expectation, {"value", "mask"}, f"{label}.{name}")
            expected = integer(expectation.get("value"), f"{label}.{name}.value")
            mask = integer(expectation.get("mask", 0xFFFFFFFF), f"{label}.{name}.mask")
        else:
            expected = integer(expectation, f"{label}.{name}")
            mask = 0xFFFFFFFF
        result[name] = MaskedValue(expected, mask)
    return result


def _parse_memory_expectations(value: object, label: str) -> tuple[MemoryExpectation, ...]:
    expectations = []
    for index, item in enumerate(_sequence(value, label)):
        raw = _mapping(item, f"{label}[{index}]")
        _only(raw, {"address", "data", "mask"}, f"{label}[{index}]")
        address = integer(raw.get("address"), f"{label}[{index}].address")
        data = hex_bytes(raw.get("data"), f"{label}[{index}].data")
        mask = hex_bytes(raw.get("mask", "ff" * len(data)), f"{label}[{index}].mask")
        if len(mask) != len(data):
            raise CaseError(f"{label}[{index}].mask length differs from data")
        if address + len(data) > 0x1_0000_0000:
            raise CaseError(f"{label}[{index}] wraps the 32-bit address space")
        expectations.append(MemoryExpectation(address, data, mask))
    return tuple(expectations)


def parse_case(raw_value: object, source: Path | None = None) -> ConformanceCase:
    raw = _mapping(raw_value, "case")
    _only(
        raw,
        {
            "schema", "id", "description", "authority", "derived_from",
            "requires", "initial", "run", "observe", "expect",
        },
        "case",
    )
    schema = integer(raw.get("schema"), "case.schema")
    if schema != SCHEMA_VERSION:
        raise CaseError(f"unsupported case schema {schema}; expected {SCHEMA_VERSION}")

    case_id = raw.get("id")
    if not isinstance(case_id, str) or not case_id.strip():
        raise CaseError("case.id must be a nonempty string")
    description = raw.get("description", "")
    if not isinstance(description, str):
        raise CaseError("case.description must be a string")

    authority_raw = raw.get("authority", [])
    authority = tuple(str(item) for item in _sequence(authority_raw, "case.authority"))
    requires_raw = raw.get("requires", [])
    requires = tuple(str(item) for item in _sequence(requires_raw, "case.requires"))
    derived_raw = raw.get("derived_from", [])
    derived_from = tuple(
        str(item) for item in _sequence(derived_raw, "case.derived_from")
    )

    initial = _mapping(raw.get("initial", {}), "case.initial")
    _only(initial, {"reset", "cpu", "memory"}, "case.initial")
    reset = initial.get("reset", True)
    if not isinstance(reset, bool):
        raise CaseError("case.initial.reset must be a boolean")
    initial_cpu = _parse_cpu(initial.get("cpu", {}), "case.initial.cpu")
    memory = _parse_segments(initial.get("memory", []), "case.initial.memory")

    run_raw = _mapping(raw.get("run"), "case.run")
    _only(run_raw, {"mode", "max_cycles", "stop"}, "case.run")
    mode = run_raw.get("mode")
    if mode not in RUN_MODES:
        raise CaseError(f"case.run.mode must be one of {', '.join(RUN_MODES)}")
    max_cycles = integer(run_raw.get("max_cycles", 100_000), "case.run.max_cycles")
    if max_cycles == 0 or max_cycles > 0x7FFFFFFF:
        raise CaseError("case.run.max_cycles must be between 1 and 2147483647")
    stop_raw = _mapping(run_raw.get("stop", {}), "case.run.stop")
    _only(stop_raw, {"address", "mask", "value"}, "case.run.stop")
    run = RunConfig(
        mode=mode,
        max_cycles=max_cycles,
        stop_address=integer(stop_raw.get("address", 0), "case.run.stop.address"),
        stop_mask=integer(stop_raw.get("mask", 0xFFFFFFFF), "case.run.stop.mask"),
        stop_value=integer(stop_raw.get("value", 0), "case.run.stop.value"),
    )
    if mode == "memory" and "address" not in stop_raw:
        raise CaseError("case.run.stop.address is required for memory mode")

    observe = _parse_ranges(raw.get("observe", []), "case.observe")
    expect = _mapping(raw.get("expect", {}), "case.expect")
    _only(expect, {"terminal", "cpu", "memory"}, "case.expect")
    expected_cpu = _parse_cpu_expectations(expect.get("cpu", {}), "case.expect.cpu")
    expected_memory = _parse_memory_expectations(
        expect.get("memory", []), "case.expect.memory"
    )
    expected_terminal = expect.get("terminal", mode if mode != "cycles" else "cycle-limit")
    if expected_terminal not in TERMINALS:
        raise CaseError(f"case.expect.terminal must be one of {', '.join(TERMINALS)}")

    return ConformanceCase(
        case_id=case_id,
        description=description,
        authority=authority,
        requires=requires,
        reset=reset,
        initial_cpu=initial_cpu,
        memory=memory,
        run=run,
        observe=observe,
        expected_cpu=expected_cpu,
        expected_memory=expected_memory,
        expected_terminal=expected_terminal,
        derived_from=derived_from,
        source=source,
    )


def load_case(path: str | Path) -> ConformanceCase:
    source = Path(path).resolve()
    try:
        raw = json.loads(source.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise CaseError(f"cannot load {source}: {exc}") from exc
    return parse_case(raw, source)


def compare_result(case: ConformanceCase, result: ExecutionResult) -> list[str]:
    mismatches = []
    if result.terminal != case.expected_terminal:
        mismatches.append(
            f"terminal={result.terminal}, expected {case.expected_terminal}"
        )

    for name, expected in case.expected_cpu.items():
        if name not in result.cpu:
            mismatches.append(f"{name}=missing")
            continue
        actual = result.cpu[name] & 0xFFFFFFFF
        if actual & expected.mask != expected.value & expected.mask:
            mismatches.append(
                f"{name}=0x{actual:08x}, expected 0x{expected.value:08x} "
                f"mask 0x{expected.mask:08x}"
            )

    for expected in case.expected_memory:
        actual = result.memory_bytes(expected.address, len(expected.data))
        if actual is None:
            mismatches.append(
                f"memory[0x{expected.address:08x}..+{len(expected.data)}]=missing"
            )
            continue
        differing = [
            index
            for index, (got, want, mask) in enumerate(zip(actual, expected.data, expected.mask))
            if got & mask != want & mask
        ]
        if differing:
            first = differing[0]
            mismatches.append(
                f"memory[0x{expected.address + first:08x}]=0x{actual[first]:02x}, "
                f"expected 0x{expected.data[first]:02x} mask 0x{expected.mask[first]:02x}"
            )
    return mismatches
