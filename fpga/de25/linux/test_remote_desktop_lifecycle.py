#!/usr/bin/env python3
"""Exercise the Linux remote-desktop broker without Astra hardware."""

import argparse
import errno
import os
from pathlib import Path
import signal
import socket
import subprocess
import tempfile
import time


def wait_for(predicate, description, timeout=5.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.01)
    raise RuntimeError("timed out waiting for " + description)


def port_available(port):
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("127.0.0.1", port))
        return True
    except OSError as error:
        if error.errno == errno.EADDRINUSE:
            return False
        raise
    finally:
        probe.close()


def connect_control(path):
    connection = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    connection.settimeout(5.0)
    connection.connect(path)
    return connection


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary")
    parser.add_argument("--qemu-user")
    parser.add_argument("--sysroot")
    arguments = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="astra-remote-lifecycle-") as root:
        control_path = os.path.join(root, "control.sock")
        capture_path = os.path.join(root, "capture.bin")
        port_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        port_probe.bind(("127.0.0.1", 0))
        port = port_probe.getsockname()[1]
        port_probe.close()
        environment = os.environ.copy()
        environment.update({
            "ASTRA_CAPTURE_DEVICE": capture_path,
            "ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET": control_path,
            "ASTRA_RFB_PORT": str(port),
        })
        runner = []
        if arguments.qemu_user is not None:
            runner.append(arguments.qemu_user)
            if arguments.sysroot is not None:
                runner.extend(("-L", arguments.sysroot))
        process = subprocess.Popen(
            runner + [arguments.binary], env=environment,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            wait_for(lambda: Path(control_path).exists(), "control socket")
            assert port_available(port), "VNC listened without an Astra lease"

            failed = connect_control(control_path)
            assert failed.recv(32).startswith(b"ERROR ")
            failed.close()
            assert process.poll() is None, "activation failure killed broker"

            with open(capture_path, "wb") as capture:
                capture.truncate(1920 * 1080 * 3)

            lease = connect_control(control_path)
            reply = lease.recv(32)
            assert reply == b"READY 1\n", (reply, output if 'output' in locals() else '')
            wait_for(lambda: not port_available(port), "VNC listener")
            lease.close()
            wait_for(lambda: port_available(port), "VNC shutdown")
            assert process.poll() is None, "lease release killed broker"

            lease = connect_control(control_path)
            reply = lease.recv(32)
            assert reply == b"READY 2\n", reply
            wait_for(lambda: not port_available(port), "VNC restart")
            lease.close()
            wait_for(lambda: port_available(port), "second VNC shutdown")
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            try:
                output = process.communicate(timeout=5.0)[0]
            except subprocess.TimeoutExpired:
                process.kill()
                output = process.communicate()[0]
                raise RuntimeError("remote desktop broker hung on shutdown")
        if process.returncode != 0:
            raise RuntimeError("remote desktop broker failed:\n" + output)
    print("ASTRA_REMOTE_DESKTOP_LIFECYCLE PASS")


if __name__ == "__main__":
    main()
