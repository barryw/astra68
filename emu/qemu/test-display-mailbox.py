#!/usr/bin/env python3
"""A physical display mailbox has exactly one QEMU producer."""

import os
import subprocess
import sys
import tempfile
import time


def start(qemu, rom, mailbox, stderr):
    environment = os.environ.copy()
    environment["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
    return subprocess.Popen(
        [qemu, "-M", "astra68", "-m", "1M", "-bios", rom, "-S",
         "-display", "none", "-monitor", "none", "-serial", "none"],
        env=environment, stdout=subprocess.DEVNULL, stderr=stderr, text=True)


def main():
    if len(sys.argv) != 3:
        print("usage: test-display-mailbox.py QEMU ROM", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory(prefix="astra-display-owner-") as root:
        mailbox = os.path.join(root, "display.bin")
        with open(mailbox, "wb") as stream:
            stream.truncate(4096 + 1280 * 720 * 2)
        first = start(sys.argv[1], sys.argv[2], mailbox,
                      subprocess.DEVNULL)
        try:
            time.sleep(0.25)
            if first.poll() is not None:
                raise RuntimeError("first QEMU failed to retain the mailbox")
            second = start(sys.argv[1], sys.argv[2], mailbox,
                           subprocess.PIPE)
            try:
                _, error = second.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                second.terminate()
                second.wait()
                raise RuntimeError("second QEMU acquired an owned mailbox")
            if second.returncode == 0 or "already owned by another QEMU" not in error:
                raise RuntimeError("second QEMU failed unclearly: %s" % error)
        finally:
            first.terminate()
            first.wait()
    print("ASTRA DISPLAY MAILBOX OWNERSHIP PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
