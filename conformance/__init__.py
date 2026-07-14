"""Backend-neutral Astra68 CPU and PMMU conformance support."""

from .model import (
    CPU_REGISTERS,
    ConformanceCase,
    ExecutionResult,
    MemoryRange,
    MemorySegment,
    RunConfig,
    compare_result,
    load_case,
)

__all__ = [
    "CPU_REGISTERS",
    "ConformanceCase",
    "ExecutionResult",
    "MemoryRange",
    "MemorySegment",
    "RunConfig",
    "compare_result",
    "load_case",
]
