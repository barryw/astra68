from pathlib import Path

import pytest

pytest.importorskip("conformance")

from conformance.model import ExecutionResult, MemorySegment
from conformance.targets.musashi import DEFAULT_WORKER
from sw.harte.host.harte_full import (
    MusashiFullHarteTarget,
    build_full_case,
    compare_full_result,
    expected_pc,
    sr_mask,
)
from sw.harte.host.tests.test_harte_case import raw_case


def full_raw_case(opcode=0x4E71):
    raw = raw_case(opcode)
    for state in (raw["initial"], raw["final"]):
        state["a7"] = 0x12345678
        state["usp"] = 0x00102030
        state["ssp"] = 0x00405060
    return raw


def result_for(case):
    cpu = {
        **{f"d{index}": value for index, value in enumerate(case.final.d)},
        **{f"a{index}": value for index, value in enumerate(case.final.a)},
        "a7": case.final.ssp if case.final.sr & 0x2000 else case.final.usp,
        "pc": case.final.pc,
        "sr": case.final.sr,
        "usp": case.final.usp,
        "isp": case.final.ssp,
    }
    memory = tuple(MemorySegment(address, bytes((value,)))
                   for address, value in case.final.ram)
    return ExecutionResult("instruction", 4, cpu, memory)


def test_full_case_compares_register_stack_sr_and_memory():
    raw = full_raw_case()
    raw["initial"]["ram"] = [[0x1000, 0x4E], [0x1001, 0x71]]
    raw["final"]["ram"] = [[0x1000, 0x4E], [0x1001, 0x71]]
    case = build_full_case(raw)
    assert compare_full_result(case, result_for(case)) == []

    result = result_for(case)
    result.cpu["d0"] ^= 1
    assert compare_full_result(case, result)[0].startswith("D0=")


def test_bcd_masks_architecturally_undefined_n_and_v():
    case = build_full_case(full_raw_case(0xC100))
    assert sr_mask(case) & 0x0A == 0


def test_successful_stop_corrects_prefetch_relative_final_pc():
    raw = full_raw_case(0x4E72)
    raw["transactions"] = [["n", 4]]
    raw["final"]["pc"] = raw["initial"]["pc"]
    case = build_full_case(raw)
    assert expected_pc(case) == case.initial.pc + 4


@pytest.mark.skipif(not DEFAULT_WORKER.is_file(), reason="Musashi target is not built")
def test_full_nop_executes_in_musashi_68000_mode():
    raw = full_raw_case()
    raw["initial"]["pc"] = 0x1000
    raw["final"]["pc"] = 0x1002
    raw["initial"]["prefetch"] = [0x4E71, 0x4E71]
    raw["final"]["prefetch"] = [0x4E71, 0x4E71]
    raw["initial"]["ram"] = [
        [0x1000, 0x4E], [0x1001, 0x71],
        [0x1002, 0x4E], [0x1003, 0x71],
    ]
    raw["final"]["ram"] = list(raw["initial"]["ram"])
    case = build_full_case(raw)
    with MusashiFullHarteTarget(Path(DEFAULT_WORKER)) as target:
        result, error = target.execute(case)
        assert error is None
        assert result is not None
        assert compare_full_result(case, result) == []
        assert target.manifest()["implementation"] == "vendored-musashi-68000"
