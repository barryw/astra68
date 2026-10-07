#!/usr/bin/env python3
"""DE25 filesystem baseline (docs/FILESYSTEM_PRODUCTION_PRD.md).

usage: fs-baseline.py LABEL OUT.json [--restart] [--no-cold]

Runs on the board, as root, from the release's bin directory:
  - with --restart, restarts astra.service and waits for the remote desktop;
  - opens Terminal by double-clicking its icon (the desktop logs the icon's
    rectangle as "desktop icon /apps/Terminal.app L T W H");
  - types `fsbench --label LABEL --cold-handshake`;
  - at each cold marker drops the Linux page cache and removes the marker;
  - samples QEMU's RSS, descriptor count and CPU time before and after;
  - writes {"identity", "host", "guest"} to OUT.json.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
STORE = os.environ.get("ASTRA_STORE", "/var/lib/astra")
WORK = os.path.join(STORE, "hostfs", "work")
ASTRA = os.path.join(WORK, ".astra")
PASSWORD = "/etc/astra/remote-desktop.password"
ICON = re.compile(r"desktop icon /apps/Terminal\.app (\d+) (\d+) (\d+) (\d+)$")
QEMU_NAME = "qemu-system-m68k-astra"


def rfb(*arguments):
    frame = os.path.join("/tmp", "fs-baseline-frame.raw")
    return subprocess.run(
        [sys.executable, os.path.join(HERE, "de25-rfb.py"), frame,
         "--password-file", PASSWORD] + list(arguments),
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def syslog():
    text = ""
    for name in ("syslog.3", "syslog.2", "syslog.1", "syslog"):
        path = os.path.join(ASTRA, name)
        if os.path.exists(path):
            with open(path, encoding="utf-8", errors="replace") as source:
                text += source.read()
    return text


def terminal_icon(deadline):
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        for line in reversed(syslog().splitlines()):
            match = ICON.search(line)
            if match:
                left, top, width, height = (int(v) for v in match.groups())
                return left + width // 2, top + height // 2
        time.sleep(1.0)
    raise SystemExit("fs-baseline: the desktop never logged Terminal's icon")


def qemu_pid():
    output = subprocess.run(["pidof", QEMU_NAME], capture_output=True,
                            text=True).stdout.split()
    if len(output) != 1:
        raise SystemExit("fs-baseline: expected one %s, found %r" %
                         (QEMU_NAME, output))
    return int(output[0])


def host_sample(pid):
    with open("/proc/%d/status" % pid) as source:
        status = dict(line.split(":", 1) for line in source if ":" in line)
    with open("/proc/%d/stat" % pid) as source:
        fields = source.read().rsplit(")", 1)[1].split()
    ticks = os.sysconf("SC_CLK_TCK")
    return {
        "rss_kib": int(status["VmRSS"].split()[0]),
        "fds": len(os.listdir("/proc/%d/fd" % pid)),
        "threads": int(status["Threads"].split()[0]),
        # utime and stime are fields 14 and 15 of /proc/PID/stat, which are
        # indices 11 and 12 once the "pid (comm)" prefix is removed.
        "cpu_s": (int(fields[11]) + int(fields[12])) / ticks,
        "mem_available_kib": int(open("/proc/meminfo").read().split(
            "MemAvailable:")[1].split()[0]),
    }


def identity():
    current = os.path.realpath(os.path.join(STORE, "current"))
    result = {"release": os.path.basename(current)}
    for name in ("artifacts.sha256", "source/SOURCE_SHA256SUMS"):
        path = os.path.join(current, name)
        with open(path, "rb") as source:
            result[name] = hashlib.sha256(source.read()).hexdigest()
    result["kernel"] = os.uname().release
    return result


def drop_caches():
    subprocess.run(["sync"], check=True)
    with open("/proc/sys/vm/drop_caches", "w") as control:
        control.write("3\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("label")
    parser.add_argument("output")
    parser.add_argument("--restart", action="store_true")
    parser.add_argument("--no-cold", action="store_true")
    arguments = parser.parse_args()
    if os.geteuid() != 0:
        raise SystemExit("fs-baseline: run as root on the board")
    if arguments.restart:
        subprocess.run(["systemctl", "restart", "astra.service"], check=True)
        for _ in range(90):
            if rfb():
                break
            time.sleep(2)
        else:
            raise SystemExit("fs-baseline: the remote desktop never answered")
        time.sleep(20)
    x, y = terminal_icon(60)
    if not rfb("--double-click", str(x), str(y)):
        raise SystemExit("fs-baseline: double click failed")
    time.sleep(15)
    pid = qemu_pid()
    before = host_sample(pid)
    result_path = os.path.join(ASTRA, "fsbench-%s.json" % arguments.label)
    cold_path = os.path.join(ASTRA, "fsbench-%s.cold" % arguments.label)
    command = "fsbench --label %s%s\n" % (
        arguments.label, "" if arguments.no_cold else " --cold-handshake")
    if not rfb("--keys", command):
        raise SystemExit("fs-baseline: typing failed")
    deadline = time.monotonic() + 1800
    peak = dict(before)
    while not os.path.exists(result_path):
        if time.monotonic() > deadline:
            raise SystemExit("fs-baseline: fsbench never finished")
        if os.path.exists(cold_path):
            drop_caches()
            os.unlink(cold_path)
        current = host_sample(pid)
        peak["rss_kib"] = max(peak["rss_kib"], current["rss_kib"])
        peak["fds"] = max(peak["fds"], current["fds"])
        time.sleep(0.2)
    after = host_sample(pid)
    with open(result_path) as source:
        guest = json.load(source)
    record = {"identity": identity(),
              "host": {"before": before, "after": after, "peak": peak,
                       "cpu_s": after["cpu_s"] - before["cpu_s"]},
              "guest": guest}
    with open(arguments.output, "w") as output:
        json.dump(record, output, indent=1, sort_keys=True)
    print("fs-baseline: wrote %s" % arguments.output)


if __name__ == "__main__":
    main()
