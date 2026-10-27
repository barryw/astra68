#!/usr/bin/env python3
"""Reconcile Linux host resources with a running Astra QEMU instance."""

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
GAMEPAD_LIMIT = 4


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


def discover(directory, attached=None):
    def identity(path):
        try:
            info = os.stat(path)
        except OSError:
            return None
        if not stat.S_ISCHR(info.st_mode):
            return None
        sysfs = f"/sys/dev/char/{os.major(info.st_rdev)}:{os.minor(info.st_rdev)}"
        return (path, info.st_dev, info.st_ino, info.st_rdev,
                os.path.realpath(sysfs))

    devices = {}
    for role, pattern in DEVICE_PATTERNS.items():
        for path in sorted(glob.glob(os.path.join(directory, pattern))):
            found = identity(path)
            if found is not None:
                devices[role] = found
                break
    candidates = []
    seen_devices = set()
    patterns = (os.path.join(directory, "*-event-joystick"),
                os.path.join(os.path.dirname(directory), "by-path",
                             "*-event-joystick"))
    for path in sorted(set(path for pattern in patterns
                           for path in glob.glob(pattern))):
        found = identity(path)
        if found is not None and found[3] not in seen_devices:
            seen_devices.add(found[3])
            candidates.append(found)
    attached = attached or {}
    for slot in range(GAMEPAD_LIMIT):
        current = attached.get(f"gamepad{slot}")
        match = next((item for item in candidates if current is not None and
                      item[0] == current[0]), None)
        if match is not None:
            devices[f"gamepad{slot}"] = match
            candidates.remove(match)
    for item in candidates:
        for slot in range(GAMEPAD_LIMIT):
            role = f"gamepad{slot}"
            if role not in devices:
                devices[role] = item
                break
        else:
            break
    return devices


def reconcile(qmp, attached, desired, report=print):
    for role in (*DEVICE_PATTERNS, *(f"gamepad{slot}"
                                      for slot in range(GAMEPAD_LIMIT))):
        current = attached.get(role)
        wanted = desired.get(role)
        if current == wanted:
            continue
        if current is not None:
            try:
                qmp.execute("object-del", {"id": OBJECT_IDS.get(role,
                                                      f"astra-{role}")})
            except QmpError as error:
                report(f"Astra {role} detach failed: {error}", file=sys.stderr)
                continue
            attached.pop(role)
            report(f"Astra {role}: detached")
        if wanted is not None:
            try:
                options = {"qom-type": "input-linux",
                           "id": OBJECT_IDS.get(role, f"astra-{role}"),
                           "evdev": wanted[0], "repeat": False}
                if role.startswith("gamepad"):
                    options = {"qom-type": "astra-evdev-gamepad",
                               "id": f"astra-{role}",
                               "evdev": wanted[0],
                               "slot": int(role[len("gamepad"):])}
                qmp.execute("object-add", options)
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


def tune_runtime(qmp, task_ids=None, set_affinity=None, report=print):
    """Give the TCG vCPU and QEMU host work separate physical cores."""
    if (os.cpu_count() or 1) < 2:
        return True
    try:
        vcpu_cpu = int(os.environ.get("ASTRA_VCPU_CPU", "2"))
        io_cpu = int(os.environ.get("ASTRA_IO_CPU", "3"))
        if vcpu_cpu < 0 or io_cpu < 0 or vcpu_cpu == io_cpu:
            raise ValueError("runtime CPU assignments must be distinct and nonnegative")
        cpus = qmp.execute("query-cpus-fast")
        vcpu_tasks = {int(cpu["thread-id"]) for cpu in cpus}
        if not vcpu_tasks:
            raise QmpError("QEMU reported no vCPU thread")
        if set_affinity is None:
            set_affinity = os.sched_setaffinity
        if task_ids is None:
            first = next(iter(vcpu_tasks))
            with open(f"/proc/{first}/status", encoding="ascii") as status:
                tgid = next(int(line.split()[1]) for line in status
                            if line.startswith("Tgid:"))
            task_ids = [int(os.path.basename(path)) for path in
                        glob.glob(f"/proc/{tgid}/task/*")]
        for task_id in task_ids:
            set_affinity(task_id,
                         {vcpu_cpu} if task_id in vcpu_tasks else {io_cpu})
    except (KeyError, OSError, QmpError, StopIteration, TypeError,
            ValueError) as error:
        report(f"Astra runtime tuning unavailable: {error}", file=sys.stderr)
        return False
    report(f"Astra runtime: vCPU on CPU{vcpu_cpu}, host I/O on CPU{io_cpu}")
    return True


def synchronize_runtime(qmp_path, client=QmpClient, report=print):
    try:
        qmp = client(qmp_path)
    except (OSError, QmpError, ValueError) as error:
        report(f"Astra runtime QMP unavailable: {error}", file=sys.stderr)
        return False
    try:
        return tune_runtime(qmp, report=report)
    finally:
        qmp.close()


def run(qmp_path, device_directory, interval):
    sys.stdout.reconfigure(line_buffering=True)
    running = True

    def stop(_signum, _frame):
        nonlocal running
        running = False

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    attached = {}
    tuned = False
    try:
        while running:
            if not tuned:
                tuned = synchronize_runtime(qmp_path)
            synchronize(qmp_path, attached,
                        discover(device_directory, attached))
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
