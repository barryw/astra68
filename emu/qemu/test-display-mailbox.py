#!/usr/bin/env python3
"""The physical display mailbox: one QEMU producer, completions by wake.

Ownership: a second QEMU cannot take a mailbox the first one holds.

Completion wake (Linux hosts, with a storage image): a stand-in for the board
helper completes every request and futex-wakes completion_sequence, exactly
as astra-terminal-display does. QEMU does not poll the mailbox, so the guest
only reaches its desktop if the wake is delivered, and the gap from a
completion to the guest's next request shows how quickly it is seen.
"""

import ctypes
import mmap
import os
import platform
import statistics
import subprocess
import sys
import tempfile
import threading
import time

from qemu_runtime import DEFAULT_MEMORY, qemu_environment

# Mailbox 1.7: a header file (the futex words) and a separate payload.
HEADER_BYTES = 4096
PAYLOAD_BYTES = 8 * 1024 * 1024
REQUEST_SEQUENCE = 8
REQUEST_ID = 12
COMPLETION_SEQUENCE = 24
COMPLETION_ID = 28
COMPLETION_STATUS = 32
COMPLETION_GENERATION = 36
FUTEX_WAIT = 0
FUTEX_WAKE = 1
SYS_FUTEX = {"x86_64": 202, "aarch64": 98}
BOOT_MARKER = "stage 8"
BOOT_SECONDS = 120.0
RUN_SECONDS = 20.0
MINIMUM_COMPLETIONS = 20
# A polled completion is seen a whole poll period late (1 ms before 1.6); a
# woken one within microseconds. Back-to-back requests expose the difference.
FASTEST_GAP_SECONDS = 0.0005


def start(qemu, rom, mailbox, stderr):
    environment = os.environ.copy()
    environment["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
    environment["ASTRA_DISPLAY_PAYLOAD_PATH"] = payload_path(mailbox)
    return subprocess.Popen(
        [qemu, "-M", "astra68", "-m", "1M", "-bios", rom, "-S",
         "-display", "none", "-monitor", "none", "-serial", "none"],
        env=environment, stdout=subprocess.DEVNULL, stderr=stderr, text=True)


def payload_path(mailbox):
    return mailbox + ".payload"


def create_mailbox(path):
    with open(path, "wb") as stream:
        stream.truncate(HEADER_BYTES)
    with open(payload_path(path), "wb") as stream:
        stream.truncate(PAYLOAD_BYTES)


def ownership(qemu, rom, root):
    mailbox = os.path.join(root, "owned.bin")
    create_mailbox(mailbox)
    first = start(qemu, rom, mailbox, subprocess.DEVNULL)
    try:
        time.sleep(0.25)
        if first.poll() is not None:
            raise RuntimeError("first QEMU failed to retain the mailbox")
        second = start(qemu, rom, mailbox, subprocess.PIPE)
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


class Helper:
    """Completes each request the way the board helper does."""

    def __init__(self, view):
        self.libc = ctypes.CDLL(None, use_errno=True)
        self.futex = SYS_FUTEX[platform.machine()]
        self.words = {offset: ctypes.c_uint32.from_buffer(view, offset)
                      for offset in (REQUEST_SEQUENCE, REQUEST_ID,
                                     COMPLETION_SEQUENCE, COMPLETION_ID,
                                     COMPLETION_STATUS, COMPLETION_GENERATION)}
        self.stopping = False
        self.completions = 0
        self.gaps = []
        self.thread = threading.Thread(target=self.run, daemon=True)

    def release(self):
        self.words.clear()

    def call(self, offset, operation, value, timeout=None):
        address = ctypes.addressof(self.words[offset])
        self.libc.syscall(self.futex, ctypes.c_void_p(address), operation,
                          value, timeout, None, 0)

    def run(self):
        timeout = ctypes.create_string_buffer(16)
        ctypes.c_long.from_buffer(timeout, 8).value = 50_000_000
        seen = 0
        completed_at = None
        while not self.stopping:
            sequence = self.words[REQUEST_SEQUENCE].value
            if sequence == seen:
                self.call(REQUEST_SEQUENCE, FUTEX_WAIT, sequence, timeout)
                continue
            if completed_at is not None:
                self.gaps.append(time.monotonic() - completed_at)
            self.words[COMPLETION_ID].value = self.words[REQUEST_ID].value
            self.words[COMPLETION_STATUS].value = 0
            self.words[COMPLETION_GENERATION].value = self.completions + 1
            self.words[COMPLETION_SEQUENCE].value = sequence
            self.call(COMPLETION_SEQUENCE, FUTEX_WAKE, 1)
            completed_at = time.monotonic()
            seen = sequence
            self.completions += 1


def completion_wake(qemu, rom, image, root):
    mailbox = os.path.join(root, "wake.bin")
    create_mailbox(mailbox)
    environment = qemu_environment(
        qemu, hostfs_root=os.path.join(root, "hostfs"))
    environment["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
    environment["ASTRA_DISPLAY_PAYLOAD_PATH"] = payload_path(mailbox)
    with open(mailbox, "r+b") as stream:
        view = mmap.mmap(stream.fileno(), HEADER_BYTES)
    helper = Helper(view)
    helper.thread.start()
    process = subprocess.Popen(
        [qemu, "-M", "astra68", "-m", DEFAULT_MEMORY, "-bios", rom,
         "-display", "none", "-monitor", "none", "-serial", "stdio",
         "-no-reboot", "-drive", "if=none,format=raw,file=%s" % image],
        env=environment, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT, text=True, bufsize=1)
    log = []
    try:
        booted = threading.Event()

        def pump():
            for line in process.stdout:
                log.append(line.rstrip("\n"))
                if BOOT_MARKER in line:
                    booted.set()

        threading.Thread(target=pump, daemon=True).start()
        if not booted.wait(BOOT_SECONDS):
            raise RuntimeError("guest did not boot\n%s" % "\n".join(log[-40:]))
        time.sleep(RUN_SECONDS)
        if process.poll() is not None:
            raise RuntimeError("QEMU exited %s\n%s" %
                               (process.returncode, "\n".join(log[-40:])))
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        helper.stopping = True
        helper.thread.join(timeout=1)
        helper.release()
        view.close()
    if helper.completions < MINIMUM_COMPLETIONS or not helper.gaps:
        raise RuntimeError("only %d display requests completed; the guest "
                           "stalled waiting for a completion" %
                           helper.completions)
    gaps = sorted(helper.gaps)
    if gaps[0] >= FASTEST_GAP_SECONDS:
        raise RuntimeError("no completion was seen sooner than %.0f us; QEMU "
                           "is polling instead of waking" % (gaps[0] * 1e6))
    return helper.completions, gaps[0], statistics.median(gaps)


def main():
    if len(sys.argv) not in (3, 4):
        print("usage: test-display-mailbox.py QEMU ROM [IMAGE]",
              file=sys.stderr)
        return 2
    qemu, rom = sys.argv[1], sys.argv[2]
    with tempfile.TemporaryDirectory(prefix="astra-display-mailbox-") as root:
        ownership(qemu, rom, root)
        print("ASTRA DISPLAY MAILBOX OWNERSHIP PASS")
        if len(sys.argv) == 4:
            if platform.system() != "Linux":
                print("ASTRA DISPLAY MAILBOX WAKE SKIPPED: futex needs Linux")
                return 0
            completions, fastest, median = completion_wake(
                qemu, rom, sys.argv[3], root)
            print("ASTRA DISPLAY MAILBOX WAKE PASS completions=%d "
                  "next-request-us min=%.0f median=%.0f" %
                  (completions, fastest * 1e6, median * 1e6))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
