#!/usr/bin/env python3
"""Completed snapshots drain submitted workers and always resume the guest."""
import importlib.util
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("terminal_gate", Path(__file__).with_name("test-terminal.py"))
terminal = importlib.util.module_from_spec(spec)
spec.loader.exec_module(terminal)


class Peer:
    def __init__(self, counts=(20, 21, 22, 22), fail=False):
        self.counts = list(counts)
        self.inflight = [2, 0, 0, 0, 0]
        self.samples = 0
        self.paused = False
        self.resumed = False
        self.fail = fail

    def execute(self, command, arguments=None):
        if command == "stop":
            self.paused = True
            return
        if command == "cont":
            self.paused = False
            self.resumed = True
            return
        assert command == "qom-get" and self.paused
        name = arguments["property"]
        if name == "astra-host-inflight":
            return self.inflight.pop(0) if self.inflight else 0
        if name == "astra-host-commands":
            return self.counts.pop(0)
        if self.fail:
            raise RuntimeError("property failed")
        self.samples += 1
        return self.samples


with patch.object(terminal.time, "sleep", return_value=None):
    peer = Peer()
    assert terminal.Qmp.host_snapshot(peer, ("sample",)) == {"sample": 2}
    assert peer.samples == 2 and peer.resumed and not peer.paused
    failed = Peer(counts=(20,), fail=True)
    try:
        terminal.Qmp.host_snapshot(failed, ("sample",))
        raise AssertionError("failed property accepted")
    except RuntimeError as error:
        assert str(error) == "property failed"
    assert failed.resumed and not failed.paused
    timed = Peer()
    try:
        terminal.Qmp.host_snapshot(timed, ("sample",), deadline=0.0)
        raise AssertionError("expired snapshot deadline accepted")
    except RuntimeError as error:
        assert "did not drain" in str(error)
    assert timed.resumed and not timed.paused
print("Completed host snapshot/drift/failure recovery: PASS")
