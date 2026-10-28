#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import tempfile
from unittest.mock import patch

from qemu_runtime import DEFAULT_MEMORY_BYTES, qemu_environment, require_provider_abi, unprivileged_command


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
    environment = qemu_environment(
        str(qemu), {"LD_LIBRARY_PATH": "/system/lib", "KEEP": "yes"})
    assert environment["LD_LIBRARY_PATH"] == \
        "%s:/system/lib" % os.path.realpath(library)
    assert environment["KEEP"] == "yes"
    assert "ASTRA_HOSTFS_ROOT" not in environment
    try:
        qemu_environment(str(qemu), {"ASTRA_HOSTFS_ROOT": str(root / "retired")})
        raise AssertionError("retired root authority accepted")
    except ValueError as error:
        assert "confined storage provider" in str(error)


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

with tempfile.TemporaryDirectory() as temporary:
    qemu = Path(temporary) / "qemu"
    provider = Path(temporary) / "provider"
    header = bytearray(20)
    header[:4] = b"\x7fELF"
    header[4:6] = b"\x02\x01"
    header[18:20] = b"\xb7\x00"  # AArch64
    qemu.write_bytes(header)
    header[18:20] = b"\x3e\x00"  # x86-64
    provider.write_bytes(header)
    try:
        require_provider_abi(qemu, provider)
        raise AssertionError("mixed QEMU/provider host ABIs accepted")
    except ValueError as error:
        assert "different host ELF ABIs" in str(error)
    provider.write_bytes(qemu.read_bytes())
    require_provider_abi(qemu, provider)
print("QEMU/storage provider host ABI pairing: PASS")

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    log = root / "export" / ".astra"
    log.mkdir(parents=True)
    outside = root / "outside"
    outside.write_text("outside diagnostic authority\n")
    (log / "syslog").symlink_to(outside)
    assert terminal.read_host_syslog(str(root / "export")) == ""
    (log / "syslog").unlink()
    (log / "syslog").write_text("native-log\n")
    (log / "syslog.1").write_text("older-log\n")
    assert terminal.read_host_syslog(str(root / "export")) == "older-log\nnative-log\n"
print("Root observer diagnostic symlink refusal: PASS")

with patch("qemu_runtime.os.geteuid", return_value=0):
    command = unprivileged_command(["qemu", "-S"], (1000, 1000))
    assert command[0] == "setpriv" and command[-2:] == ["qemu", "-S"]
    assert "--clear-groups" in command and "--bounding-set=-all" in command
    assert "--no-new-privs" in command and "--ambient-caps=-all" in command
    for credentials in ((0, 1000), (1000, 0)):
        try:
            unprivileged_command(["qemu"], credentials)
            raise AssertionError("root QEMU credentials accepted")
        except ValueError:
            pass
with patch("qemu_runtime.os.geteuid", return_value=1000):
    assert unprivileged_command(["qemu", "-S"]) == ["qemu", "-S"]
print("Device-only QEMU privilege separation: PASS")
