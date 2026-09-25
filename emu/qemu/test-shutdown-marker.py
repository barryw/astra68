#!/usr/bin/env python3
"""A real guest write must distinguish clean shutdown from ordinary exit."""

import pathlib
import subprocess
import sys
import tempfile


source = pathlib.Path(__file__).with_name("tests") / "shutdown-marker.S"
qemu = sys.argv[1]

with tempfile.TemporaryDirectory() as directory:
    directory = pathlib.Path(directory)
    for mode, expected, marker in ((0, 0, "READY"),
                                   (1, 88, "SHUTDOWN"),
                                   (2, 89, "RESTART")):
        obj = directory / "marker.o"
        rom = directory / "marker.bin"
        subprocess.run(["m68k-astra-as", "-m68040", "--defsym",
                        "MODE=%d" % mode, str(source), "-o",
                        str(obj)], check=True)
        subprocess.run(["m68k-astra-objcopy", "-O", "binary", str(obj),
                        str(rom)], check=True)
        result = subprocess.run([qemu, "-M", "astra68", "-m", "512M",
                                 "-bios", str(rom), "-display", "none",
                                 "-monitor", "none", "-serial", "none"],
                                capture_output=True, text=True, timeout=15,
                                check=False)
        assert result.returncode == expected, result.stderr
        assert "ASTRA68-QEMU " + marker in result.stderr, result.stderr

print("QEMU clean power markers passed")
