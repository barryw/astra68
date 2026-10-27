#!/usr/bin/env python3
"""Regression checks for Astra's no-reboot input reconciliation."""

import importlib.util
import pathlib
import stat
from types import SimpleNamespace
from unittest import mock


SOURCE = pathlib.Path(__file__).with_name("astra-input-hotplug.py")
SPEC = importlib.util.spec_from_file_location("astra_input_hotplug", SOURCE)
HOTPLUG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOTPLUG)


class FakeQmp:
    def __init__(self, fail=None, responses=None):
        self.calls = []
        self.fail = fail
        self.responses = responses or {}

    def execute(self, command, arguments=None):
        self.calls.append((command, arguments))
        if command == self.fail:
            raise HOTPLUG.QmpError("injected")
        return self.responses.get(command, {})

    def close(self):
        self.calls.append(("close", None))


class BrokenStream:
    def write(self, _data):
        raise BrokenPipeError("gone")


def source(path, inode):
    return (path, 1, inode, 2, "/sys/dev/char/0:2")


def main():
    reports = []
    report = lambda *args, **kwargs: reports.append(args[0])
    qmp = FakeQmp()
    attached = {}
    keyboard = source("/dev/input/by-id/keyboard", 10)
    pointer = source("/dev/input/by-id/pointer", 11)

    HOTPLUG.reconcile(qmp, attached,
                      {"keyboard": keyboard, "pointer": pointer}, report)
    assert [call[0] for call in qmp.calls] == ["object-add", "object-add"]
    assert qmp.calls[0][1] == {
        "qom-type": "input-linux", "id": "astra-keyboard",
        "evdev": keyboard[0], "repeat": False,
    }
    assert attached == {"keyboard": keyboard, "pointer": pointer}

    gamepad = ("/dev/input/by-id/usb-controller-event-joystick", 1, 13, 3,
               "/devices/pad1")
    second = ("/dev/input/by-id/usb-other-event-joystick", 1, 14, 4,
              "/devices/pad2")
    devices = {gamepad[0]: (13, 3), second[0]: (14, 4),
               "/dev/input/by-path/port-event-joystick": (13, 3)}
    sysfs_paths = {"/sys/dev/char/0:3": "/devices/pad1",
                   "/sys/dev/char/0:4": "/devices/pad2"}
    def device_stat(path):
        inode, rdev = devices[path]
        return SimpleNamespace(st_mode=stat.S_IFCHR, st_dev=1,
                               st_ino=inode, st_rdev=rdev)
    with mock.patch.object(HOTPLUG.glob, "glob", side_effect=lambda pattern: [
        path for path in devices if pathlib.PurePath(path).match(
            pathlib.PurePath(pattern).name) and
        pathlib.PurePath(path).parent == pathlib.PurePath(pattern).parent]), \
         mock.patch.object(HOTPLUG.os, "stat", side_effect=device_stat), \
         mock.patch.object(HOTPLUG.os.path, "realpath",
                           side_effect=lambda path: sysfs_paths.get(path, path)):
        found = HOTPLUG.discover("/dev/input/by-id")
        assert found["gamepad0"] == gamepad
        assert found["gamepad1"] == second
        sysfs_paths["/sys/dev/char/0:3"] = "/devices/pad1-replugged"
        found = HOTPLUG.discover("/dev/input/by-id", {"gamepad0": gamepad,
                                                      "gamepad1": second})
        assert found["gamepad0"] != gamepad
        assert found["gamepad0"][:4] == gamepad[:4]
        sysfs_paths["/sys/dev/char/0:3"] = "/devices/pad1"
        del devices[gamepad[0]]
        del devices["/dev/input/by-path/port-event-joystick"]
        found = HOTPLUG.discover("/dev/input/by-id", {"gamepad0": gamepad,
                                                      "gamepad1": second})
        assert found == {"gamepad1": second}

    qmp.calls.clear()
    HOTPLUG.reconcile(qmp, attached,
                      {"keyboard": keyboard, "pointer": pointer,
                       "gamepad0": gamepad}, report)
    assert qmp.calls == [("object-add", {
        "qom-type": "astra-evdev-gamepad", "id": "astra-gamepad0",
        "evdev": gamepad[0], "slot": 0,
    })]
    assert attached["gamepad0"] == gamepad

    qmp.calls.clear()
    HOTPLUG.reconcile(qmp, attached,
                      {"keyboard": keyboard, "pointer": pointer}, report)
    assert qmp.calls == [("object-del", {"id": "astra-gamepad0"})]

    qmp.calls.clear()
    HOTPLUG.reconcile(qmp, attached,
                      {"keyboard": keyboard, "pointer": pointer}, report)
    assert qmp.calls == []

    replacement = source(keyboard[0], 12)
    HOTPLUG.reconcile(qmp, attached,
                      {"keyboard": replacement, "pointer": pointer}, report)
    assert [call[0] for call in qmp.calls] == ["object-del", "object-add"]
    assert attached["keyboard"] == replacement

    qmp.calls.clear()
    HOTPLUG.reconcile(qmp, attached, {}, report)
    assert [call[0] for call in qmp.calls] == ["object-del", "object-del"]
    assert attached == {}

    failed = FakeQmp(fail="object-add")
    HOTPLUG.reconcile(failed, attached, {"keyboard": keyboard}, report)
    assert attached == {}
    assert len(failed.calls) == 1

    clients = []
    factory = lambda _path: clients.append(FakeQmp()) or clients[-1]
    HOTPLUG.synchronize("qmp", attached, {"keyboard": keyboard}, factory,
                        report)
    assert [call[0] for call in clients[0].calls] == ["object-add", "close"]
    HOTPLUG.synchronize("qmp", attached, {"keyboard": keyboard}, factory,
                        report)
    assert len(clients) == 1

    disconnected = HOTPLUG.QmpClient.__new__(HOTPLUG.QmpClient)
    disconnected.stream = BrokenStream()
    try:
        disconnected.execute("object-del", {"id": "astra-keyboard"})
        assert False, "broken QMP write accepted"
    except HOTPLUG.QmpError:
        pass

    qmp = FakeQmp(responses={
        "query-cpus-fast": [{"cpu-index": 0, "thread-id": 102}],
    })
    affinities = []
    assert HOTPLUG.tune_runtime(
        qmp, task_ids=[100, 101, 102],
        set_affinity=lambda task, cpus: affinities.append((task, cpus)),
        report=report)
    assert affinities == [(100, {3}), (101, {3}), (102, {2})]

    affinities.clear()
    with mock.patch.dict(HOTPLUG.os.environ,
                         {"ASTRA_VCPU_CPU": "2", "ASTRA_IO_CPU": "3"}):
        assert HOTPLUG.tune_runtime(
            qmp, task_ids=[100, 101, 102],
            set_affinity=lambda task, cpus: affinities.append((task, cpus)),
            report=report)
    assert affinities == [(100, {3}), (101, {3}), (102, {2})]

    qmp = FakeQmp(responses={"query-cpus-fast": []})
    assert not HOTPLUG.tune_runtime(qmp, task_ids=[], report=report)
    print("Astra input hotplug tests passed")


if __name__ == "__main__":
    main()
