from pathlib import Path

import pytest

pytest.importorskip("conformance")

from conformance.targets.musashi import DEFAULT_WORKER
from conformance.targets.rtl import DEFAULT_SIMULATOR
from sw.harte.host.harte_case import build_case
from sw.harte.host.harte_run import compare_result
from sw.harte.host.musashi import (
    MusashiHarteTarget,
    RtlHarteTarget,
    to_conformance_case,
)
from sw.harte.host.tests.test_harte_case import raw_case


def representative_cases():
    nop = raw_case()

    moveq = raw_case(0x76FF)
    moveq["final"]["d3"] = 0xFFFFFFFF
    moveq["final"]["sr"] = 0x08

    add = raw_case(0xD081)
    add["initial"]["d0"] = 0xFFFFFFFF
    add["initial"]["d1"] = 1
    add["final"]["d0"] = 0
    add["final"]["d1"] = 1
    add["final"]["sr"] = 0x15
    return [build_case(raw) for raw in (nop, moveq, add)]


def test_harte_case_translation_owns_expected_state_and_masks():
    source = representative_cases()[2]
    case = to_conformance_case(source)
    assert case.case_id.startswith("harte/")
    assert case.requires == ("mc68030", "single-instruction")
    assert case.expected_cpu["d0"].value == source.fd[0]
    assert case.expected_cpu["sr"].value == source.fccr
    assert case.expected_cpu["sr"].mask == 0x1F


@pytest.mark.skipif(not DEFAULT_WORKER.is_file(), reason="Musashi target is not built")
def test_representative_harte_cases_use_common_musashi_target():
    with MusashiHarteTarget(Path(DEFAULT_WORKER)) as target:
        for case in representative_cases():
            payload, error = target.execute(case, 0, 0.1, 0.0)
            assert error is None
            assert payload is not None
            assert compare_result(case, payload) == []
        assert target.manifest()["suite_adapter"] == "harte-register-v1"


@pytest.mark.skipif(not DEFAULT_SIMULATOR.is_file(), reason="RTL target is not built")
def test_representative_harte_cases_use_common_rtl_target():
    with RtlHarteTarget(Path(DEFAULT_SIMULATOR)) as target:
        for case in representative_cases():
            payload, error = target.execute(case, 0, 0.1, 0.0)
            assert error is None
            assert payload is not None
            assert compare_result(case, payload) == []
        assert target.manifest()["suite_adapter"] == "harte-register-v1"
