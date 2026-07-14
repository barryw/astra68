"""Persistent target adapter for Astra's vendored Musashi 68030/PMMU."""

from __future__ import annotations

import hashlib
from pathlib import Path
import struct
import subprocess
from typing import BinaryIO

from ..model import (
    CPU_REGISTERS,
    SCHEMA_VERSION,
    ConformanceCase,
    ExecutionResult,
    MemorySegment,
)
from ..target import TargetError


PROTOCOL_VERSION = 1
REQUEST_MAGIC = b"A68Q"
RESPONSE_MAGIC = b"A68R"
COMMAND_HELLO = 1
COMMAND_RUN = 2
STATUS_OK = 0

MODE_VALUES = {"instruction": 1, "cycles": 2, "memory": 3}
TERMINAL_VALUES = {1: "instruction", 2: "cycle-limit", 3: "memory"}

CAPABILITIES = {
    1 << 0: "single-instruction",
    1 << 1: "sparse-memory",
    1 << 2: "memory-stop",
    1 << 3: "memory-observe",
    1 << 4: "no-fpu",
    1 << 5: "table-bus-fault",
}

CPU_MODELS = {
    "68000": {"architecture": ["mc68000"], "features": []},
    "68030": {
        "architecture": ["mc68030", "pmmu"],
        "features": ["format-a-frame", "format-b-frame"],
    },
}

REPOSITORY = Path(__file__).resolve().parents[2]
DEFAULT_WORKER = (
    REPOSITORY / "third_party/musashi/astra/build/astra_conformance_target"
)
SOURCE_PATHS = (
    "conformance/model.py",
    "conformance/target.py",
    "conformance/targets/musashi.py",
    "conformance/targets/musashi_worker.c",
    "third_party/musashi/Makefile",
    "third_party/musashi/m68k.h",
    "third_party/musashi/m68kconf.h",
    "third_party/musashi/m68kcpu.c",
    "third_party/musashi/m68kcpu.h",
    "third_party/musashi/m68k_in.c",
    "third_party/musashi/m68kfpu.c",
    "third_party/musashi/m68kmmu.h",
    "third_party/musashi/astra/pmmu030.c",
    "third_party/musashi/astra/pmmu030.h",
    "third_party/musashi/softfloat/softfloat.c",
    "third_party/musashi/softfloat/softfloat.h",
)


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


def _read_exact(stream: BinaryIO, length: int) -> bytes:
    content = bytearray()
    while len(content) < length:
        chunk = stream.read(length - len(content))
        if not chunk:
            raise TargetError(
                f"Musashi target closed its output after {len(content)} of {length} bytes"
            )
        content.extend(chunk)
    return bytes(content)


