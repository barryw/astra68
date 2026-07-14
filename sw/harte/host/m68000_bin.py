"""Read SingleStepTests/m68000 ``.json.bin`` vector files.

The format and field mapping follow the MIT-licensed upstream ``decode.py`` at
revision 64b253116a3de04aaac4346c43680960dc9b67e5. Reading the binary source
directly avoids generating and then hashing a much larger derived JSON corpus.
"""

from __future__ import annotations

from pathlib import Path
import struct
from typing import Iterator


FILE_MAGIC = 0x1A3F5D71
TEST_MAGIC = 0xABC12367
NAME_MAGIC = 0x89ABCDEF
STATE_MAGIC = 0x01234567
TRANSACTION_MAGIC = 0x456789AB

REGISTER_ORDER = (
    "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7",
    "a0", "a1", "a2", "a3", "a4", "a5", "a6", "usp",
    "ssp", "sr", "pc",
)

TRANSACTION_KINDS = {
    1: "w",
    2: "r",
    3: "t",
    4: "re",
    5: "we",
}


class M68000BinaryError(ValueError):
    """The maintained m68000 vector file is truncated or malformed."""


class _Reader:
    def __init__(self, content: bytes, source: Path):
        self.content = content
        self.source = source
        self.offset = 0

    def unpack(self, format_string: str):
        size = struct.calcsize(format_string)
        if self.offset + size > len(self.content):
            raise M68000BinaryError(
                f"{self.source}: truncated at byte {self.offset}"
            )
        values = struct.unpack_from(format_string, self.content, self.offset)
        self.offset += size
        return values

    def take(self, count: int) -> bytes:
        if self.offset + count > len(self.content):
            raise M68000BinaryError(
                f"{self.source}: truncated at byte {self.offset}"
            )
        value = self.content[self.offset:self.offset + count]
        self.offset += count
        return value

    def section(self, expected_magic: int) -> None:
        _byte_count, magic = self.unpack("<II")
        if magic != expected_magic:
            raise M68000BinaryError(
                f"{self.source}: bad section magic 0x{magic:08x} "
                f"at byte {self.offset - 4}, expected 0x{expected_magic:08x}"
            )


def _read_name(reader: _Reader) -> str:
    reader.section(NAME_MAGIC)
    (length,) = reader.unpack("<I")
    try:
        return reader.take(length).decode("utf-8")
    except UnicodeDecodeError as exc:
        raise M68000BinaryError(
            f"{reader.source}: invalid UTF-8 test name"
        ) from exc


def _read_state(reader: _Reader) -> dict:
    reader.section(STATE_MAGIC)
    state = {name: reader.unpack("<I")[0] for name in REGISTER_ORDER}
    state["prefetch"] = list(reader.unpack("<II"))
    (ram_count,) = reader.unpack("<I")
    ram = []
    for _ in range(ram_count):
        address, data = reader.unpack("<IH")
        if address >= 0x01000000:
            raise M68000BinaryError(
                f"{reader.source}: 68000 RAM address 0x{address:08x} is out of range"
            )
        ram.append([address, data >> 8])
        ram.append([address | 1, data & 0xFF])
    state["ram"] = ram

    # This corpus records MAME's next-prefetch address (instruction address + 4).
    # Normalize it to the instruction address used by the older Harte JSON format.
    state["pc"] = (state["pc"] - 4) & 0xFFFFFFFF
    return state


def _read_transactions(reader: _Reader) -> tuple[list, int]:
    reader.section(TRANSACTION_MAGIC)
    cycle_count, transaction_count = reader.unpack("<II")
    transactions = []
    for _ in range(transaction_count):
        kind, cycles = reader.unpack("<BI")
        if kind == 0:
            transactions.append(["n", cycles])
            continue
        if kind not in TRANSACTION_KINDS:
            raise M68000BinaryError(
                f"{reader.source}: unknown transaction kind {kind}"
            )
        function_code, address, data, uds, lds = reader.unpack("<IIIII")
        transactions.append([
            TRANSACTION_KINDS[kind],
            cycles,
            function_code,
            address,
            ".w" if uds + lds == 2 else ".b",
            data,
            uds,
            lds,
        ])
    return transactions, cycle_count


def load_binary(path: str | Path) -> Iterator[dict]:
    """Yield normalized old-style dictionaries from a maintained binary corpus."""
    vector_path = Path(path)
    reader = _Reader(vector_path.read_bytes(), vector_path)
    magic, test_count = reader.unpack("<II")
    if magic != FILE_MAGIC:
        raise M68000BinaryError(
            f"{vector_path}: bad file magic 0x{magic:08x}, expected 0x{FILE_MAGIC:08x}"
        )

    for _ in range(test_count):
        reader.section(TEST_MAGIC)
        name = _read_name(reader)
        initial = _read_state(reader)
        final = _read_state(reader)
        transactions, cycle_count = _read_transactions(reader)
        yield {
            "name": name,
            "initial": initial,
            "final": final,
            "transactions": transactions,
            "length": cycle_count,
        }

    if reader.offset != len(reader.content):
        raise M68000BinaryError(
            f"{vector_path}: {len(reader.content) - reader.offset} trailing bytes"
        )
