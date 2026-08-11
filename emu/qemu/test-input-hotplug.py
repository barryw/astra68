#!/usr/bin/env python3
"""Regression checks for Astra's no-reboot input reconciliation."""

import importlib.util
import pathlib


SOURCE = pathlib.Path(__file__).with_name("astra-input-hotplug.py")
SPEC = importlib.util.spec_from_file_location("astra_input_hotplug", SOURCE)
HOTPLUG = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HOTPLUG)


class FakeQmp:
    def __init__(self, fail=None):
        self.calls = []
        self.fail = fail

    def execute(self, command, arguments=None):
        self.calls.append((command, arguments))
        if command == self.fail:
            raise HOTPLUG.QmpError("injected")
        return {}

    def close(self):
        self.calls.append(("close", None))


class BrokenStream:
    def write(self, _data):
        raise BrokenPipeError("gone")


def source(path, inode):
    return (path, 1, inode, 2)


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
    print("Astra input hotplug tests passed")


if __name__ == "__main__":
    main()
