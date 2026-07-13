import struct

import pytest

from harte_case import build_case
from harte_run import compare_result, decode_result, encode_run
from test_harte_case import raw_case


def result_payload(case, ccr=None):
    payload = b"".join(struct.pack(">I", value) for value in case.fd + case.fa)
    return payload + bytes((case.fccr if ccr is None else ccr,))


def test_encode_run_uses_instruction_byte_length():
    case = build_case(raw_case())
    encoded = encode_run(case)
    assert encoded[0] == 0x55
    assert encoded[-4:-1] == b"\x02\x4e\x71"


def test_decode_and_compare_matching_result():
    case = build_case(raw_case())
    d, a, ccr = decode_result(result_payload(case))
    assert d == case.fd
    assert a == case.fa
    assert ccr == case.fccr
    assert compare_result(case, result_payload(case)) == []


def test_compare_reports_register_and_ccr_mismatches():
    case = build_case(raw_case())
    payload = bytearray(result_payload(case, ccr=1))
    payload[3] ^= 1
    mismatch = compare_result(case, bytes(payload))
    assert any(item.startswith("D0=") for item in mismatch)
    assert any(item.startswith("CCR=") for item in mismatch)


def test_decode_rejects_wrong_payload_size():
    with pytest.raises(ValueError):
        decode_result(b"\0" * 60)
