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

remote = (Path(__file__).with_name(
    "astra-remote-desktop.service")).read_text()
for required in (
    "Requires=astra.service",
    "After=astra.service",
    "PartOf=astra.service",
    "ConditionPathExists=/dev/astra-display-capture",
    "ExecStart=/var/lib/astra/current/bin/astra-remote-desktop",
    "Environment=ASTRA_QMP_SOCKET=/run/astra/remote-desktop-qmp.sock",
    "Environment=ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET="
    "/run/astra/remote-desktop-control.sock",
    "Environment=ASTRA_RFB_LISTEN_ADDRESS=0.0.0.0",
    "Environment=ASTRA_RFB_PASSWORD_FILE=/etc/astra/remote-desktop.password",
    "Restart=always",
    "NoNewPrivileges=true",
    "ProtectSystem=strict",
    "RestrictAddressFamilies=AF_UNIX AF_INET",
    "DevicePolicy=closed",
    "DeviceAllow=/dev/astra-display-capture r",
):
    assert required in remote, required

assert "Environment=ASTRA_RFB_PASSWORD=" not in remote

print("DE25 remote desktop service contract: PASS")

modules = (Path(__file__).with_name(
    "astra-display-capture.conf")).read_text().splitlines()
assert modules == ["astra_display_capture"]
print("DE25 display capture module-load contract: PASS")
