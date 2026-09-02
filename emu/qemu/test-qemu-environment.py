#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import tempfile


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    qemu = root / "qemu" / "bin" / "qemu-system-m68k-astra"
    library = root / "qemu" / "lib"
    library.mkdir(parents=True)
    environment = terminal.qemu_environment(
        str(qemu), {"LD_LIBRARY_PATH": "/system/lib", "KEEP": "yes"})
    assert environment["LD_LIBRARY_PATH"] == \
        "%s:/system/lib" % os.path.realpath(library)
    assert environment["KEEP"] == "yes"


class ExitedProcess:
    returncode = 17

    @staticmethod
    def poll():
        return 17


try:
    terminal.Qmp("/no/such/astra-qmp.sock", deadline=1.0,
                 process=ExitedProcess())
    raise AssertionError("QMP waited for a process that had already exited")
except RuntimeError as error:
    assert str(error) == "QEMU exited with status 17 before QMP was available"

print("QEMU private library environment: PASS")
