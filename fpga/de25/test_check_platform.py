#!/usr/bin/env python3
"""Regression check for the DE25 build-host qualification gate."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


CHECK = Path(__file__).with_name("check_platform.py")


with tempfile.TemporaryDirectory() as temporary_text:
    temporary = Path(temporary_text)
    quartus = temporary / "quartus"
    bin_dir = quartus / "bin"
    bin_dir.mkdir(parents=True)
    (bin_dir / "quartus_sh").write_text(
        "#!/bin/sh\necho 'Version TEST Build 1'\n", encoding="utf-8")
    (bin_dir / "jtagconfig").write_text(
        "#!/bin/sh\nprintf '%s\\n' '1) DE25-Nano [test]' "
        "'  4BA06477 ARM_CORESIGHT_SOC_600' '  4362C0DD A5E(test)'\n",
        encoding="utf-8",
    )
    (bin_dir / "quartus_sh").chmod(0o755)
    (bin_dir / "jtagconfig").chmod(0o755)

    archive = temporary / "vendor.qar"
    archive.write_bytes(b"known vendor archive")
    resource = temporary / "resource.zip"
    resource.write_bytes(b"known resource package")
    license_file = temporary / "quartus2_lic.dat"
    license_file.write_text("FEATURE quartus_agilex5e test\n", encoding="ascii")
    restored = temporary / "restored"
    (restored / "output_files").mkdir(parents=True)
    qsf = restored / "golden_top.qsf"
    qsf.write_bytes(b"known qsf")
    hps_hex = restored / "output_files" / "u-boot-spl-dtb.hex"
    hps_hex.write_bytes(b"known HPS boot hex")
    usb = temporary / "usb" / "9-2"
    usb.mkdir(parents=True)
    for name, value in {
        "idVendor": "09fb\n",
        "idProduct": "6026\n",
        "product": "DE25-Nano\n",
        "serial": "TESTSERIAL\n",
    }.items():
        (usb / name).write_text(value, encoding="ascii")

    manifest = temporary / "platform.json"
    manifest.write_text(json.dumps({
        "board": "DE25-Nano",
        "usb_id": "09fb:6026",
        "usb_serial": "TESTSERIAL",
        "jtag_ids": ["4BA06477", "4362C0DD"],
        "device": "A5EB013BB23BE4SCS",
        "quartus_version": "Version TEST Build 1",
        "quartus_license_feature": "quartus_agilex5e",
        "ghrd_archive": archive.name,
        "ghrd_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        "ghrd_qsf": "golden_top.qsf",
        "ghrd_qsf_sha256": hashlib.sha256(qsf.read_bytes()).hexdigest(),
        "ghrd_hps_hex": "output_files/u-boot-spl-dtb.hex",
        "ghrd_hps_hex_sha256": hashlib.sha256(hps_hex.read_bytes()).hexdigest(),
        "resource_archive": resource.name,
        "resource_sha256": hashlib.sha256(resource.read_bytes()).hexdigest(),
        "lpddr4b_ip": "Demonstration/FPGA/test.ip",
        "lpddr4b_ip_sha256": "1" * 64,
        "lpddr4b_calibration": "Demonstration/FPGA/calibration.sv",
        "lpddr4b_calibration_sha256": "2" * 64,
        "lpddr4b_reference_qsf": "Demonstration/FPGA/golden_top.qsf",
        "lpddr4b_reference_qsf_sha256": "3" * 64,
    }), encoding="utf-8")

    command = [
        sys.executable, str(CHECK),
        "--manifest", str(manifest),
        "--quartus-root", str(quartus),
        "--ghrd", str(archive),
        "--resource", str(resource),
        "--license", str(license_file),
        "--restored", str(restored),
        "--usb-root", str(temporary / "usb"),
    ]
    passed = subprocess.run(command, text=True, capture_output=True)
    assert passed.returncode == 0, passed.stderr
    assert "DE25 platform qualification: PASS" in passed.stdout
    assert "device=A5EB013BB23BE4SCS" in passed.stdout

    archive.write_bytes(b"stale vendor archive")
    rejected = subprocess.run(command, text=True, capture_output=True)
    assert rejected.returncode != 0
    assert "GHRD SHA-256 mismatch" in rejected.stderr

    archive.write_bytes(b"known vendor archive")
    resource.write_bytes(b"stale resource package")
    rejected = subprocess.run(command, text=True, capture_output=True)
    assert rejected.returncode != 0
    assert "resource package SHA-256 mismatch" in rejected.stderr

print("DE25 platform qualification tests: PASS")
