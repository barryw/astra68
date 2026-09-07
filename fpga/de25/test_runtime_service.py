#!/usr/bin/env python3
"""Pin the DE25 service boundary and keep runtime policy in the release."""

from pathlib import Path


unit = Path(__file__).with_name("astra.service").read_text()
for required in (
    "Wants=network-online.target time-sync.target "
    "systemd-time-wait-sync.service",
    "After=network-online.target time-sync.target "
    "systemd-time-wait-sync.service",
    "ConditionFileIsExecutable=/var/lib/astra/current/bin/run-arty.sh",
    "ConditionKernelCommandLine=astra.fabric=ready",
    "Environment=ASTRA_STORE=/var/lib/astra",
    "Environment=ASTRA_FRONT_PANEL_MMIO_OFFSET=0x20107000",
    "ExecStart=/var/lib/astra/current/bin/run-arty.sh",
    "Restart=always",
    "Nice=-10",
    "IOSchedulingPriority=0",
):
    assert required in unit, required

for release_policy in ("ASTRA_VCPU_CPU", "ASTRA_IO_CPU", "ASTRA_AUX_CPU",
                       "ASTRA_DISPLAY_CPU"):
    assert release_policy not in unit, release_policy

print("DE25 runtime service contract: PASS")
