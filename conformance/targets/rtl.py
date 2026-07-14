"""GHDL target adapter for Astra's TG68K.C 68030/PMMU RTL."""

from __future__ import annotations

import hashlib
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory

from ..model import (
    CPU_REGISTERS,
    SCHEMA_VERSION,
    ConformanceCase,
    ExecutionResult,
    MemorySegment,
)
from ..target import TargetError


MODE_VALUES = {"instruction": 1, "cycles": 2, "memory": 3}
TERMINAL_VALUES = {1: "instruction", 2: "cycle-limit", 3: "memory"}

REPOSITORY = Path(__file__).resolve().parents[2]
DEFAULT_SIMULATOR = REPOSITORY / "conformance/rtl/build/tb_astra_conformance"
SOURCE_PATHS = (
    "conformance/model.py",
    "conformance/target.py",
    "conformance/targets/rtl.py",
    "conformance/rtl/Makefile",
    "conformance/rtl/tb_astra_conformance.vhd",
    "fpga/cpu/tg68k_c_030_mmu2/TG68K_Pack.vhd",
    "fpga/cpu/tg68k_c_030_mmu2/TG68K_ALU.vhd",
    "fpga/cpu/tg68k_c_030_mmu2/TG68K_PMMU_030.vhd",
    "fpga/cpu/tg68k_c_030_mmu2/TG68K_Cache_030.vhd",
    "fpga/cpu/tg68k_c_030_mmu2/TG68K_CacheCtrl_030.vhd",
    "fpga/cpu/tg68k_c_030_mmu2/TG68KdotC_Kernel.vhd",
)

BOOTSTRAP_CANDIDATES = (
    0x01FE0000,
    0x00FE0000,
    0x007E0000,
    0x003E0000,
    0x001E0000,
)
BOOTSTRAP_RESERVATION = 0x1000

