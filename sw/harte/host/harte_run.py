#!/usr/bin/env python3
"""Run the Motorola-invariant Tom Harte 68000 subset on Astra hardware."""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import asdict
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import sys
import time
from typing import Iterable

REPOSITORY = Path(__file__).resolve().parents[3]
if str(REPOSITORY) not in sys.path:
    sys.path.insert(0, str(REPOSITORY))

from conformance.model import (  # noqa: E402 - repository path is established above.
    ExecutionResult,
    compare_result as compare_conformance_result,
)
from conformance.targets import (  # noqa: E402
    PRODUCTION_TARGETS,
    canonical_target_id,
)

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from harte_case import Case, CaseError, build_case, load, scope_reason
    from proto import (CMD_ERROR, CMD_INFO, CMD_INFOR, CMD_RESULT, CMD_RUN,
                       describe_error, parse_device_info)
    from transport import find_port, transact
    from musashi import RegisteredHarteTarget, to_conformance_case
else:
    from .harte_case import Case, CaseError, build_case, load, scope_reason
    from .proto import (CMD_ERROR, CMD_INFO, CMD_INFOR, CMD_RESULT, CMD_RUN,
                        describe_error, parse_device_info)
    from .transport import find_port, transact
    from .musashi import RegisteredHarteTarget, to_conformance_case


