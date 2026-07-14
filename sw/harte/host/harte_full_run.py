#!/usr/bin/env python3
"""Run every pinned Harte MC68000 vector against Musashi architectural state."""

from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys


REPOSITORY = Path(__file__).resolve().parents[3]
if str(REPOSITORY) not in sys.path:
    sys.path.insert(0, str(REPOSITORY))

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from harte_case import load
    from harte_full import (
        FullCase,
        FullCaseError,
        MusashiFullHarteTarget,
        build_full_case,
        compare_full_result,
    )
    from harte_run import corpus_manifest
else:
    from .harte_case import load
    from .harte_full import (
        FullCase,
        FullCaseError,
        MusashiFullHarteTarget,
        build_full_case,
        compare_full_result,
    )
    from .harte_run import corpus_manifest


HOST_SOURCE_NAMES = (
    "harte_case.py",
    "harte_full.py",
    "harte_full_run.py",
    "m68000_bin.py",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def host_manifest() -> dict:
    host_dir = Path(__file__).resolve().parent
    digest = hashlib.sha256()
    files = []
    for name in HOST_SOURCE_NAMES:
        path = host_dir / name
        content_hash = file_sha256(path)
        digest.update(name.encode("ascii") + b"\0" + bytes.fromhex(content_hash))
        files.append({"path": str(path), "sha256": content_hash})
    return {"sha256": digest.hexdigest(), "files": files}


def iter_cases(paths: list[str]):
    index = 0
    for path in paths:
        for raw in load(path):
            yield index, path, build_full_case(raw)
            index += 1


def scan(paths: list[str]) -> dict:
    total = 0
    malformed = Counter()
    files = []
    for path in paths:
        file_total = 0
        file_malformed = Counter()
        for raw in load(path):
            total += 1
            file_total += 1
            try:
                build_full_case(raw)
            except FullCaseError as exc:
                malformed[str(exc)] += 1
                file_malformed[str(exc)] += 1
        files.append({
            "path": str(Path(path).resolve()),
            "vectors": file_total,
            "malformed": dict(file_malformed),
        })
    return {
        "vectors": total,
        "admitted": total - sum(malformed.values()),
        "skipped": sum(malformed.values()),
        "skip_reasons": dict(malformed),
        "files": files,
    }


def write_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def components(mismatches: list[str]) -> tuple[str, ...]:
    found = set()
    for mismatch in mismatches:
        if mismatch.startswith("[") or mismatch.startswith("memory:"):
            found.add("memory")
        elif mismatch.startswith("SR="):
            found.add("sr")
        else:
            found.add("register")
    return tuple(sorted(found))


def run(paths: list[str], worker: Path | None, scope: dict, corpus: dict,
        start: int, limit: int | None, checkpoint_every: int,
        report_path: Path, only_class: str | None = None) -> int:
    started = utc_now()
    runner = host_manifest()
    passed = 0
    failed = 0
    attempted = 0
    next_index = start
    failures = []
    failure_files = Counter()
    failure_files_by_class: dict[str, Counter] = {}
    failure_opcodes = Counter()
    failure_components = Counter()
    attempted_classes = Counter()
    passed_classes = Counter()
    failure_classes = Counter()
    failure_examples_by_file: dict[str, list[dict]] = {}

    with MusashiFullHarteTarget(worker) as target:
        target_manifest = target.manifest()
        print(
            f"target={target_manifest['implementation']} "
            f"corpus={corpus['sha256'][:12]}"
        )

        def checkpoint() -> None:
            report = {
                "schema": 1,
                "started_utc": started,
                "updated_utc": utc_now(),
                "target": target_manifest,
                "runner": runner,
                "corpus": corpus,
                "scope": scope,
                "run": {
                    "start_index": start,
                    "limit": limit,
                    "only_class": only_class,
                    "checkpoint_every": checkpoint_every,
                    "next_index": next_index,
                    "attempted": attempted,
                    "passed": passed,
                    "failed": failed,
                    "failures": failures,
                    "failure_summary": {
                        "by_file": dict(sorted(failure_files.items())),
                        "by_opcode": dict(sorted(failure_opcodes.items())),
                        "components": dict(sorted(failure_components.items())),
                        "by_class": dict(sorted(failure_classes.items())),
                        "by_class_and_file": {
                            classification: dict(sorted(counts.items()))
                            for classification, counts in sorted(
                                failure_files_by_class.items()
                            )
                        },
                        "examples_by_file": failure_examples_by_file,
                    },
                    "coverage_by_class": {
                        classification: {
                            "attempted": count,
                            "passed": passed_classes[classification],
                            "failed": failure_classes[classification],
                        }
                        for classification, count in sorted(attempted_classes.items())
                    },
                },
            }
            write_report(report_path, report)

        for index, path, case in iter_cases(paths):
            if index < start:
                continue
            if only_class is not None and case.classification != only_class:
                continue
            if limit is not None and attempted >= limit:
                break
            result, execution_error = target.execute(case)
            attempted += 1
            attempted_classes[case.classification] += 1
            next_index = index + 1
            if result is None:
                mismatches = [execution_error or "target returned no result"]
                failure_kind = ("transport",)
            else:
                mismatches = compare_full_result(case, result)
                failure_kind = components(mismatches)
            if mismatches:
                failed += 1
                detail = "; ".join(mismatches)
                filename = Path(path).name
                record = {
                    "index": index,
                    "file": str(Path(path).resolve()),
                    "name": case.name,
                    "opcode": f"{case.opcode:04x}",
                    "error": detail,
                }
                failure_files[filename] += 1
                failure_files_by_class.setdefault(
                    case.classification, Counter()
                )[filename] += 1
                failure_opcodes[f"{case.opcode:04x}"] += 1
                failure_components.update(failure_kind)
                failure_classes[case.classification] += 1
                if len(failures) < 100:
                    failures.append(record)
                examples = failure_examples_by_file.setdefault(filename, [])
                if len(examples) < 5:
                    examples.append(record)
                if failed <= 100:
                    print(f"FAIL [{index}] {case.name}: {detail}")
            else:
                passed += 1
                passed_classes[case.classification] += 1

            if attempted % 1000 == 0:
                print(f"progress index={next_index} pass={passed} fail={failed}")
            if checkpoint_every > 0 and attempted % checkpoint_every == 0:
                checkpoint()

        checkpoint()

    print(
        f"PASS={passed} FAIL={failed} ATTEMPTED={attempted} "
        f"NEXT_INDEX={next_index} REPORT={report_path}"
    )
    return 0 if passed and failed == 0 else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("vectors", nargs="+")
    parser.add_argument("--worker", type=Path)
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--limit", type=int)
    parser.add_argument("--checkpoint-every", type=int, default=1000)
    parser.add_argument("--report", type=Path,
                        default=Path("/tmp/astra68-harte-full-report.json"))
    parser.add_argument("--allow-unpinned-corpus", action="store_true")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument(
        "--only-class",
        choices=(
            "ordinary", "address-error", "upstream-caveat-tas",
            "upstream-caveat-trapv",
        ),
        help="execute only one classified vector group (default: all)",
    )
    args = parser.parse_args(argv)
    if args.start < 0:
        parser.error("--start must be non-negative")
    if args.limit is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    if args.checkpoint_every < 0:
        parser.error("--checkpoint-every must not be negative")

    paths = sorted(str(Path(path)) for path in args.vectors)
    scope = scan(paths)
    print(
        f"vectors={scope['vectors']} in_scope={scope['admitted']} "
        f"skipped={scope['skipped']}"
    )
    for reason, count in sorted(
        scope["skip_reasons"].items(), key=lambda item: -item[1]
    ):
        print(f"  skip {count:7d}  {reason}")
    if args.dry_run:
        return 0 if scope["admitted"] else 1
    if scope["skipped"]:
        parser.error("full corpus contains malformed vectors; refusing partial run")

    corpus = corpus_manifest(paths)
    if corpus["revision"] is None and not args.allow_unpinned_corpus:
        parser.error("vector corpus is unpinned; use fetch_vectors.sh")
    try:
        return run(
            paths, args.worker, scope, corpus, args.start, args.limit,
            args.checkpoint_every, args.report, args.only_class,
        )
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"HARTE INFRASTRUCTURE FAILURE: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