CONTROL_REGISTERS = {
    "sfc": 0x000,
    "dfc": 0x001,
    "cacr": 0x002,
    "vbr": 0x801,
    "caar": 0x802,
    "msp": 0x803,
    "isp": 0x804,
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _source_manifest() -> dict:
    digest = hashlib.sha256()
    files = []
    for relative in SOURCE_PATHS:
        path = REPOSITORY / relative
        content_hash = _sha256(path)
        digest.update(relative.encode("utf-8") + b"\0" + bytes.fromhex(content_hash))
        files.append({"path": relative, "sha256": content_hash})
    return {"sha256": digest.hexdigest(), "files": files}


def _append_word(code: bytearray, value: int) -> None:
    code.extend((value & 0xFFFF).to_bytes(2, "big"))


def _append_long(code: bytearray, value: int) -> None:
    code.extend((value & 0xFFFFFFFF).to_bytes(4, "big"))


def _move_immediate_to_data(code: bytearray, register: int, value: int) -> None:
    _append_word(code, 0x203C | (register << 9))
    _append_long(code, value)


def _move_immediate_to_address(code: bytearray, register: int, value: int) -> None:
    _append_word(code, 0x207C | (register << 9))
    _append_long(code, value)


def _move_data_to_control(code: bytearray, register: int, selector: int) -> None:
    _append_word(code, 0x4E7B)
    _append_word(code, (register << 12) | selector)


def _move_immediate_to_absolute(code: bytearray, value: int, address: int) -> None:
    _append_word(code, 0x23FC)
    _append_long(code, value)
    _append_long(code, address)


def _range_intersects(start: int, length: int, other_start: int,
                      other_length: int) -> bool:
    return start < other_start + other_length and other_start < start + length


def _read_memory_word(memory: dict[int, int], address: int) -> int:
    return int.from_bytes(
        bytes(memory.get((address + offset) & 0xFFFFFFFF, 0) for offset in range(4)),
        "big",
    )


def _select_bootstrap_address(case: ConformanceCase) -> int:
    occupied = [(segment.address, len(segment.data)) for segment in case.memory]
    occupied.extend((item.address, item.length) for item in case.observation_ranges())
    occupied.append((case.run.stop_address, 4))
    target_pc = case.initial_cpu.get("pc")
    if target_pc is not None:
        occupied.append((target_pc, 2))

    for candidate in BOOTSTRAP_CANDIDATES:
        if all(
            not _range_intersects(
                candidate, BOOTSTRAP_RESERVATION, address, length
            )
            for address, length in occupied
            if length
        ):
            return candidate
    raise TargetError("could not reserve an address for the RTL state bootstrap")


def _build_bootstrap(case: ConformanceCase, original_reset_sp: int,
                     original_reset_pc: int) -> bytes:
    initial = case.initial_cpu
    sr = initial.get("sr", 0x2700)
    if sr & 0xC000:
        raise TargetError(
            "RTL bootstrap cannot initialize SR trace bits without executing "
            "a bootstrap trace exception"
        )
    if sr > 0xFFFF:
        raise TargetError("RTL bootstrap SR value exceeds 16 bits")

    code = bytearray()

    # Put the fixture's reset vector back before the instruction under test runs.
    _move_immediate_to_absolute(code, original_reset_pc, 4)

    control_values = {
        "sfc": initial.get("sfc", 0),
        "dfc": initial.get("dfc", 0),
        "cacr": initial.get("cacr", 0),
        "vbr": initial.get("vbr", 0),
        "caar": initial.get("caar", 0),
        "msp": initial.get("msp", 0),
        "isp": initial.get("isp", original_reset_sp),
    }
    for name, selector in CONTROL_REGISTERS.items():
        _move_immediate_to_data(code, 7, control_values[name])
        _move_data_to_control(code, 7, selector)

    usp = initial.get("usp", 0)
    _move_immediate_to_address(code, 0, usp)
    _append_word(code, 0x4E60)  # MOVE A0,USP

    for register in range(8):
        _move_immediate_to_data(code, register, initial.get(f"d{register}", 0))
    for register in range(7):
        _move_immediate_to_address(code, register, initial.get(f"a{register}", 0))

    _append_word(code, 0x46FC)  # MOVE.W #imm,SR
    _append_word(code, sr)

    if sr & 0x2000:
        selected_stack = control_values["msp"] if sr & 0x1000 else control_values["isp"]
    else:
        selected_stack = usp
    _move_immediate_to_address(code, 7, initial.get("a7", selected_stack))

    target_pc = initial.get("pc", original_reset_pc)
    _append_word(code, 0x4EF9)  # JMP abs.l
    _append_long(code, target_pc)
    return bytes(code)


def _prepare_memory(case: ConformanceCase) -> tuple[dict[int, int], int]:
    if not case.reset:
        raise TargetError("RTL target currently requires initial.reset=true")

    memory: dict[int, int] = {}
    for segment in case.memory:
        for offset, value in enumerate(segment.data):
            memory[segment.address + offset] = value

    original_reset_sp = _read_memory_word(memory, 0)
    original_reset_pc = _read_memory_word(memory, 4)
    target_pc = case.initial_cpu.get("pc", original_reset_pc)
    bootstrap_address = _select_bootstrap_address(case)
    bootstrap = _build_bootstrap(case, original_reset_sp, original_reset_pc)
    if len(bootstrap) > BOOTSTRAP_RESERVATION:
        raise TargetError("RTL state bootstrap exceeds its reserved memory range")

    for offset, value in enumerate(bootstrap):
        memory[bootstrap_address + offset] = value
    for offset, value in enumerate(bootstrap_address.to_bytes(4, "big")):
        memory[4 + offset] = value
    return memory, target_pc


def _write_memory(path: Path, memory: dict[int, int]) -> None:
    with path.open("w", encoding="ascii") as stream:
        for address, value in sorted(memory.items()):
            stream.write(f"{address:08X} {value:02X}\n")


def _write_config(path: Path, case: ConformanceCase, target_pc: int) -> None:
    observations = case.observation_ranges()
    with path.open("w", encoding="ascii") as stream:
        stream.write(f"{MODE_VALUES[case.run.mode]}\n")
        stream.write(f"{case.run.max_cycles}\n")
        stream.write(f"{target_pc:08X}\n")
        stream.write(
            f"{case.run.stop_address:08X} {case.run.stop_mask:08X} "
            f"{case.run.stop_value:08X}\n"
        )
        stream.write(f"{len(observations)}\n")
        for observation in observations:
            stream.write(f"{observation.address:08X} {observation.length}\n")


def _parse_result(path: Path, expected_observations: int) -> ExecutionResult:
    try:
        lines = path.read_text(encoding="ascii").splitlines()
    except OSError as exc:
        raise TargetError(f"RTL simulator did not produce a result: {exc}") from exc
    if len(lines) < 3:
        raise TargetError("RTL simulator result is truncated")

    header = lines[0].split()
    if len(header) != 2:
        raise TargetError("RTL simulator result header is malformed")
    try:
        terminal_value, cycles = (int(value, 10) for value in header)
    except ValueError as exc:
        raise TargetError("RTL simulator result header is not numeric") from exc
    if terminal_value not in TERMINAL_VALUES or cycles < 0:
        raise TargetError("RTL simulator returned an invalid terminal or cycle count")

    register_values = lines[1].split()
    if len(register_values) != len(CPU_REGISTERS):
        raise TargetError(
            f"RTL simulator returned {len(register_values)} CPU registers; "
            f"expected {len(CPU_REGISTERS)}"
        )
    try:
        cpu = {
            name: int(value, 16)
            for name, value in zip(CPU_REGISTERS, register_values)
        }
        observation_count = int(lines[2], 10)
    except ValueError as exc:
        raise TargetError("RTL simulator result contains an invalid number") from exc
    if observation_count != expected_observations:
        raise TargetError(
            f"RTL simulator returned {observation_count} observations; "
            f"expected {expected_observations}"
        )
    if len(lines) != 3 + observation_count:
        raise TargetError("RTL simulator result observation count is inconsistent")

    observations = []
    for line in lines[3:]:
        fields = line.split()
        if len(fields) != 3:
            raise TargetError("RTL simulator observation is malformed")
        try:
            address = int(fields[0], 16)
            length = int(fields[1], 10)
            data = bytes.fromhex(fields[2])
        except ValueError as exc:
            raise TargetError("RTL simulator observation contains invalid data") from exc
        if length != len(data):
            raise TargetError("RTL simulator observation length does not match its data")
        observations.append(MemorySegment(address, data))

    return ExecutionResult(
        terminal=TERMINAL_VALUES[terminal_value],
        cycles=cycles,
        cpu=cpu,
        memory=tuple(observations),
    )


class RtlTarget:
    """Execute normalized cases in a freshly elaborated TG68K.C simulation."""

    def __init__(self, simulator: str | Path = DEFAULT_SIMULATOR,
                 timeout: float = 120.0):
        self.simulator = Path(simulator).resolve()
        self.timeout = timeout
        if not self.simulator.is_file():
            raise TargetError(
                f"RTL conformance simulator is missing: {self.simulator}; build it "
                "with `rtk make -C conformance/rtl`"
            )
        self._manifest = {
            "target_id": "rtl-tg68k030-mmu2",
            "kind": "rtl-simulation",
            "implementation": "TG68K.C 68030/PMMU (GHDL)",
            "architecture": ["mc68030", "pmmu"],
            "features": ["format-a-frame", "format-b-frame"],
            "protocol": 1,
            "case_schema": SCHEMA_VERSION,
            "capabilities": [
                "single-instruction",
                "sparse-memory",
                "memory-stop",
                "memory-observe",
                "no-fpu",
                "table-bus-fault",
            ],
            "source": _source_manifest(),
            "executable": {
                "path": str(self.simulator),
                "sha256": _sha256(self.simulator),
            },
        }

    def __enter__(self) -> "RtlTarget":
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.close()

    def close(self) -> None:
        pass

    def manifest(self) -> dict:
        return self._manifest

    def execute(self, case: ConformanceCase) -> ExecutionResult:
        memory, target_pc = _prepare_memory(case)
        observations = case.observation_ranges()
        with TemporaryDirectory(prefix="astra68-rtl-") as temporary:
            directory = Path(temporary)
            memory_path = directory / "memory.hex"
            config_path = directory / "config.txt"
            result_path = directory / "result.txt"
            _write_memory(memory_path, memory)
            _write_config(config_path, case, target_pc)
            command = [
                str(self.simulator),
                f"-gMEMORY_FILE={memory_path}",
                f"-gCONFIG_FILE={config_path}",
                f"-gRESULT_FILE={result_path}",
                "--assert-level=error",
                "--ieee-asserts=disable",
            ]
            try:
                completed = subprocess.run(
                    command,
                    check=False,
                    capture_output=True,
                    text=True,
                    timeout=self.timeout,
                )
            except (OSError, subprocess.TimeoutExpired) as exc:
                raise TargetError(f"RTL simulator execution failed: {exc}") from exc
            if completed.returncode != 0:
                detail = "\n".join(
                    item.strip()
                    for item in (completed.stdout, completed.stderr)
                    if item.strip()
                )
                raise TargetError(
                    f"RTL simulator exited with {completed.returncode}"
                    + (f": {detail}" if detail else "")
                )
            return _parse_result(result_path, len(observations))
