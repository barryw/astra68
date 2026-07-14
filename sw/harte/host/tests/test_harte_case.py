import gzip
import json
import struct

import pytest

from sw.harte.host.harte_case import CaseError, build_case, is_in_scope, load, scope_reason
from sw.harte.host.m68000_bin import (
    FILE_MAGIC,
    NAME_MAGIC,
    REGISTER_ORDER,
    STATE_MAGIC,
    TEST_MAGIC,
    TRANSACTION_MAGIC,
    M68000BinaryError,
)


def raw_case(opcode=0x4E71, extension=b"", sr=0, final_pc=None):
    pc = 0x1000
    instr = opcode.to_bytes(2, "big") + extension
    if final_pc is None:
        final_pc = pc + len(instr)
    initial = {f"d{i}": i for i in range(8)}
    initial.update({f"a{i}": 0x100 + i for i in range(7)})
    initial.update({
        "usp": 0x2000,
        "ssp": 0x3000,
        "sr": sr,
        "pc": pc,
        "prefetch": [int.from_bytes(instr[:2], "big"),
                     int.from_bytes((instr[2:4] + b"\0\0")[:2], "big")],
        "ram": [[pc + i, value] for i, value in enumerate(instr[4:], 4)],
    })
    final = dict(initial)
    final["pc"] = final_pc
    final["prefetch"] = [0, 0]
    return {
        "name": f"{opcode:04x} test",
        "initial": initial,
        "final": final,
        "length": 4,
        "transactions": [],
    }


def test_cycle_length_is_not_used_as_instruction_length():
    raw = raw_case()
    raw["length"] = 4
    case = build_case(raw)
    assert case.instr == b"\x4e\x71"
    assert case.ilen == 2
    assert case.cycle_length == 4


def test_reconstructs_extension_bytes_after_prefetch():
    raw = raw_case(0x0680, b"\x12\x34\x56\x78")  # ADDI.L #$12345678,D0
    case = build_case(raw)
    assert case.instr == b"\x06\x80\x12\x34\x56\x78"
    assert case.ilen == 6


def test_trace_bits_do_not_change_register_only_data_results():
    assert scope_reason(raw_case(sr=0xC000)) is None


def test_memory_forms_are_rejected():
    assert scope_reason(raw_case(0xD050)) == "memory or deferred ALU form"  # ADD.W (A0),D0


def test_a7_operand_is_rejected():
    assert scope_reason(raw_case(0x200F)) == "A7 operand"  # MOVE.L A7,D0


@pytest.mark.parametrize("opcode", [
    0x4E71,  # NOP
    0x7000,  # MOVEQ #0,D0
    0x4840,  # SWAP D0
    0x4880,  # EXT.W D0
    0x0300,  # BTST D1,D0
    0x03C0,  # BSET D1,D0
    0x0800,  # BTST #imm,D0
    0x0880,  # BCLR #imm,D0
    0xE340,  # ASL.W #1,D0
    0x2001,  # MOVE.L D1,D0
    0x2240,  # MOVEA.L D0,A1
    0x0680,  # ADDI.L #imm,D0
    0x50C0,  # ST D0
    0x5280,  # ADDQ.L #1,D0
    0x5288,  # ADDQ.L #1,A0
    0x4280,  # CLR.L D0
    0xD041,  # ADD.W D1,D0
    0xD2C0,  # ADDA.W D0,A1
    0x91CA,  # SUBA.L A2,A0
    0xB5C3,  # CMPA.L D3,A2
    0xC0C1,  # MULU.W D1,D0
    0xC1C1,  # MULS.W D1,D0
    0xC149,  # EXG A0,A1
    0xC189,  # EXG D0,A1
])
def test_architecture_invariant_register_families_are_admitted(opcode):
    assert scope_reason(raw_case(opcode)) is None


