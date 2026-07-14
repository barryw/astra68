"""Run backend-neutral Astra68 conformance fixtures against a target."""

from __future__ import annotations

import argparse
from contextlib import ExitStack
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

from .model import CaseError, compare_result, load_case
from .target import TargetError
from .targets import create_target, resolve_target_ids, target_choices


RUNNER_PATHS = (
    "conformance/__init__.py",
    "conformance/model.py",
    "conformance/runner.py",
    "conformance/schema/case-v1.schema.json",
    "conformance/target.py",
    "conformance/targets/registry.py",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def file_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def runner_manifest() -> dict:
    repository = Path(__file__).resolve().parent.parent
    digest = hashlib.sha256()
    files = []
    for relative in RUNNER_PATHS:
        path = repository / relative
        sha256 = file_sha256(path)
        digest.update(relative.encode("utf-8") + b"\0" + bytes.fromhex(sha256))
        files.append({"path": relative, "sha256": sha256})
    return {"sha256": digest.hexdigest(), "files": files}


def discover(paths: list[str]) -> list[Path]:
    discovered = []
    for raw in paths:
        path = Path(raw)
        if path.is_dir():
            discovered.extend(sorted(path.rglob("*.json")))
        else:
            discovered.append(path)
    return sorted(path.resolve() for path in discovered)


def write_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def load_cases(paths: list[Path]):
    cases = [load_case(path) for path in paths]
    identifiers = [case.case_id for case in cases]
    duplicates = sorted({case_id for case_id in identifiers if identifiers.count(case_id) > 1})
    if duplicates:
        raise CaseError("duplicate case IDs: " + ", ".join(duplicates))
    return cases


def execute_cases(cases, target) -> dict:
    """Execute already-parsed cases and return one target's report payload."""
    target_manifest = target.manifest()
    supported = set(target_manifest.get("architecture", []))
    supported.update(target_manifest.get("features", []))
    supported.update(target_manifest.get("capabilities", []))
    results = []
    passed = 0
    failed = 0
    for case in cases:
        missing = sorted(set(case.requires) - supported)
        if missing:
            actual = None
            mismatches = []
            infrastructure_error = "target lacks required features: " + ", ".join(missing)
        else:
            try:
                actual = target.execute(case)
                mismatches = compare_result(case, actual)
                infrastructure_error = None
            except TargetError as exc:
                actual = None
                mismatches = []
                infrastructure_error = str(exc)

        record = {
            "id": case.case_id,
            "source": str(case.source) if case.source else None,
            "source_sha256": file_sha256(case.source) if case.source else None,
            "authority": list(case.authority),
            "derived_from": list(case.derived_from),
            "requires": list(case.requires),
            "cycles": actual.cycles if actual else None,
            "terminal": actual.terminal if actual else None,
            "mismatches": mismatches,
            "infrastructure_error": infrastructure_error,
        }
        results.append(record)
        if infrastructure_error is not None:
            failed += 1
            print(f"ERROR {case.case_id}: {infrastructure_error}")
        elif mismatches:
            failed += 1
            print(f"FAIL  {case.case_id}: {'; '.join(mismatches)}")
        else:
            passed += 1
            print(f"PASS  {case.case_id} ({actual.cycles} cycles)")

    return {
        "target": target_manifest,
        "summary": {"total": len(cases), "passed": passed, "failed": failed},
        "cases": results,
    }


def run(paths: list[Path], target, report_path: Path | None = None) -> int:
    """Run one target; retained as the stable API for callers and unit tests."""
    cases = load_cases(paths)
    started = utc_now()
    outcome = execute_cases(cases, target)
    report = {
        "schema": 1,
        "started_utc": started,
        "completed_utc": utc_now(),
        "runner": runner_manifest(),
        **outcome,
    }
    if report_path is not None:
        write_report(report_path, report)
    print(
        f"RESULT total={len(cases)} passed={outcome['summary']['passed']} "
        f"failed={outcome['summary']['failed']}"
        + (f" report={report_path}" if report_path else "")
    )
    return 0 if cases and outcome["summary"]["failed"] == 0 else 1


def run_matrix(
    paths: list[Path],
    targets: list[tuple[str, object]],
    report_path: Path | None = None,
) -> int:
    """Run the exact same parsed cases on every selected execution adapter."""
    cases = load_cases(paths)
    started = utc_now()
    outcomes = []
    total_passed = 0
    total_failed = 0
    failed_targets = 0
    for target_id, target in targets:
        print(f"TARGET {target_id}")
        outcome = execute_cases(cases, target)
        outcome["id"] = target_id
        outcomes.append(outcome)
        summary = outcome["summary"]
        total_passed += summary["passed"]
        total_failed += summary["failed"]
        if summary["failed"]:
            failed_targets += 1
        print(
            f"TARGET_RESULT {target_id} total={summary['total']} "
            f"passed={summary['passed']} failed={summary['failed']}"
        )

    report = {
        "schema": 2,
        "started_utc": started,
        "completed_utc": utc_now(),
        "runner": runner_manifest(),
        "summary": {
            "targets": len(targets),
            "failed_targets": failed_targets,
            "case_executions": len(cases) * len(targets),
            "passed": total_passed,
            "failed": total_failed,
        },
        "targets": outcomes,
    }
    if report_path is not None:
        write_report(report_path, report)
    print(
        f"MATRIX_RESULT targets={len(targets)} cases={len(cases)} "
        f"passed={total_passed} failed={total_failed}"
        + (f" report={report_path}" if report_path else "")
    )
    return 0 if cases and targets and total_failed == 0 else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cases", nargs="+", help="case JSON files or directories")
    parser.add_argument(
        "--target",
        action="append",
        choices=("all", *target_choices()),
        help=(
            "execution target; repeat for a matrix (default: all production "
            "targets; musashi and rtl remain aliases)"
        ),
    )
    parser.add_argument("--worker", type=Path, help="Musashi target executable")
    parser.add_argument("--simulator", type=Path, help="RTL simulator executable")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)

    paths = discover(args.cases)
    if not paths:
        parser.error("no case files found")
    target_ids = resolve_target_ids(args.target)
    if args.worker and not any(name.startswith("musashi-") for name in target_ids):
        parser.error("--worker requires a Musashi target")
    if args.simulator and "rtl-tg68k030-mmu2" not in target_ids:
        parser.error("--simulator requires the TG68K RTL target")

    try:
        with ExitStack() as stack:
            targets = []
            for target_id in target_ids:
                target = create_target(
                    target_id,
                    worker=args.worker if target_id.startswith("musashi-") else None,
                    simulator=(
                        args.simulator
                        if target_id == "rtl-tg68k030-mmu2" else None
                    ),
                )
                stack.callback(target.close)
                targets.append((target_id, target))
            if len(targets) == 1:
                return run(paths, targets[0][1], args.report)
            return run_matrix(paths, targets, args.report)
    except (CaseError, TargetError, OSError) as exc:
        print(f"CONFORMANCE INFRASTRUCTURE FAILURE: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
