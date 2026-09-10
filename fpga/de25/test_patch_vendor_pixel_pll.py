#!/usr/bin/env python3

from pathlib import Path
import subprocess
import sys
import tempfile


HERE = Path(__file__).resolve().parent

fixture = """<component>
<ipxact:parameter parameterId="clockRate"><ipxact:value>74250000</ipxact:value></ipxact:parameter>
<ipxact:parameter parameterId="gui_number_of_clocks"><ipxact:value>1</ipxact:value></ipxact:parameter>
<ipxact:parameter parameterId="gui_output_clock_frequency0"><ipxact:value>74.25</ipxact:value></ipxact:parameter>
<ipxact:parameter parameterId="gui_output_clock_frequency1"><ipxact:value>12.288</ipxact:value></ipxact:parameter>
<entry>&lt;key&gt;CLOCK_RATE&lt;/key&gt;&lt;value&gt;74250000&lt;/value&gt;</entry>
</component>
"""

with tempfile.TemporaryDirectory() as temporary:
    path = Path(temporary) / "sys_pll.ip"
    path.write_text(fixture, encoding="utf-8")
    subprocess.run(
        [sys.executable, str(HERE / "patch_vendor_pixel_pll.py"), str(path)],
        check=True,
    )
    patched = path.read_text(encoding="utf-8")
    assert patched.count("148500000") == 2
    assert patched.count("148.5") == 1
    assert 'parameterId="gui_number_of_clocks"><ipxact:value>2' in patched
    assert 'parameterId="gui_output_clock_frequency1"><ipxact:value>165.0' in patched
    assert "74250000" not in patched
    assert "74.25" not in patched

print("DE25 vendor pixel PLL patch PASS")
