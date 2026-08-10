#!/usr/bin/env python3
"""Boot the protected display service and require one consumed fence."""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import astra_image

BOOT_MARKER = "stage 8"
DISPLAY_QUEUE = 0xFFF001E0
ASTRAEA_IRQ_STATUS = 0xFFF10014
QUEUE_REQUEST_READY = 1 << 8
DRAW_DONE = 1 << 3
PRESENT_BUDGET_CYCLES = 250000


class Qmp:
    def __init__(self, path, deadline=20.0):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        end = time.monotonic() + deadline
        while True:
            try:
                self.socket.connect(path)
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if time.monotonic() >= end:
                    raise
                time.sleep(0.05)
        self.file = self.socket.makefile("rwb", buffering=0)
        self.file.readline()
        self.execute("qmp_capabilities")

    def execute(self, command, arguments=None):
        request = {"execute": command}
        if arguments is not None:
            request["arguments"] = arguments
        self.file.write(json.dumps(request).encode("ascii") + b"\n")
        while True:
            reply = json.loads(self.file.readline())
            if "event" in reply:
                continue
            if "error" in reply:
                raise RuntimeError("QMP %s failed: %s" %
                                   (command, reply["error"]))
            return reply.get("return")

    def monitor(self, line):
        return self.execute("human-monitor-command", {"command-line": line})

    def property(self, name):
        return self.execute("qom-get", {"path": "/machine",
                                        "property": name})

    def word(self, address):
        for token in self.monitor("xp /1xw 0x%x" % address).split():
            if token.startswith("0x") and len(token) == 10:
                return int(token, 16)
        raise RuntimeError("no word read back from 0x%x" % address)


def run(qemu, rom, image, catalog, deadline):
    with tempfile.TemporaryDirectory(prefix="astra-display-") as directory:
        socket_path = os.path.join(directory, "display-qmp.sock")
        scratch = os.path.join(directory, "card.img")
        shutil.copyfile(image, scratch)
        astra_image.install(
            scratch, catalog,
            service_names=("storage", "events", "display", "desktop"),
            manifest_text=astra_image.DISPLAY_STARTUP_MANIFEST)
        machine = subprocess.Popen(
            [qemu, "-M", "astra68", "-m", "128M", "-bios", rom,
             "-display", "none", "-monitor", "none", "-serial", "stdio",
             "-no-reboot", "-qmp", "unix:%s,server=on,wait=off" % socket_path,
             "-drive", "if=none,format=raw,file=%s" % scratch],
            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True)
        booted = threading.Event()
        serial = []

        def drain():
            for line in machine.stdout:
                line = line.rstrip("\n")
                serial.append(line)
                if BOOT_MARKER in line:
                    booted.set()

        threading.Thread(target=drain, daemon=True).start()
        started = time.monotonic()
        try:
            qmp = Qmp(socket_path)
            if not booted.wait(deadline):
                raise RuntimeError("no %r within %.0fs; last serial: %s" %
                                   (BOOT_MARKER, deadline, serial[-8:]))
            elapsed = time.monotonic() - started
            submissions = qmp.property("astra-display-submissions")
            completions = qmp.property("astra-display-completions")
            generation = qmp.property("astra-display-generation")
            submit_cycle = qmp.property("astra-display-submit-cycle")
            completion_cycle = qmp.property(
                "astra-display-completion-cycle")
            collect_cycle = qmp.property("astra-display-collect-cycle")
            queue = qmp.word(DISPLAY_QUEUE)
            irq = qmp.word(ASTRAEA_IRQ_STATUS)
            if (submissions, completions) != (1, 1):
                raise RuntimeError("display requests=%d completions=%d" %
                                   (submissions, completions))
            if generation == 0:
                raise RuntimeError("display completion had no generation")
            if queue != QUEUE_REQUEST_READY:
                raise RuntimeError("display completion was not consumed: "
                                   "queue=0x%08x" % queue)
            if irq & DRAW_DONE:
                raise RuntimeError("display completion IRQ was not cleared")
            present_cycles = collect_cycle - submit_cycle
            if not (submit_cycle < completion_cycle <= collect_cycle):
                raise RuntimeError("display cycle order invalid: %d %d %d" %
                                   (submit_cycle, completion_cycle,
                                    collect_cycle))
            if present_cycles > PRESENT_BUDGET_CYCLES:
                raise RuntimeError("display present took %d cycles; budget %d" %
                                   (present_cycles, PRESENT_BUDGET_CYCLES))
            time.sleep(0.1)
            if machine.poll() is not None:
                raise RuntimeError("machine exited after display startup: %s" %
                                   serial[-8:])
            print("ASTRA DISPLAY PASS fence=1 generation=%d cycles=%d/%d "
                  "boot=%.3fs" % (generation, present_cycles,
                                   PRESENT_BUDGET_CYCLES, elapsed))
            return 0
        finally:
            machine.kill()
            machine.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    arguments = parser.parse_args()
    return run(arguments.qemu, arguments.rom, arguments.image,
               arguments.catalog, arguments.boot_deadline)


if __name__ == "__main__":
    sys.exit(main())
