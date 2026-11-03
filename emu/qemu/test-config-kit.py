#!/usr/bin/env python3
"""Boot Config Kit's native probe twice against one disposable host state."""

import argparse
import importlib.util
import os
from pathlib import Path
import shutil
import tempfile
import time

HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("astra_terminal_gate",
                                              HERE / "test-terminal.py")
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)


def run(qemu, rom, image):
    if os.geteuid() != 0:
        raise RuntimeError("root observer required; QEMU runs as ASTRA_TEST_QEMU_UID")
    with tempfile.TemporaryDirectory(prefix="astra-config-kit-") as scratch:
        root = Path(scratch)
        key_dir = root / "key"
        key_dir.mkdir(mode=0o700)
        os.environ["ASTRA_STORAGE_SECRET_KEY_PATH"] = str(key_dir / "host.key")
        exports = root / "exports"
        trial = root / "trial.img"
        shutil.copyfile(image, trial)
        previous = 0
        for marker in ("config-secret first-write", "config-secret restart-read"):
            machine = terminal.Machine(qemu, rom, str(trial), scratch,
                                       hostfs_root=exports)
            try:
                if not machine.wait_for_serial("stage 8", 120):
                    raise RuntimeError("boot did not reach stage 8: " +
                                       "\n".join(machine.recent_serial(30)))
                deadline = time.monotonic() + 60
                required = {marker, "config-secret roundtrip PASS",
                            "config-stale conflict/retry PASS",
                            "config-batch atomic PASS",
                            "config-concurrent writers PASS",
                            "config-public roundtrip PASS",
                            "config-secret unprivileged denied PASS"}
                while time.monotonic() < deadline:
                    fresh = "\n".join(machine.syslog_lines()[previous:])
                    if all(item in fresh for item in required):
                        break
                    time.sleep(0.2)
                else:
                    raise RuntimeError("Config Kit probe incomplete: " + fresh[-3000:])
                previous = len(machine.syslog_lines())
            finally:
                machine.close()
        documents = [path for path in (root / "config").rglob("settings.conf")
                     if b"network.probe" in path.read_bytes()]
        if len(documents) != 1:
            raise RuntimeError("expected one private probe document, found %d" %
                               len(documents))
        data = documents[0].read_bytes()
        if b"host held credential probe" in data:
            raise RuntimeError("sensitive setting missing or stored as plaintext")
        print("ASTRA CONFIG KIT PRIVATE/PUBLIC RESTART PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    args = parser.parse_args()
    run(args.qemu, args.rom, args.image)
