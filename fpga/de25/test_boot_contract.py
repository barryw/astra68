#!/usr/bin/env python3
"""Regression contract for the DE25 HPS-first Astra boot path."""

import json
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent

paths = [
    HERE / "boot.cmd",
    HERE / "boot.its",
    HERE / "build_boot_bundle.sh",
    HERE / "install_boot_bundle.sh",
    HERE / "program_hps_qspi.sh",
    HERE / "verify_running_shell.sh",
    HERE / "verify_running_shell.tcl",
]
for path in paths:
    assert path.is_file(), f"missing DE25 boot owner: {path.name}"

boot = (HERE / "boot.cmd").read_text(encoding="utf-8")
its = (HERE / "boot.its").read_text(encoding="utf-8")
assert 'images {\n        default = "script";' in its
ordered = (
    "fatload mmc 0:1 ${loadaddr} astra68.core.rbf",
    "fpga load 0 ${loadaddr} ${filesize}",
    "bridge enable",
    "fatload mmc 0:1 ${kernel_addr_r} Image",
    "fatload mmc 0:1 ${fdt_addr_r} socfpga_agilex5_de25_nano.dtb",
    "booti ${kernel_addr_r} - ${fdt_addr_r}",
)
positions = [boot.index(item) for item in ordered]
assert positions == sorted(positions), "fabric must precede bridges and Linux"
assert "if fpga load 0 ${loadaddr} ${filesize}; then" in boot
assert "if bridge enable; then" in boot
assert "&&" not in boot
assert "mem=" not in boot
assert "ASTRA BOOT HALTED" in boot
assert "astra.fabric=ready" in boot
assert "rootflags=commit=5" in boot
assert "commit=1" not in boot
assert "barrier=0" not in boot
assert "data=writeback" not in boot

build = (HERE / "build_boot_bundle.sh").read_text(encoding="utf-8")
for required in (
    "sha256sum -c BUILD_SHA256SUMS",
    "golden_top_boot.core.rbf",
    "astra68.hps.jic",
    "astra68.core.rbf",
    "program_hps_qspi.sh",
    "EXPECTED_DTB_SHA256",
    "SHA256SUMS",
):
    assert required in build, required
assert "quartus_pfg" not in build
assert "boot.scr.uimg" not in build

install = (HERE / "install_boot_bundle.sh").read_text(encoding="utf-8")
for required in (
    "sha256sum -c SHA256SUMS",
    "socfpga_agilex5_de25_nano.dtb",
    "astra68.core.rbf.astra-new",
    'mv "$mount/astra68.core.rbf.astra-new" "$mount/astra68.core.rbf"',
    'sha256sum "$mount/boot.scr.uimg"',
):
    assert required in install, required
assert '"$bundle/boot.scr.uimg"' not in install
assert "boot.scr.uimg.astra-new" not in install

program = (HERE / "program_hps_qspi.sh").read_text(encoding="utf-8")
for required in (
    "sha256sum -c SHA256SUMS",
    "jtagconfig",
    "4BA06477",
    "4362C0DD",
    "pvbi;",
    "astra68.hps.jic",
    "A5EB013BB23BE4SCS@2",
):
    assert required in program, required

verify = (HERE / "verify_running_shell.sh").read_text(encoding="utf-8")
for required in (
    "sha256sum -c BUILD_SHA256SUMS",
    "golden_top_hps.sof",
    "ASTRA_DE25_SOF",
    "system-console",
    "status == 2",
):
    assert required in verify, required

verify_tcl = (HERE / "verify_running_shell.tcl").read_text(encoding="utf-8")
for required in (
    "design_load",
    "design_instantiate",
    "design_link",
    "#DE25-Nano",
    "exit 2",
    "DE25 running shell: PASS",
):
    assert required in verify_tcl, required

platform = json.loads((HERE / "platform.json").read_text(encoding="utf-8"))
assert platform["boot_dtb_sha256"] == (
    "d9bd893fc94e45359fb442ca06741aa63c5881e9376473fb9397ebf6d6be4d13"
)

for path in paths[2:-1]:
    subprocess.run(["bash", "-n", path], check=True)

with tempfile.TemporaryDirectory(prefix="astra-fabric-install-") as temporary:
    root = Path(temporary)
    bundle, boot, commands = (root / name for name in ("bundle", "boot", "bin"))
    for directory in (bundle, boot, commands):
        directory.mkdir()
    shutil.copy2(HERE / "install_boot_bundle.sh", bundle)
    for name, data in {
        "astra68.core.rbf": b"new fabric",
        "boot.scr.uimg": b"generic vendor kernel selection",
        "EXPECTED_DTB_SHA256": hashlib.sha256(b"qualified dtb").hexdigest().encode() + b"\n",
    }.items():
        (bundle / name).write_bytes(data)
    selected = b"qualified storage kernel selection"
    (boot / "boot.scr.uimg").write_bytes(selected)
    (boot / "Image").write_bytes(b"selected kernel")
    (boot / "socfpga_agilex5_de25_nano.dtb").write_bytes(b"qualified dtb")
    for name, body in {
        "id": "printf '0\\n'",
        "mountpoint": "exit 0",
        "findmnt": "printf '/dev/astra-test\\n'",
        "sync": "exit 0",
    }.items():
        command = commands / name
        command.write_text("#!/bin/sh\n" + body + "\n")
        command.chmod(0o755)
    environment = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ["PATH"])
    command = ["bash", str(bundle / "install_boot_bundle.sh"), "/dev/astra-test", str(boot)]
    for includes_script in (True, False):
        names = ["astra68.core.rbf", "EXPECTED_DTB_SHA256"]
        if includes_script:
            names.append("boot.scr.uimg")
        else:
            (bundle / "boot.scr.uimg").unlink()
        (bundle / "SHA256SUMS").write_text("".join(
            hashlib.sha256((bundle / name).read_bytes()).hexdigest() + "  " + name + "\n"
            for name in names))
        subprocess.run(command, env=environment, check=True, capture_output=True)
        assert (boot / "astra68.core.rbf").read_bytes() == b"new fabric"
        assert (boot / "boot.scr.uimg").read_bytes() == selected, "fabric update replaced kernel selection"
        assert (boot / "Image").read_bytes() == b"selected kernel"
    (boot / "socfpga_agilex5_de25_nano.dtb").write_bytes(b"unqualified dtb")
    (boot / "astra68.core.rbf").write_bytes(b"old fabric")
    rejected = subprocess.run(command, env=environment, capture_output=True)
    assert rejected.returncode != 0
    assert (boot / "astra68.core.rbf").read_bytes() == b"old fabric"
    assert (boot / "boot.scr.uimg").read_bytes() == selected
    (boot / "socfpga_agilex5_de25_nano.dtb").write_bytes(b"qualified dtb")
    (boot / "boot.scr.uimg").unlink()
    rejected = subprocess.run(command, env=environment, capture_output=True)
    assert rejected.returncode != 0
    assert (boot / "astra68.core.rbf").read_bytes() == b"old fabric"
    assert not (boot / "boot.scr.uimg").exists()

print("DE25 boot contract: PASS")
