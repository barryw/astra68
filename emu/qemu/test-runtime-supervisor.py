#!/usr/bin/env python3
"""Guest watchdog timeout and retained-reason contract."""

from pathlib import Path
import runpy
import socket
import tempfile
import threading
import time


supervisor = runpy.run_path(str(Path(__file__).with_name("runtime-supervisor.py")))
GuestWatchdog = supervisor["GuestWatchdog"]
record_watchdog = supervisor["record_watchdog"]
reset_cause = supervisor["reset_cause"]
record_text = supervisor["record_text"]
QmpClient = runpy.run_path(str(Path(__file__).with_name(
    "astra-input-hotplug.py")))["QmpClient"]

watchdog = GuestWatchdog(Path("/absent"), Path("/absent"))
watchdog.started = 0
assert watchdog.observe(119, None) is None
assert watchdog.observe(120, None) == "guest heartbeat did not start"
assert watchdog.observe(121, 1) is None
assert watchdog.observe(180, None) is None
assert watchdog.observe(181, None) == "guest heartbeat stalled"
assert watchdog.observe(182, 2) is None
assert watchdog.observe(183, 0) is None  # guest reset starts a new incarnation

# Finite service completions can keep a slow startup alive beyond 120 seconds;
# a stopped startup still expires 60 seconds after its last real progress.
watchdog = GuestWatchdog(Path("/absent"), Path("/absent"))
watchdog.started = 0
for counter, now in enumerate((30, 80, 130, 180), start=1):
    assert watchdog.observe(now, counter) is None
    assert watchdog.observe(now + 49, None) is None
assert watchdog.observe(240, None) == "guest heartbeat stalled"

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / "watchdog-latest.log"
    record_watchdog(path, "guest heartbeat stalled", 2, "QMP timed out")
    assert "heartbeat=2" in path.read_text()
    assert "QMP timed out" in path.read_text()
    assert not path.with_name(path.name + ".tmp").exists()

    reset = Path(directory) / "reset-cause-latest.log"
    record_text(reset, "boot=test cause=watchdog-event preload=0xa57a68d1\n")
    assert reset.read_text() == "boot=test cause=watchdog-event preload=0xa57a68d1\n"
    assert not reset.with_name(reset.name + ".tmp").exists()
    assert reset_cause("console=ttyS0 astra.watchdog_preload=0xa57a68d1") == "watchdog-event"
    assert reset_cause("astra.watchdog_preload=a57a68d0") == "no-watchdog-event-observed"
    for cmdline in ("", "astra.watchdog_preload=unknown", "astra.watchdog_preload=0", "astra.watchdog_preload=0xa57a68d2", "astra.watchdog_preload=a57a68d1 astra.watchdog_preload=a57a68d0"):
        assert reset_cause(cmdline) == "unknown"

    listener = socket.socket(socket.AF_UNIX)
    listener.bind(str(Path(directory) / "qmp.sock"))
    listener.listen(1)
    def silent_peer():
        connection, _ = listener.accept()
        try:
            time.sleep(0.5)
        finally:
            connection.close()
            listener.close()
    peer = threading.Thread(target=silent_peer)
    peer.start()
    started = time.monotonic()
    try:
        QmpClient(str(Path(directory) / "qmp.sock"), timeout=0.1)
        raise AssertionError("silent QMP peer was accepted")
    except TimeoutError:
        assert time.monotonic() - started < 2.0
    peer.join()

print("Astra runtime guest watchdog: PASS")