@pytest.mark.parametrize(("opcode", "reason"), [
    (0xE1D0, "memory shift/rotate"),       # ASL.W (A0)
    (0x2010, "memory MOVE"),               # MOVE.L (A0),D0
    (0x0690, "memory or special immediate operation"),
    (0x51C8, "DBcc or memory Scc"),        # DBF D0
    (0x50D0, "DBcc or memory Scc"),        # ST (A0)
    (0x5290, "memory ADDQ/SUBQ"),          # ADDQ.L #1,(A0)
    (0x0310, "memory bit operation"),       # BTST D1,(A0)
    (0x40C0, "system-register or TAS operation"),  # MOVE SR,D0
    (0x42C0, "system-register or TAS operation"),  # MOVE CCR,D0
    (0x44C0, "system-register or TAS operation"),  # MOVE D0,CCR
    (0x46C0, "system-register or TAS operation"),  # MOVE D0,SR
    (0x4AC0, "system-register or TAS operation"),  # TAS D0
    (0x8100, "BCD flags are architecture-specific"),  # SBCD D0,D0
    (0x80C0, "DIV flags or exception not comparable"),  # DIVU.W D0,D0
    (0xDEC0, "A7 operand"),                # ADDA.W D0,A7
    (0xD0CF, "A7 operand"),                # ADDA.W A7,A0
    (0xC14F, "A7 operand"),                # EXG A0,A7
    (0x4E75, "unsupported opcode"),                # RTS owns A7/PC
])
def test_noncomparable_or_harness_owned_families_are_rejected(opcode, reason):
    assert scope_reason(raw_case(opcode)) == reason


def test_non_sequential_control_flow_is_rejected():
    assert scope_reason(raw_case(final_pc=0x2000)).startswith("non-sequential PC delta")


def test_changed_ram_is_rejected():
    raw = raw_case()
    raw["final"]["ram"] = [[0x1234, 0x56]]
    assert scope_reason(raw) == "RAM changed"


def test_build_case_fails_closed_for_unsupported_opcode():
    with pytest.raises(CaseError):
        build_case(raw_case(0x4E75))  # RTS uses harness-owned A7


def test_load_plain_and_gzip(tmp_path):
    vector = raw_case()
    plain = tmp_path / "vectors.json"
    compressed = tmp_path / "vectors.json.gz"
    plain.write_text(json.dumps([vector]))
    with gzip.open(compressed, "wt") as stream:
        json.dump([vector], stream)
    assert list(load(plain))[0]["name"] == vector["name"]
    assert list(load(compressed))[0]["name"] == vector["name"]


def binary_section(magic, payload):
    return struct.pack("<II", len(payload), magic) + payload


def binary_state(raw_pc):
    registers = {name: 0 for name in REGISTER_ORDER}
    registers.update({f"d{index}": index for index in range(8)})
    registers.update({f"a{index}": 0x100 + index for index in range(7)})
    registers.update({"ssp": 0x2000, "sr": 0x2700, "pc": raw_pc})
    payload = struct.pack(
        "<" + "I" * len(REGISTER_ORDER),
        *(registers[name] for name in REGISTER_ORDER),
    )
    payload += struct.pack("<II", 0x4E71, 0x1234)
    payload += struct.pack("<I", 1) + struct.pack("<IH", 0x1000, 0x4E71)
    return binary_section(STATE_MAGIC, payload)


def test_load_maintained_binary_normalizes_pc_and_word_ram(tmp_path):
    name = b"NOP maintained"
    encoded_name = binary_section(NAME_MAGIC, struct.pack("<I", len(name)) + name)
    transactions = binary_section(
        TRANSACTION_MAGIC,
        struct.pack("<II", 4, 1) + struct.pack("<BI", 0, 4),
    )
    test = binary_section(
        TEST_MAGIC,
        encoded_name + binary_state(0x1004) + binary_state(0x1006) + transactions,
    )
    path = tmp_path / "NOP.json.bin"
    path.write_bytes(struct.pack("<II", FILE_MAGIC, 1) + test)

    raw = list(load(path))[0]
    assert raw["initial"]["pc"] == 0x1000
    assert raw["final"]["pc"] == 0x1002
    assert raw["initial"]["ram"] == [[0x1000, 0x4E], [0x1001, 0x71]]
    assert build_case(raw).instr == b"\x4e\x71"


def test_load_maintained_binary_rejects_bad_magic(tmp_path):
    path = tmp_path / "bad.json.bin"
    path.write_bytes(struct.pack("<II", 0, 0))
    with pytest.raises(M68000BinaryError, match="bad file magic"):
        list(load(path))


def test_is_in_scope_matches_reason():
    assert is_in_scope(raw_case())
    assert not is_in_scope(raw_case(0x4E75))