RESULT_PAYLOAD_BYTES = 61
DEFAULT_BAUD = int(os.environ.get("ASTRA_BAUD", "460800"))
BUILD_ID_HEADER = Path(__file__).resolve().parents[1] / "build_id.h"
HOST_SOURCE_NAMES = (
    "flags.py",
    "harte_case.py",
    "harte_run.py",
    "m68000_bin.py",
    "musashi.py",
    "proto.py",
    "transport.py",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def encode_run(case: Case) -> bytes:
    payload = b"".join(struct.pack(">I", value) for value in case.d + case.a)
    payload += bytes((case.ccr, case.ilen)) + case.instr
    return payload


def decode_result(payload: bytes) -> tuple[tuple[int, ...], tuple[int, ...], int]:
    if len(payload) != RESULT_PAYLOAD_BYTES:
        raise ValueError(f"RESULT payload is {len(payload)} bytes, expected {RESULT_PAYLOAD_BYTES}")
    regs = tuple(struct.unpack_from(">I", payload, offset)[0]
                 for offset in range(0, 60, 4))
    return regs[:8], regs[8:], payload[60]


def payload_result(payload: bytes) -> ExecutionResult:
    """Normalize the FPGA register protocol into the common result model."""
    actual_d, actual_a, actual_ccr = decode_result(payload)
    cpu = {f"d{index}": value for index, value in enumerate(actual_d)}
    cpu.update({f"a{index}": value for index, value in enumerate(actual_a)})
    cpu["sr"] = actual_ccr
    return ExecutionResult(
        terminal="instruction",
        cycles=0,
        cpu=cpu,
        memory=(),
    )


def compare_result(case: Case, actual: bytes | ExecutionResult) -> list[str]:
    """Compare Harte-derived expectations through the shared masked comparator."""
    result = payload_result(actual) if isinstance(actual, bytes) else actual
    return compare_conformance_result(to_conformance_case(case), result)


def execute_case(serial_port, case: Case, retries: int, timeout: float,
                 pace_seconds: float) -> tuple[bytes | None, str | None]:
    last_error = "no response"
    for _ in range(retries + 1):
        response = transact(serial_port, CMD_RUN, encode_run(case), timeout, pace_seconds)
        if response is None:
            last_error = "no valid response"
            continue
        command, payload = response
        if command == CMD_RESULT:
            return payload, None
        if command == CMD_ERROR:
            last_error = describe_error(payload)
            continue
        last_error = f"unexpected response command 0x{command:02x}"
    return None, last_error


def iter_admitted(paths: Iterable[str]):
    admitted_index = 0
    for path in paths:
        for raw in load(path):
            if scope_reason(raw) is not None:
                continue
            try:
                case = build_case(raw)
            except CaseError:
                continue
            yield admitted_index, path, case
            admitted_index += 1


def scan_scope(paths: list[str]) -> dict:
    total = 0
    admitted = 0
    skipped = Counter()
    files = []
    for path in paths:
        file_total = 0
        file_admitted = 0
        file_skipped = Counter()
        for raw in load(path):
            total += 1
            file_total += 1
            reason = scope_reason(raw)
            if reason is None:
                try:
                    build_case(raw)
                except CaseError as exc:
                    reason = str(exc)
            if reason is None:
                admitted += 1
                file_admitted += 1
            else:
                skipped[reason] += 1
                file_skipped[reason] += 1
        files.append({
            "path": str(Path(path).resolve()),
            "vectors": file_total,
            "admitted": file_admitted,
            "skipped": dict(file_skipped),
        })
    return {
        "vectors": total,
        "admitted": admitted,
        "skipped": sum(skipped.values()),
        "skip_reasons": dict(skipped),
        "files": files,
    }


def file_sha256(path: str) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def corpus_revision(paths: list[str]) -> str | None:
    revisions = set()
    for path in paths:
        for parent in Path(path).resolve().parents:
            marker = parent / ".astra-harte-revision"
            if marker.is_file():
                revisions.add(marker.read_text().strip())
                break
    if len(revisions) > 1:
        raise ValueError(f"vector files span multiple pinned revisions: {sorted(revisions)}")
    return next(iter(revisions), None)


def corpus_manifest(paths: list[str]) -> dict:
    digest = hashlib.sha256()
    files = []
    for path in paths:
        resolved = Path(path).resolve()
        sha256 = file_sha256(str(resolved))
        size = resolved.stat().st_size
        digest.update(resolved.name.encode("utf-8") + b"\0")
        digest.update(bytes.fromhex(sha256))
        files.append({"path": str(resolved), "bytes": size, "sha256": sha256})
    return {
        "revision": corpus_revision(paths),
        "sha256": digest.hexdigest(),
        "files": files,
    }


def host_manifest() -> dict:
    host_dir = Path(__file__).resolve().parent
    digest = hashlib.sha256()
    files = []
    for name in HOST_SOURCE_NAMES:
        path = host_dir / name
        sha256 = file_sha256(str(path))
        digest.update(name.encode("ascii") + b"\0")
        digest.update(bytes.fromhex(sha256))
        files.append({"path": str(path), "sha256": sha256})
    return {"sha256": digest.hexdigest(), "files": files}


def parse_expected_build_id(value: str | None) -> int | None:
    if value is not None:
        return int(value, 0)
    try:
        match = re.search(r"BUILD_ID\s+0x([0-9a-fA-F]{8})", BUILD_ID_HEADER.read_text())
    except FileNotFoundError:
        return None
    return int(match.group(1), 16) if match else None


def write_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
    temporary.replace(path)


def print_scope(scope: dict) -> None:
    print(f"vectors={scope['vectors']} in_scope={scope['admitted']} skipped={scope['skipped']}")
    for reason, count in sorted(scope["skip_reasons"].items(), key=lambda item: -item[1]):
        print(f"  skip {count:7d}  {reason}")


def dry_run(paths: list[str]) -> int:
    scope = scan_scope(paths)
    print_scope(scope)
    return 0 if scope["admitted"] else 1


def preflight(serial_port, timeout: float, expected_build_id: int | None,
              allow_unpinned: bool):
    response = transact(serial_port, CMD_INFO, b"", timeout)
    if response is None or response[0] != CMD_INFOR:
        raise RuntimeError("device did not return a protocol-v2 INFO response")
    info = parse_device_info(response[1])
    if info.protocol_major != 2:
        raise RuntimeError(f"unsupported device protocol {info.protocol_major}.{info.protocol_minor}")
    if (info.features & 0x03) != 0x03:
        raise RuntimeError(f"device lacks required strict-frame/RX-FIFO features: 0x{info.features:02x}")
    if expected_build_id is None and not allow_unpinned:
        raise RuntimeError("no expected BUILD_ID; build the harness first or pass --expect-build-id")
    if expected_build_id is not None and info.build_id != expected_build_id:
        raise RuntimeError(
            f"device BUILD_ID 0x{info.build_id:08x} != expected 0x{expected_build_id:08x}"
        )
    return info


class HardwareHarteTarget:
    def __init__(self, serial_port, timeout: float, expected_build_id: int | None,
                 allow_unpinned: bool):
        self.serial_port = serial_port
        self.info = preflight(
            serial_port, timeout, expected_build_id, allow_unpinned
        )

    def manifest(self) -> dict:
        return {
            "target_id": "fpga-tg68k030-mmu2",
            "kind": "fpga",
            "implementation": "astra68-hardware",
            "suite_adapter": "harte-register-v2-uart",
            "device": asdict(self.info),
        }

    def execute(self, case: Case, retries: int, timeout: float,
                pace_seconds: float):
        payload, error = execute_case(
            self.serial_port, case, retries, timeout, pace_seconds
        )
        if payload is None:
            return None, error
        try:
            return payload_result(payload), None
        except ValueError as exc:
            return None, str(exc)


def run(paths: list[str], target, scope: dict, corpus: dict, start: int,
        limit: int | None, retries: int, timeout: float, pace_seconds: float,
        max_consecutive_timeouts: int, checkpoint_every: int,
        report_path: Path) -> int:
    target_manifest = target.manifest()
    implementation = target_manifest.get("implementation", "unknown")
    print(f"target={implementation} corpus={corpus['sha256'][:12]}")

    started = utc_now()
    runner = host_manifest()
    passed = 0
    failed = 0
    attempted = 0
    consecutive_timeouts = 0
    failures = []
    failure_files = Counter()
    failure_opcodes = Counter()
    failure_components = Counter()
    failure_examples_by_file: dict[str, list[dict]] = {}
    aborted = None
    next_index = start

    def record_failure(admitted_index: int, path: str, case: Case,
                       detail: str, components: Iterable[str]) -> None:
        record = {
            "index": admitted_index,
            "file": str(Path(path).resolve()),
            "name": case.name,
            "error": detail,
        }
        filename = Path(path).name
        failure_files[filename] += 1
        failure_opcodes[f"{case.opcode:04x}"] += 1
        failure_components.update(components)
        if len(failures) < 100:
            failures.append(record)
        examples = failure_examples_by_file.setdefault(filename, [])
        if len(examples) < 5:
            examples.append(record)

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
                "retries": retries,
                "timeout_seconds": timeout,
                "pace_seconds": pace_seconds,
                "max_consecutive_timeouts": max_consecutive_timeouts,
                "checkpoint_every": checkpoint_every,
                "next_admitted_index": next_index,
                "attempted": attempted,
                "passed": passed,
                "failed": failed,
                "aborted": aborted,
                "failures": failures,
                "failure_summary": {
                    "by_file": dict(sorted(failure_files.items())),
                    "by_opcode": dict(sorted(failure_opcodes.items())),
                    "components": dict(sorted(failure_components.items())),
                    "examples_by_file": failure_examples_by_file,
                },
            },
        }
        if "device" in target_manifest:
            # Preserve the hardware report field for existing report consumers.
            report["device"] = target_manifest["device"]
        write_report(report_path, report)

    for admitted_index, path, case in iter_admitted(paths):
        if admitted_index < start:
            continue
        if limit is not None and attempted >= limit:
            break

        actual, transport_error = target.execute(
            case, retries, timeout, pace_seconds
        )
        attempted += 1
        next_index = admitted_index + 1

        if actual is None:
            failed += 1
            consecutive_timeouts += 1
            detail = transport_error or "no response"
            print(f"FAIL [{admitted_index}] {case.name}: {detail}")
            record_failure(admitted_index, path, case, detail, ("transport",))
            if consecutive_timeouts >= max_consecutive_timeouts:
                aborted = f"{consecutive_timeouts} consecutive transport failures"
                checkpoint()
                break
        else:
            consecutive_timeouts = 0
            try:
                mismatches = compare_result(case, actual)
            except ValueError as exc:
                mismatches = [str(exc)]
            if mismatches:
                failed += 1
                detail = "; ".join(mismatches)
                print(f"FAIL [{admitted_index}] {case.name}: {detail}")
                components = []
                if any(item.startswith(("d", "a")) for item in mismatches):
                    components.append("register")
                if any(item.startswith("sr=") for item in mismatches):
                    components.append("ccr")
                record_failure(admitted_index, path, case, detail, components)
            else:
                passed += 1

        if attempted % 1000 == 0:
            print(f"progress index={next_index} pass={passed} fail={failed}")
        if checkpoint_every > 0 and attempted % checkpoint_every == 0:
            checkpoint()

    checkpoint()
    print(
        f"PASS={passed} FAIL={failed} ATTEMPTED={attempted} "
        f"NEXT_INDEX={next_index} ABORTED={aborted or 'no'} REPORT={report_path}"
    )
    return 0 if passed and failed == 0 and aborted is None else 1


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "vectors", nargs="+",
        help="pinned m68000 .json.bin or legacy Harte .json/.json.gz files",
    )
    parser.add_argument("--dry-run", action="store_true", help="enumerate scope without hardware")
    parser.add_argument(
        "--target",
        choices=("hardware", "musashi", "rtl", *PRODUCTION_TARGETS),
        default="hardware",
        help="execution target (default: hardware)",
    )
    parser.add_argument("--worker", type=Path, help="Musashi target executable")
    parser.add_argument("--simulator", type=Path, help="RTL simulator executable")
    parser.add_argument("--start", type=int, default=0, help="first admitted corpus index")
    parser.add_argument("--limit", type=int, help="maximum cases to execute after --start")
    parser.add_argument("--port", default=find_port())
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument("--timeout", type=float, default=0.5)
    parser.add_argument("--retries", type=int, default=2)
    parser.add_argument("--pace-ms", type=float, default=0.0)
    parser.add_argument("--expect-build-id", help="required device BUILD_ID (default: build_id.h)")
    parser.add_argument("--allow-unpinned-device", action="store_true")
    parser.add_argument("--allow-unpinned-corpus", action="store_true")
    parser.add_argument("--max-consecutive-timeouts", type=int, default=1)
    parser.add_argument("--checkpoint-every", type=int, default=1000)
    parser.add_argument("--report", type=Path, default=Path("/tmp/astra68-harte-report.json"))
    args = parser.parse_args(argv)

    if args.start < 0:
        parser.error("--start must be non-negative")
    if args.limit is not None and args.limit <= 0:
        parser.error("--limit must be positive")
    if args.max_consecutive_timeouts <= 0:
        parser.error("--max-consecutive-timeouts must be positive")
    shared_target_id = (
        canonical_target_id(args.target) if args.target != "hardware" else None
    )
    if args.worker and (
        shared_target_id is None or not shared_target_id.startswith("musashi-")
    ):
        parser.error("--worker requires a Musashi target")
    if args.simulator and shared_target_id != "rtl-tg68k030-mmu2":
        parser.error("--simulator requires the TG68K RTL target")

    paths = sorted(str(Path(path)) for path in args.vectors)
    scope = scan_scope(paths)
    print_scope(scope)
    if args.dry_run:
        return 0 if scope["admitted"] else 1

    corpus = corpus_manifest(paths)
    if corpus["revision"] is None and not args.allow_unpinned_corpus:
        parser.error("vector corpus has no .astra-harte-revision marker; use fetch_vectors.sh")

    try:
        if shared_target_id is not None:
            shared_target = RegisteredHarteTarget(
                shared_target_id,
                worker=args.worker,
                simulator=args.simulator,
            )
            with shared_target as target:
                return run(
                    paths, target, scope, corpus, args.start, args.limit,
                    args.retries, args.timeout, args.pace_ms / 1000.0,
                    args.max_consecutive_timeouts, args.checkpoint_every,
                    args.report,
                )

        try:
            import serial
        except ImportError:
            print("pyserial is required for hardware runs", file=sys.stderr)
            return 2
        expected_build_id = parse_expected_build_id(args.expect_build_id)
        with serial.Serial(args.port, args.baud, timeout=min(args.timeout, 0.1)) as serial_port:
            time.sleep(0.25)
            target = HardwareHarteTarget(
                serial_port, args.timeout, expected_build_id,
                args.allow_unpinned_device,
            )
            return run(
                paths, target, scope, corpus, args.start, args.limit,
                args.retries, args.timeout, args.pace_ms / 1000.0,
                args.max_consecutive_timeouts, args.checkpoint_every,
                args.report,
            )
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"HARTE INFRASTRUCTURE FAILURE: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
