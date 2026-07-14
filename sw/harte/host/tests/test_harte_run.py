import struct
import json

import pytest

from sw.harte.host import harte_run
from sw.harte.host.harte_case import build_case
from sw.harte.host.harte_run import (
    compare_result,
    corpus_manifest,
    decode_result,
    encode_run,
)
from sw.harte.host.tests.test_harte_case import raw_case


def result_payload(case, ccr=None):
    payload = b"".join(struct.pack(">I", value) for value in case.fd + case.fa)
    return payload + bytes((case.fccr if ccr is None else ccr,))


def test_encode_run_uses_instruction_byte_length():
    case = build_case(raw_case())
    encoded = encode_run(case)
    assert len(encoded) == 64
    assert encoded[-3:] == b"\x02\x4e\x71"


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
    assert any(item.startswith("d0=") for item in mismatch)
    assert any(item.startswith("sr=") for item in mismatch)


def test_decode_rejects_wrong_payload_size():
    with pytest.raises(ValueError):
        decode_result(b"\0" * 60)


def test_corpus_manifest_records_revision_and_content(tmp_path):
    (tmp_path / ".astra-harte-revision").write_text("abc123\n")
    vector = tmp_path / "NOP.json.gz"
    vector.write_bytes(b"test corpus bytes")
    manifest = corpus_manifest([str(vector)])
    assert manifest["revision"] == "abc123"
    assert manifest["files"][0]["bytes"] == 17
    assert len(manifest["sha256"]) == 64


def test_host_manifest_hashes_every_behavioral_source():
    manifest = harte_run.host_manifest()
    assert [harte_run.Path(item["path"]).name for item in manifest["files"]] == list(
        harte_run.HOST_SOURCE_NAMES
    )
    assert len(manifest["sha256"]) == 64


def info_response(protocol=2, features=3, build_id=0x12345678):
    payload = bytes((protocol, 0, 30, features))
    payload += build_id.to_bytes(4, "big")
    payload += (460800).to_bytes(4, "big")
    return harte_run.CMD_INFOR, payload


def test_preflight_requires_exact_protocol_features_and_build(monkeypatch):
    monkeypatch.setattr(harte_run, "transact", lambda *args: info_response())
    info = harte_run.preflight(object(), 0.1, 0x12345678, False)
    assert info.build_id == 0x12345678

    with pytest.raises(RuntimeError, match="BUILD_ID"):
        harte_run.preflight(object(), 0.1, 0xDEADBEEF, False)

    monkeypatch.setattr(
        harte_run, "transact", lambda *args: info_response(protocol=1)
    )
    with pytest.raises(RuntimeError, match="unsupported device protocol"):
        harte_run.preflight(object(), 0.1, 0x12345678, False)

    monkeypatch.setattr(
        harte_run, "transact", lambda *args: info_response(features=1)
    )
    with pytest.raises(RuntimeError, match="lacks required"):
        harte_run.preflight(object(), 0.1, 0x12345678, False)


def test_preflight_rejects_device_without_expected_build(monkeypatch):
    monkeypatch.setattr(harte_run, "transact", lambda *args: info_response())
    with pytest.raises(RuntimeError, match="no expected BUILD_ID"):
        harte_run.preflight(object(), 0.1, None, False)


def test_run_aggregates_failures_without_losing_per_file_examples(tmp_path, monkeypatch):
    vector = tmp_path / "ASL.b.json"
    vector.write_text(json.dumps([raw_case(0xE502)]))
    paths = [str(vector)]
    scope = harte_run.scan_scope(paths)
    case = build_case(raw_case(0xE502))
    mismatching = bytearray(result_payload(case))
    mismatching[11] ^= 1

    class FakeTarget:
        @staticmethod
        def manifest():
            return {"kind": "test", "implementation": "fake"}

        @staticmethod
        def execute(*_args):
            return bytes(mismatching), None

    report_path = tmp_path / "report.json"
    result = harte_run.run(
        paths, FakeTarget(), scope, {"sha256": "corpus"},
        0, None, 0, 0.1, 0.0, 1, 1, report_path,
    )
    report = json.loads(report_path.read_text())
    summary = report["run"]["failure_summary"]
    assert result == 1
    assert summary["by_file"] == {"ASL.b.json": 1}
    assert summary["by_opcode"] == {"e502": 1}
    assert summary["components"] == {"register": 1}
    assert summary["examples_by_file"]["ASL.b.json"][0]["name"] == case.name
