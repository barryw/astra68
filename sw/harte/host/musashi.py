"""Convert admitted Harte vectors into shared architectural conformance cases."""

from __future__ import annotations

from pathlib import Path
from conformance.model import (
    ConformanceCase,
    MaskedValue,
    MemorySegment,
    RunConfig,
)
from conformance.target import TargetError
from conformance.targets import create_target

try:
    from .flags import mask_for_opcode
except ImportError:  # Direct script execution from sw/harte/host.
    from flags import mask_for_opcode


def to_conformance_case(case) -> ConformanceCase:
    """Translate one admitted Harte case without adding a target-specific oracle."""
    initial_cpu = {
        **{f"d{index}": value for index, value in enumerate(case.d)},
        **{f"a{index}": value for index, value in enumerate(case.a)},
        "pc": 0x00001000,
        # The FPGA harness writes only CCR and remains in supervisor mode.
        "sr": 0x00002000 | case.ccr,
    }
    expected_cpu = {
        **{
            f"d{index}": MaskedValue(value)
            for index, value in enumerate(case.fd)
        },
        **{
            f"a{index}": MaskedValue(value)
            for index, value in enumerate(case.fa)
        },
        "sr": MaskedValue(case.fccr, mask_for_opcode(case.opcode)),
    }
    return ConformanceCase(
        case_id=f"harte/{case.name}",
        description="Harte register-only instruction adapted at runtime",
        authority=("Motorola MC68030 architecture",),
        derived_from=(f"SingleStepTests/m68000:{case.name}",),
        requires=("mc68030", "single-instruction"),
        reset=True,
        initial_cpu=initial_cpu,
        memory=(MemorySegment(0x00001000, case.instr),),
        run=RunConfig(mode="instruction", max_cycles=100),
        observe=(),
        expected_cpu=expected_cpu,
        expected_memory=(),
        expected_terminal="instruction",
    )


class ConformanceHarteTarget:
    """Harte execution target backed by a common conformance adapter."""

    def __init__(self, target):
        self.target = target

    def __enter__(self) -> "ConformanceHarteTarget":
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.close()

    def close(self) -> None:
        self.target.close()

    def manifest(self) -> dict:
        manifest = dict(self.target.manifest())
        manifest["suite_adapter"] = "harte-register-v1"
        return manifest

    def execute(self, case, _retries: int, _timeout: float,
                _pace_seconds: float):
        try:
            result = self.target.execute(to_conformance_case(case))
        except TargetError as exc:
            return None, str(exc)
        return result, None


class RegisteredHarteTarget(ConformanceHarteTarget):
    """Harte suite adapter around a canonical conformance target ID."""

    def __init__(self, target_id: str, *, worker: str | Path | None = None,
                 simulator: str | Path | None = None):
        super().__init__(create_target(
            target_id,
            worker=worker,
            simulator=simulator,
        ))


class MusashiHarteTarget(ConformanceHarteTarget):
    """Harte execution target backed by vendored Musashi."""

    def __init__(self, worker: str | Path | None = None,
                 cpu_model: str = "68030"):
        target_id = f"musashi-{cpu_model}"
        super().__init__(
            create_target(target_id, worker=worker)
        )


class RtlHarteTarget(ConformanceHarteTarget):
    """Harte execution target backed by the TG68K.C GHDL simulator."""

    def __init__(self, simulator: str | Path | None = None):
        super().__init__(create_target(
            "rtl-tg68k030-mmu2", simulator=simulator
        ))
