"""Canonical construction of every backend-neutral conformance target."""

from __future__ import annotations

from pathlib import Path

from ..target import ConformanceTarget, TargetError
from .musashi import MusashiTarget
from .rtl import RtlTarget


PRODUCTION_TARGETS = (
    "musashi-68030",
    "rtl-tg68k030-mmu2",
)

DIAGNOSTIC_TARGETS = (
    "musashi-68000",
)

TARGET_ALIASES = {
    "musashi": "musashi-68030",
    "rtl": "rtl-tg68k030-mmu2",
}


def target_choices() -> tuple[str, ...]:
    """Return stable target IDs plus compatibility aliases for CLI parsers."""
    return (*PRODUCTION_TARGETS, *DIAGNOSTIC_TARGETS, *TARGET_ALIASES)


def canonical_target_id(target_id: str) -> str:
    canonical = TARGET_ALIASES.get(target_id, target_id)
    if canonical not in (*PRODUCTION_TARGETS, *DIAGNOSTIC_TARGETS):
        raise TargetError(f"unknown conformance target {target_id!r}")
    return canonical


def resolve_target_ids(requested: list[str] | tuple[str, ...] | None) -> tuple[str, ...]:
    """Resolve repeated CLI selections, with ``all`` meaning the production matrix."""
    selections = list(requested or ("all",))
    resolved: list[str] = []
    for selection in selections:
        candidates = PRODUCTION_TARGETS if selection == "all" else (
            canonical_target_id(selection),
        )
        for candidate in candidates:
            if candidate not in resolved:
                resolved.append(candidate)
    return tuple(resolved)


def create_target(
    target_id: str,
    *,
    worker: str | Path | None = None,
    simulator: str | Path | None = None,
) -> ConformanceTarget:
    """Construct one target without leaking backend selection into test suites."""
    canonical = canonical_target_id(target_id)
    if canonical == "musashi-68030":
        return MusashiTarget(worker, cpu_model="68030") if worker else MusashiTarget()
    if canonical == "musashi-68000":
        return (
            MusashiTarget(worker, cpu_model="68000")
            if worker else MusashiTarget(cpu_model="68000")
        )
    if canonical == "rtl-tg68k030-mmu2":
        return RtlTarget(simulator) if simulator else RtlTarget()
    raise AssertionError(f"unhandled conformance target {canonical}")
