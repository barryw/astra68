import pytest

from conformance.model import (
    CaseError,
    ExecutionResult,
    MemorySegment,
    compare_result,
    parse_case,
)


def minimal_case():
    return {
        "schema": 1,
        "id": "test/minimal",
        "initial": {
            "cpu": {"d0": "0xffffffff", "pc": "0x1000", "sr": "0x2000"},
            "memory": [{"address": "0x1000", "data": "4e71"}],
        },
        "run": {"mode": "instruction", "max_cycles": 10},
        "expect": {
            "cpu": {
                "d0": "0xffffffff",
                "sr": {"value": "0x2000", "mask": "0x201f"},
            },
            "memory": [{"address": "0x2000", "data": "1234", "mask": "fff0"}],
        },
    }


def test_parse_case_normalizes_numbers_bytes_and_observations():
    case = parse_case(minimal_case())
    assert case.initial_cpu["pc"] == 0x1000
    assert case.memory[0].data == b"\x4e\x71"
    assert case.observation_ranges()[-1].address == 0x2000


def test_compare_result_applies_register_and_memory_masks():
    case = parse_case(minimal_case())
    result = ExecutionResult(
        terminal="instruction",
        cycles=4,
        cpu={"d0": 0xFFFFFFFF, "sr": 0xA000},
        memory=(MemorySegment(0x2000, b"\x12\x3f"),),
    )
    assert compare_result(case, result) == []


def test_compare_result_reports_first_observable_difference():
    case = parse_case(minimal_case())
    result = ExecutionResult(
        terminal="cycle-limit",
        cycles=4,
        cpu={"d0": 0, "sr": 0x2001},
        memory=(MemorySegment(0x2000, b"\x02\x34"),),
    )
    mismatches = compare_result(case, result)
    assert any(item.startswith("terminal=") for item in mismatches)
    assert any(item.startswith("d0=") for item in mismatches)
    assert any(item.startswith("sr=") for item in mismatches)
    assert any(item.startswith("memory[") for item in mismatches)


def test_unknown_fixture_fields_fail_closed():
    raw = minimal_case()
    raw["initial"]["memroy"] = []
    with pytest.raises(CaseError, match="unknown fields: memroy"):
        parse_case(raw)