class MusashiTarget:
    """Execute normalized cases against one persistent Musashi process."""

    def __init__(self, worker: str | Path = DEFAULT_WORKER,
                 cpu_model: str = "68030"):
        if cpu_model not in CPU_MODELS:
            raise TargetError(
                f"unsupported Musashi CPU model {cpu_model!r}; "
                f"expected one of {', '.join(CPU_MODELS)}"
            )
        self.cpu_model = cpu_model
        self.worker = Path(worker).resolve()
        if not self.worker.is_file():
            raise TargetError(
                f"Musashi target is missing: {self.worker}; build it with "
                "`rtk make -C third_party/musashi conformance-target`"
            )
        self.process = subprocess.Popen(
            [str(self.worker), "--cpu", cpu_model],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if self.process.stdin is None or self.process.stdout is None:
            self.process.kill()
            raise TargetError("could not create Musashi target pipes")
        self._name, self._capability_bits = self._hello()
        missing = [name for bit, name in CAPABILITIES.items() if not self._capability_bits & bit]
        if missing:
            self.close()
            raise TargetError(
                "Musashi target lacks required capabilities: " + ", ".join(missing)
            )
        self._manifest = {
            "target_id": f"musashi-{cpu_model}",
            "kind": "emulator",
            "implementation": self._name,
            **CPU_MODELS[cpu_model],
            "protocol": PROTOCOL_VERSION,
            "case_schema": SCHEMA_VERSION,
            "capabilities": [
                name for bit, name in CAPABILITIES.items() if self._capability_bits & bit
            ],
            "source": _source_manifest(),
            "executable": {
                "path": str(self.worker),
                "sha256": _sha256(self.worker),
            },
        }

    def __enter__(self) -> "MusashiTarget":
        return self

    def __exit__(self, _exc_type, _exc, _traceback) -> None:
        self.close()

    def close(self) -> None:
        process = getattr(self, "process", None)
        if process is None:
            return
        self.process = None
        if process.stdin is not None:
            process.stdin.close()
        try:
            process.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    def manifest(self) -> dict:
        return self._manifest

    def _request(self, command: int, payload: bytes = b"") -> bytes:
        if self.process is None or self.process.stdin is None or self.process.stdout is None:
            raise TargetError("Musashi target is closed")
        if self.process.poll() is not None:
            detail = ""
            if self.process.stderr is not None:
                detail = self.process.stderr.read().decode("utf-8", errors="replace").strip()
            raise TargetError(
                f"Musashi target exited with {self.process.returncode}"
                + (f": {detail}" if detail else "")
            )
        header = struct.pack(">4sHHI", REQUEST_MAGIC, PROTOCOL_VERSION, command, len(payload))
        try:
            self.process.stdin.write(header + payload)
            self.process.stdin.flush()
            response_header = _read_exact(self.process.stdout, 12)
            magic, version, status, length = struct.unpack(">4sHHI", response_header)
            response = _read_exact(self.process.stdout, length)
        except (BrokenPipeError, OSError) as exc:
            raise TargetError(f"Musashi target transport failed: {exc}") from exc
        if magic != RESPONSE_MAGIC or version != PROTOCOL_VERSION:
            raise TargetError("Musashi target returned an invalid response header")
        if status != STATUS_OK:
            detail = response.decode("utf-8", errors="replace")
            raise TargetError(f"Musashi target rejected request (status {status}): {detail}")
        return response

    def _hello(self) -> tuple[str, int]:
        response = self._request(COMMAND_HELLO)
        if len(response) < 8:
            raise TargetError("Musashi HELLO response is truncated")
        schema, capabilities, name_length = struct.unpack_from(">HIH", response)
        if schema != SCHEMA_VERSION or len(response) != 8 + name_length:
            raise TargetError("Musashi HELLO response has incompatible schema or length")
        try:
            name = response[8:].decode("utf-8")
        except UnicodeDecodeError as exc:
            raise TargetError("Musashi HELLO target name is not UTF-8") from exc
        return name, capabilities

    def execute(self, case: ConformanceCase) -> ExecutionResult:
        cpu_mask = 0
        cpu_values = []
        for index, name in enumerate(CPU_REGISTERS):
            if name in case.initial_cpu:
                cpu_mask |= 1 << index
            cpu_values.append(case.initial_cpu.get(name, 0) & 0xFFFFFFFF)

        observations = case.observation_ranges()
        payload = bytearray()
        flags = 1 if case.reset else 0
        payload.extend(struct.pack(">HH", SCHEMA_VERSION, flags))
        payload.extend(
            struct.pack(
                ">IIIIII",
                MODE_VALUES[case.run.mode],
                case.run.max_cycles,
                case.run.stop_address,
                case.run.stop_mask,
                case.run.stop_value,
                cpu_mask,
            )
        )
        payload.extend(struct.pack(">" + "I" * len(cpu_values), *cpu_values))
        payload.extend(struct.pack(">II", len(case.memory), len(observations)))
        for segment in case.memory:
            payload.extend(struct.pack(">II", segment.address, len(segment.data)))
            payload.extend(segment.data)
        for observation in observations:
            payload.extend(struct.pack(">II", observation.address, observation.length))

        response = self._request(COMMAND_RUN, bytes(payload))
        minimum = 8 + 4 * len(CPU_REGISTERS) + 4
        if len(response) < minimum:
            raise TargetError("Musashi RUN response is truncated")
        schema, terminal_value, cycles = struct.unpack_from(">HHI", response)
        if schema != SCHEMA_VERSION or terminal_value not in TERMINAL_VALUES:
            raise TargetError("Musashi RUN response has incompatible schema or terminal")
        offset = 8
        cpu_raw = struct.unpack_from(">" + "I" * len(CPU_REGISTERS), response, offset)
        cpu = dict(zip(CPU_REGISTERS, cpu_raw))
        offset += 4 * len(CPU_REGISTERS)
        observation_count = struct.unpack_from(">I", response, offset)[0]
        offset += 4
        memory = []
        for _ in range(observation_count):
            if len(response) - offset < 8:
                raise TargetError("Musashi observation header is truncated")
            address, length = struct.unpack_from(">II", response, offset)
            offset += 8
            if length > len(response) - offset:
                raise TargetError("Musashi observation data is truncated")
            memory.append(MemorySegment(address, response[offset:offset + length]))
            offset += length
        if offset != len(response):
            raise TargetError("Musashi RUN response contains trailing data")
        if observation_count != len(observations):
            raise TargetError(
                f"Musashi returned {observation_count} observations; "
                f"expected {len(observations)}"
            )
        return ExecutionResult(
            terminal=TERMINAL_VALUES[terminal_value],
            cycles=cycles,
            cpu=cpu,
            memory=tuple(memory),
        )
