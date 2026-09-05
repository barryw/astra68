#!/usr/bin/env python3
"""The shared Linux deployer must not hard-code the Arty data mount."""

from pathlib import Path


script = Path(__file__).with_name("deploy-arty-release.sh").read_text()
assert "STORE=${ASTRA_STORE:-/data/astra}" in script
assert "mkdir -p '$STORE/incoming'" in script
assert "install '$INCOMING' '$STORE'" in script
assert "'$STORE/current/bin/astra-release.py'" in script
assert "'$STORE/current'" in script

print("Linux release deployment profile test: PASS")
