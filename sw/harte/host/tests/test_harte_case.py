import gzip
import json

import pytest

from harte_case import CaseError, build_case, is_in_scope, load, scope_reason


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


def test_trace_and_memory_forms_are_rejected():
    assert scope_reason(raw_case(sr=0x8000)) == "trace enabled"
    assert scope_reason(raw_case(0xD050)) == "memory or deferred ALU form"  # ADD.W (A0),D0


def test_a7_operand_is_rejected():
    assert scope_reason(raw_case(0x200F)) == "A7 operand"  # MOVE.L A7,D0


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


def test_is_in_scope_matches_reason():
    assert is_in_scope(raw_case())
    assert not is_in_scope(raw_case(0x4E75))
