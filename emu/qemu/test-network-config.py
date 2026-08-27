#!/usr/bin/env python3
"""The installed network policy and boot ownership contract."""

import astra_image


def test_network_configuration_is_installed_under_config():
    assert astra_image.CONFIG_DIRECTORY == "config"
    assert astra_image.CONFIG_SCOPES == (
        "system", "services", "commands", "applications")
    assert astra_image.CONFIGURATION["system/network/resolv.conf"].endswith(
        "\n")
    assert "host resolver" in \
        astra_image.CONFIGURATION["system/network/resolv.conf"].lower()
    assert astra_image.CONFIGURATION["services/ntpd/settings.conf"] == \
        "astra-config 1\nschema 1\npool pool.ntp.org\n"


def test_ntpd_owns_initial_clock_sync_before_the_desktop():
    lines = [line for line in astra_image.STARTUP_MANIFEST.splitlines()
             if line]
    network = next(index for index, line in enumerate(lines)
                   if "SERVICES:network " in line)
    ntpd = next(index for index, line in enumerate(lines)
                if "SERVICES:ntpd " in line)
    desktop = next(index for index, line in enumerate(lines)
                   if "SERVICES:desktop " in line)
    assert network < ntpd < desktop
    assert "grants CLOCK CONFIG:r LIBS:r NETWORK" in lines[ntpd]
    assert "serves NTP" in lines[ntpd]
