#!/usr/bin/env python3
"""Attach Linux evdev devices to a running Astra QEMU instance."""

import argparse
import glob
import json
import os
import signal
import socket
import stat
import sys
import time


DEVICE_PATTERNS = {
    "keyboard": "*-event-kbd",
    "pointer": "*-event-mouse",
}
OBJECT_IDS = {
    "keyboard": "astra-keyboard",
    "pointer": "astra-pointer",
}


class QmpError(RuntimeError):
    pass


class QmpClient:
    def __init__(self, path, timeout=10.0):
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.socket = socket.socket(socket.AF_UNIX,
                                            socket.SOCK_STREAM)
                self.socket.connect(path)
                break
            except OSError:
                self.socket.close()
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        self.stream = self.socket.makefile("rwb", buffering=0)
        greeting = self._response()
        if "QMP" not in greeting:
            raise QmpError("QMP greeting missing")
        self.execute("qmp_capabilities")

    def _response(self):
        while True:
            line = self.stream.readline()
            if not line:
                raise QmpError("QMP disconnected")
            response = json.loads(line)
            if "event" not in response:
                return response

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        try:
            self.stream.write(json.dumps(request).encode("ascii") + b"\n")
            response = self._response()
        except (OSError, ValueError) as error:
            raise QmpError(str(error)) from error
        if "error" in response:
            raise QmpError(response["error"].get("desc", "QMP command failed"))
        return response.get("return")

    def close(self):
        try:
            self.stream.close()
        except OSError:
            pass
        finally:
            try:
                self.socket.close()
            except OSError:
                pass


def discover(directory):
    devices = {}
    for role, pattern in DEVICE_PATTERNS.items():
        for path in sorted(glob.glob(os.path.join(directory, pattern))):
            try:
                info = os.stat(path)
            except OSError:
                continue
            if stat.S_ISCHR(info.st_mode):
                devices[role] = (path, info.st_dev, info.st_ino,
                                 info.st_rdev)
                break
    return devices


def reconcile(qmp, attached, desired, report=print):
    for role in DEVICE_PATTERNS:
        current = attached.get(role)
        wanted = desired.get(role)
        if current == wanted:
            continue
        if current is not None:
            try:
                qmp.execute("object-del", {"id": OBJECT_IDS[role]})
            except QmpError as error:
                report(f"Astra {role} detach failed: {error}", file=sys.stderr)
                continue
            attached.pop(role)
            report(f"Astra {role}: detached")
        if wanted is not None:
            try:
                qmp.execute("object-add", {
                    "qom-type": "input-linux",
                    "id": OBJECT_IDS[role],
                    "evdev": wanted[0],
                    "repeat": False,
                })
            except QmpError as error:
                report(f"Astra {role} attach failed: {error}", file=sys.stderr)
                continue
            attached[role] = wanted
            report(f"Astra {role}: {wanted[0]}")


def synchronize(qmp_path, attached, desired, client=QmpClient, report=print):
    if attached == desired:
        return
    try:
        qmp = client(qmp_path)
    except (OSError, QmpError, ValueError) as error:
        report(f"Astra input QMP unavailable: {error}", file=sys.stderr)
        return
    try:
        reconcile(qmp, attached, desired, report)
    finally:
        qmp.close()


def run(qmp_path, device_directory, interval):
    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    attached = {}
    try:
        while running:
            synchronize(qmp_path, attached, discover(device_directory))
            time.sleep(interval)
    finally:
        synchronize(qmp_path, attached, {})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--qmp", required=True)
    parser.add_argument("--devices", default="/dev/input/by-id")
    parser.add_argument("--interval", type=float, default=0.1)
    arguments = parser.parse_args()
    if arguments.interval <= 0:
        parser.error("--interval must be positive")
    run(arguments.qmp, arguments.devices, arguments.interval)


if __name__ == "__main__":
    main()
