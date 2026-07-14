from pathlib import Path

import pytest

from conformance.model import compare_result, load_case
from conformance.targets.musashi import DEFAULT_WORKER, MusashiTarget


REPOSITORY = Path(__file__).resolve().parents[2]


@pytest.mark.skipif(not DEFAULT_WORKER.is_file(), reason="Musashi target is not built")
def test_checked_in_cpu_and_pmmu_cases_pass_on_one_persistent_target():
    paths = [
        REPOSITORY / "conformance/cases/cpu/add-long-register.json",
        REPOSITORY / "conformance/cases/cpu/fpu-absent-line-f.json",
        REPOSITORY / "conformance/cases/pmmu/table-bus-fault-ptest.json",
        REPOSITORY / "conformance/cases/pmmu/translation-instructions.json",
        REPOSITORY / "conformance/cases/pmmu/unaligned-fault-format-b.json",
    ]
    with MusashiTarget() as target:
        first_case = None
        for path in paths:
            case = load_case(path)
            if first_case is None:
                first_case = case
            assert compare_result(case, target.execute(case)) == []

        # A PMMU-heavy case must not poison the following flat instruction case.
        assert first_case is not None
        assert compare_result(first_case, target.execute(first_case)) == []

        manifest = target.manifest()
        assert manifest["implementation"] == "vendored-musashi-68030-pmmu-no-fpu"
        assert "no-fpu" in manifest["capabilities"]
        assert "table-bus-fault" in manifest["capabilities"]
        assert len(manifest["source"]["sha256"]) == 64
