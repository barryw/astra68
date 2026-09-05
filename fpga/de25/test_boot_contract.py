#!/usr/bin/env python3
"""Regression contract for the DE25 HPS-first Astra boot path."""

import json
from pathlib import Path
import subprocess


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

build = (HERE / "build_boot_bundle.sh").read_text(encoding="utf-8")
for required in (
    "sha256sum -c BUILD_SHA256SUMS",
    "golden_top_boot.core.rbf",
    "astra68.hps.jic",
    "astra68.core.rbf",
    "program_hps_qspi.sh",
    "mkimage",
    "dumpimage",
    "EXPECTED_DTB_SHA256",
    "SHA256SUMS",
):
    assert required in build, required
assert "quartus_pfg" not in build

install = (HERE / "install_boot_bundle.sh").read_text(encoding="utf-8")
for required in (
    "sha256sum -c SHA256SUMS",
    "socfpga_agilex5_de25_nano.dtb",
    "astra68.core.rbf.astra-new",
    "boot.scr.uimg.astra-new",
    'mv "$mount/astra68.core.rbf.astra-new" "$mount/astra68.core.rbf"',
    'mv "$mount/boot.scr.uimg.astra-new" "$mount/boot.scr.uimg"',
):
    assert required in install, required
assert install.index('mv "$mount/astra68.core.rbf.astra-new"') < install.index(
    'mv "$mount/boot.scr.uimg.astra-new"')

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

print("DE25 boot contract: PASS")
