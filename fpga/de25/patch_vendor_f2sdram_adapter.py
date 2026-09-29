#!/usr/bin/env python3
"""Raise the vendor F2SDRAM adapter's read concurrency from 1 to 8.

The adapter is pass-through wiring, but its hw.tcl declares one outstanding
read, so Platform Designer builds the render engine's host-aperture path with
READ_ACCEPTANCE_CAPABILITY(1) and each burst waits a full HPS round trip.  The
HPS f2sdram port itself accepts 16.  Platform Designer caches interface
properties in the adapter .ip, in hps_subsys.qsys and in qsys_top.qsys's copy
of subsys_hps, so every cached copy is patched.  Idempotent; fails if the
vendor text changes.
"""

from pathlib import Path
import re
import sys

CAPABILITY = "8"
KEYS = (r"(?:readIssuingCapability|combinedIssuingCapability|"
        r"readAcceptanceCapability|combinedAcceptanceCapability)")
IPXACT = re.compile(
    r"(<ipxact:name>" + KEYS + r"</ipxact:name>\s*<ipxact:displayName>"
    r"[^<]*</ipxact:displayName>\s*<ipxact:value>)(1|8)(</ipxact:value>)")
ESCAPED = re.compile(
    r"(&lt;key&gt;" + KEYS + r"&lt;/key&gt;\s*&lt;value&gt;)(1|8)"
    r"(&lt;/value&gt;)")
PORT = re.compile(r"&lt;name&gt;([A-Za-z0-9_]+)&lt;/name&gt;")


def patch(path: Path, pattern: re.Pattern, expected: int,
          port_prefixes: tuple[str, ...] = ()) -> None:
    text = path.read_text(encoding="utf-8")
    found = 0

    def replace(match: re.Match) -> str:
        nonlocal found
        if port_prefixes:
            # Interface parameters follow the interface's port list, so the
            # nearest preceding name is one of the adapter's ports.
            names = PORT.findall(text, max(0, match.start() - 65536),
                                 match.start())
            if not names or not names[-1].startswith(port_prefixes):
                return match.group(0)
        found += 1
        return match.group(1) + CAPABILITY + match.group(3)

    text = pattern.sub(replace, text)
    if found != expected:
        raise SystemExit(
            f"{path}: expected {expected} adapter capabilities, found {found}")
    path.write_text(text, encoding="utf-8")


def main() -> None:
    tree = Path(sys.argv[1])
    hw = tree / "custom_ip/f2sdram_adapter/f2sdram_adapter_hw.tcl"
    text, count = re.subn(
        r"^(set_interface_property axi4_(?:man|sub) " + KEYS + r" )(?:1|8)$",
        r"\g<1>" + CAPABILITY, hw.read_text(encoding="utf-8"), flags=re.M)
    if count != 4:
        raise SystemExit(f"{hw}: expected 4 adapter capabilities, found {count}")
    hw.write_text(text, encoding="utf-8")

    ip = tree / "hps_subsys/ip/hps_subsys/f2sdram_adapter.ip"
    patch(ip, IPXACT, 4)
    patch(ip, ESCAPED, 4)
    patch(tree / "hps_subsys/hps_subsys.qsys", ESCAPED, 8, ("man_", "sub_"))
    patch(tree / "qsys_top.qsys", ESCAPED, 2, ("f2sdram_adapter_axi4_sub_",))


if __name__ == "__main__":
    main()
