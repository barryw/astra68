#!/usr/bin/env python3
"""Attribute pin-level TG68K PC samples to ELF text symbols."""

from __future__ import annotations

import argparse
import bisect
from collections import Counter
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Symbol:
    start: int
    end: int
    name: str


def parse_nm(path: Path) -> list[Symbol]:
    records: list[tuple[int, int | None, str]] = []
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split()
        if len(fields) == 4:
            address, size, kind, name = fields
            explicit_size: int | None = int(size, 16)
        elif len(fields) == 3:
            address, kind, name = fields
            explicit_size = None
        else:
            continue
        if kind not in ("T", "t"):
            continue
        try:
            start = int(address, 16)
        except ValueError as error:
            raise ValueError(f"{path}:{line_number}: invalid address") from error
        records.append((start, explicit_size, name))

    if not records:
        raise ValueError(f"{path}: no text symbols")

    records.sort(key=lambda record: (record[0], record[1] is None, record[2]))
    distinct_starts = sorted({record[0] for record in records})
    next_start = {
        start: distinct_starts[index + 1]
        for index, start in enumerate(distinct_starts[:-1])
    }

    by_start: dict[int, Symbol] = {}
    for start, explicit_size, name in records:
        end = start + explicit_size if explicit_size else next_start.get(start, start + 1)
        candidate = Symbol(start, end, name)
        current = by_start.get(start)
        if current is None or candidate.end > current.end:
            by_start[start] = candidate

    return [by_start[start] for start in sorted(by_start)]


def attribute_trace(path: Path, symbols: list[Symbol]) -> tuple[Counter[str], int]:
    starts = [symbol.start for symbol in symbols]
    counts: Counter[str] = Counter()
    unmapped = 0
    for line_number, line in enumerate(path.read_text().splitlines(), 1):
        fields = line.split()
        if len(fields) < 2:
            continue
        try:
            pc = int(fields[1], 16)
        except ValueError as error:
            raise ValueError(f"{path}:{line_number}: invalid PC") from error
        index = bisect.bisect_right(starts, pc) - 1
        if index < 0 or pc >= symbols[index].end:
            unmapped += 1
        else:
            counts[symbols[index].name] += 1
    return counts, unmapped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline-nm", required=True, type=Path)
    parser.add_argument("--baseline-trace", required=True, type=Path)
    parser.add_argument("--candidate-nm", required=True, type=Path)
    parser.add_argument("--candidate-trace", required=True, type=Path)
    parser.add_argument("--limit", type=int, default=40)
    args = parser.parse_args()

    baseline, baseline_unmapped = attribute_trace(
        args.baseline_trace, parse_nm(args.baseline_nm)
    )
    candidate, candidate_unmapped = attribute_trace(
        args.candidate_trace, parse_nm(args.candidate_nm)
    )
    baseline_total = sum(baseline.values()) + baseline_unmapped
    candidate_total = sum(candidate.values()) + candidate_unmapped

    print(
        f"baseline={baseline_total} candidate={candidate_total} "
        f"delta={candidate_total - baseline_total:+d} "
        f"unmapped={baseline_unmapped}/{candidate_unmapped}"
    )
    print(f"{'symbol':44} {'baseline':>10} {'candidate':>10} {'delta':>10}")
    rows = [
        (candidate[name] - baseline[name], name, baseline[name], candidate[name])
        for name in baseline.keys() | candidate.keys()
    ]
    rows.sort(key=lambda row: (row[0], row[3], row[1]), reverse=True)
    for delta, name, old, new in rows[: args.limit]:
        print(f"{name:44.44} {old:10d} {new:10d} {delta:+10d}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
