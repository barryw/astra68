from __future__ import annotations

import pytest

from sw.boot.check_monitor_hardware import normalize_build_id, validate_responses


def valid_responses() -> dict[str, bytes]:
    return {
        "build": (
            b"kernel=0.1.0-dev built=2026-07-28T02:47:45Z "
            b"git=4579138e-dirty-d545ea4c hw=D545EA4C"
        ),
        "irqs": (
            b"live=0 delivered=5 acked=5 dropped=0 storms=0 "
            b"spurious=0 irqoff_max=768837"
        ),
        "mem": b"total=8192 free=7954 high_water=238 failures=23 owners=8",
        "trace": (
            b"next=512 wraps=0 dropped=0 event=18 flags=0x00000000 "
            b"arg0=0x00000005"
        ),
        "devices": (
            b"system=0x0000000F block=0x00000003 input=1 worker_max=18839 "
            b"mon_ftdi=4 mon_spi=4"
        ),
    }


def test_validate_responses_accepts_both_transports() -> None:
    assert validate_responses(valid_responses(), b"D545EA4C", 4) == (4, 4)


@pytest.mark.parametrize(
    ("field", "replacement"),
    (
        ("build", b"kernel=0.1 built=now git=bad hw=00000000"),
        ("devices", b"system=0x0000000F mon_ftdi=4 mon_spi=4"),
        (
            "devices",
            b"system=0x0000000F block=0x00000003 input=1 worker_max=1 "
            b"mon_ftdi=3 mon_spi=4",
        ),
        (
            "devices",
            b"system=0x0000000F block=0x00000003 input=1 worker_max=1 "
            b"mon_ftdi=4 mon_spi=3",
        ),
    ),
)
def test_validate_responses_rejects_bad_evidence(
    field: str, replacement: bytes
) -> None:
    responses = valid_responses()
    responses[field] = replacement
    with pytest.raises(ValueError):
        validate_responses(responses, b"D545EA4C", 4)


def test_normalize_build_id() -> None:
    assert normalize_build_id("0xd545ea4c") == b"D545EA4C"
    with pytest.raises(ValueError):
        normalize_build_id("D545EA4")
