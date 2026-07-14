import json

from conformance.model import ExecutionResult
from conformance.runner import run, run_matrix
from conformance.targets import resolve_target_ids


class UnsupportedTarget:
    executed = False

    @staticmethod
    def manifest():
        return {
            "kind": "test",
            "implementation": "no-pmmu",
            "architecture": ["mc68030"],
        }

    def execute(self, _case):
        self.executed = True
        return ExecutionResult("instruction", 0, {}, ())


def test_required_features_fail_closed_before_execution(tmp_path):
    fixture = tmp_path / "requires-pmmu.json"
    fixture.write_text(json.dumps({
        "schema": 1,
        "id": "test/requires-pmmu",
        "requires": ["mc68030", "pmmu"],
        "initial": {},
        "run": {"mode": "instruction"},
        "expect": {},
    }))
    target = UnsupportedTarget()
    assert run([fixture], target) == 1
    assert not target.executed


class PassingTarget:
    def __init__(self, name):
        self.name = name
        self.case_objects = []

    def manifest(self):
        return {
            "target_id": self.name,
            "kind": "test",
            "implementation": self.name,
            "architecture": ["mc68030"],
        }

    def execute(self, case):
        self.case_objects.append(case)
        return ExecutionResult("instruction", 1, {}, ())


def test_matrix_runs_same_parsed_case_on_every_target(tmp_path):
    fixture = tmp_path / "shared.json"
    fixture.write_text(json.dumps({
        "schema": 1,
        "id": "test/shared",
        "requires": ["mc68030"],
        "initial": {},
        "run": {"mode": "instruction"},
        "expect": {},
    }))
    report_path = tmp_path / "matrix.json"
    first = PassingTarget("first")
    second = PassingTarget("second")

    assert run_matrix(
        [fixture], [("first", first), ("second", second)], report_path
    ) == 0
    assert first.case_objects[0] is second.case_objects[0]
    report = json.loads(report_path.read_text())
    assert report["schema"] == 2
    assert report["summary"] == {
        "targets": 2,
        "failed_targets": 0,
        "case_executions": 2,
        "passed": 2,
        "failed": 0,
    }
    assert [target["id"] for target in report["targets"]] == ["first", "second"]


def test_target_resolution_defaults_to_production_matrix_and_keeps_aliases():
    assert resolve_target_ids(None) == (
        "musashi-68030",
        "rtl-tg68k030-mmu2",
    )
    assert resolve_target_ids(["rtl", "musashi", "rtl"]) == (
        "rtl-tg68k030-mmu2",
        "musashi-68030",
    )
