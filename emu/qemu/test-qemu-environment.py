#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import tempfile

from qemu_runtime import DEFAULT_MEMORY_BYTES, qemu_environment


HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location(
    "astra_terminal_gate", HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)
input_spec = importlib.util.spec_from_file_location(
    "astra_input_gate", HERE / "test-input.py")
input_gate = importlib.util.module_from_spec(input_spec)
input_spec.loader.exec_module(input_gate)

# Positive: the production memory profile is accepted.
input_gate.require_default_memory(DEFAULT_MEMORY_BYTES)
# Negative: the retired 128 MiB profile must not silently pass this gate.
try:
    input_gate.require_default_memory(128 * 1024 * 1024)
    raise AssertionError("the stale 128 MiB profile was accepted")
except AssertionError as error:
    assert str(error) == \
        "RAM size is 134217728 bytes, expected 536870912"

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    qemu = root / "qemu" / "bin" / "qemu-system-m68k-astra"
    library = root / "qemu" / "lib"
    library.mkdir(parents=True)
    hostfs = root / "hostfs"
    environment = qemu_environment(
        str(qemu), {"LD_LIBRARY_PATH": "/system/lib", "KEEP": "yes"},
        str(hostfs))
    assert environment["LD_LIBRARY_PATH"] == \
        "%s:/system/lib" % os.path.realpath(library)
    assert environment["KEEP"] == "yes"
    assert environment["ASTRA_HOSTFS_ROOT"] == str(hostfs)
    assert hostfs.is_dir()


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
