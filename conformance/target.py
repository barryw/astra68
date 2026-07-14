"""Target interface shared by Musashi, RTL simulation, and FPGA adapters."""

from __future__ import annotations

from typing import Mapping, Protocol

from .model import ConformanceCase, ExecutionResult


class TargetError(RuntimeError):
    """The target could not execute or return a trustworthy result."""


class ConformanceTarget(Protocol):
    def manifest(self) -> Mapping[str, object]:
        """Return an immutable identity for the implementation being tested."""

    def execute(self, case: ConformanceCase) -> ExecutionResult:
        """Execute one normalized case and return observations only."""

    def close(self) -> None:
        """Release target resources."""
