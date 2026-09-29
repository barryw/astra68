#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
KEYS = ("readIssuingCapability", "combinedIssuingCapability",
        "readAcceptanceCapability", "combinedAcceptanceCapability")


def escaped(port: str, value: str = "1") -> str:
    entries = "".join(
        f"&lt;entry&gt;&lt;key&gt;{key}&lt;/key&gt;\n"
        f"    &lt;value&gt;{value}&lt;/value&gt;&lt;/entry&gt;\n"
        for key in KEYS)
    return f"&lt;name&gt;{port}&lt;/name&gt;\n{entries}"


hw = """set_interface_property axi4_man readIssuingCapability 1
set_interface_property axi4_man combinedIssuingCapability 1
set_interface_property axi4_sub readAcceptanceCapability 1
set_interface_property axi4_sub writeAcceptanceCapability 1
set_interface_property axi4_sub combinedAcceptanceCapability 1
"""
ip = "".join(
    f"<ipxact:name>{key}</ipxact:name>\n<ipxact:displayName>x</ipxact:displayName>"
    f"\n<ipxact:value>1</ipxact:value>\n" for key in KEYS) + escaped("man_arid")
# hps_subsys.qsys caches the adapter twice (instance and its .ip snapshot).
hps = 2 * (escaped("man_awregion").replace("Acceptance", "AcceptanceX")
           + escaped("sub_awregion").replace("Issuing", "IssuingX")
           + escaped("f2sdram_rid", "16"))
# Only the two acceptance entries under the exported adapter port change; an
# unrelated AXI4-Lite interface declaring 1 keeps it.
top = (escaped("f2sdram_adapter_axi4_sub_awregion").replace(
          "Issuing", "IssuingX")
       + escaped("s0_axi4lite_rvalid"))

with tempfile.TemporaryDirectory() as temporary:
    tree = Path(temporary)
    files = {
        "custom_ip/f2sdram_adapter/f2sdram_adapter_hw.tcl": hw,
        "hps_subsys/ip/hps_subsys/f2sdram_adapter.ip": ip,
        "hps_subsys/hps_subsys.qsys": hps,
        "qsys_top.qsys": top,
    }
    for name, text in files.items():
        (tree / name).parent.mkdir(parents=True, exist_ok=True)
        (tree / name).write_text(text, encoding="utf-8")
    command = [sys.executable, str(HERE / "patch_vendor_f2sdram_adapter.py"),
               str(tree)]
    subprocess.run(command, check=True)
    first = {name: (tree / name).read_text(encoding="utf-8") for name in files}
    subprocess.run(command, check=True)
    assert first == {name: (tree / name).read_text(encoding="utf-8")
                     for name in files}, "patch is not idempotent"

    assert first["custom_ip/f2sdram_adapter/f2sdram_adapter_hw.tcl"].count(
        "Capability 8") == 4
    assert "writeAcceptanceCapability 1" in first[
        "custom_ip/f2sdram_adapter/f2sdram_adapter_hw.tcl"]
    assert first["hps_subsys/ip/hps_subsys/f2sdram_adapter.ip"].count(
        ">8<") == 4
    assert first["hps_subsys/hps_subsys.qsys"].count("&gt;8&lt;") == 8
    assert first["hps_subsys/hps_subsys.qsys"].count("&gt;16&lt;") == 8
    assert first["qsys_top.qsys"].count("&gt;8&lt;") == 2
    lite = first["qsys_top.qsys"].split("s0_axi4lite_rvalid")[1]
    assert "&gt;8&lt;" not in lite

    (tree / "qsys_top.qsys").write_text(escaped("s0_axi4lite_rvalid"),
                                        encoding="utf-8")
    failed = subprocess.run(command, text=True, capture_output=True)
    assert failed.returncode != 0
    assert "expected 2 adapter capabilities, found 0" in failed.stderr

print("DE25 vendor F2SDRAM adapter patch PASS")
