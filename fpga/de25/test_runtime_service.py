#!/usr/bin/env python3
"""Pin the DE25 runtime's persistent paths and physical front-panel address."""

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

print("DE25 runtime service contract: PASS")
