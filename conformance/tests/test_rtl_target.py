from pathlib import Path

import pytest

from conformance.model import compare_result, load_case
from conformance.target import TargetError
from conformance.targets.rtl import (
    DEFAULT_SIMULATOR,
    RtlTarget,
    _prepare_memory,
)


REPOSITORY = Path(__file__).resolve().parents[2]


def test_bootstrap_preserves_fixture_reset_target():
    case = load_case(REPOSITORY / "conformance/cases/cpu/add-long-register.json")
    memory, target_pc = _prepare_memory(case)
    assert target_pc == 0x1000
    bootstrap_pc = int.from_bytes(bytes(memory[address] for address in range(4, 8)), "big")
    assert bootstrap_pc != target_pc


def test_missing_simulator_fails_closed(tmp_path):
    with pytest.raises(TargetError, match="simulator is missing"):
        RtlTarget(tmp_path / "missing-simulator")


@pytest.mark.skipif(not DEFAULT_SIMULATOR.is_file(), reason="RTL target is not built")
def test_add_fixture_executes_on_rtl():
    case = load_case(REPOSITORY / "conformance/cases/cpu/add-long-register.json")
    with RtlTarget() as target:
        actual = target.execute(case)
    assert compare_result(case, actual) == []
