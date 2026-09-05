#!/usr/bin/env python3
"""The board-qualified ADV7513 setup must match Astra's stereo stream."""

from pathlib import Path
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent
SOURCE = """\
05 : LUT_DATA <= 16'h0cbc; // four I2S lanes
06 : LUT_DATA <= 16'h1472; // 16-bit, 8 channels
16 : LUT_DATA <= 16'h7307; // eight-channel infoframe
17 : LUT_DATA <= 16'h761f; // eight-channel speakers
"""

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "I2C_HDMI_Config.v"
    path.write_text(SOURCE, encoding="utf-8")
    result = subprocess.run(
        [sys.executable, str(HERE / "patch_vendor_hdmi.py"), str(path)],
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    patched = path.read_text(encoding="utf-8")
    for value in ("16'h0c84", "16'h140b", "16'h7301", "16'h7600"):
        assert value in patched, value

    rejected = subprocess.run(
        [sys.executable, str(HERE / "patch_vendor_hdmi.py"), str(path)],
        text=True,
        capture_output=True,
    )
    assert rejected.returncode != 0
    assert "expected one" in rejected.stderr

print("DE25 ADV7513 stereo patch: PASS")
