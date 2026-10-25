#!/usr/bin/env python3
"""DE25 filesystem baseline (docs/FILESYSTEM_PRODUCTION_PRD.md).

usage: fs-baseline.py LABEL OUT.json [--restart] [--no-cold]
                      [--held-miss-floor MB_S]

Runs on the board, as root, from the release's bin directory:
  - with --restart, restarts astra.service and waits for the remote desktop;
  - opens Applications drawer, then Terminal in Atlas; both log icon bounds;
  - types `fsbench --label LABEL --cold-handshake`;
  - at the shared cold marker, drops the whole Linux page cache;
  - at held-miss's own marker (fsbench-LABEL.heldmiss.cold), evicts only
    `big2`'s host file with posix_fadvise(DONTNEED), leaving `big` -- and
    everything else fsbench wrote -- warm. held-miss measures cached reads
    of `big` under a concurrently cold `big2`; a global drop here would
    evict `big` too and contaminate the "cached" half of that measurement
    (it showed up as held-miss's p95 running ~4x its median);
  - samples QEMU's RSS, descriptor count and CPU time before and after;
  - writes {"identity", "host", "guest", "checks"} to OUT.json, always --
    before anything below can fail the run, so no workload's data is ever
    discarded;
  - then checks held-miss's own throughput as a regression guard (floor
    --held-miss-floor MB/s, default 20.0, 0 disables) and exits non-zero on
    a miss, after OUT.json is already on disk.
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
DRAWER_ICON = re.compile(r"desktop icon /apps (\d+) (\d+) (\d+) (\d+)$")
TERMINAL_ICON = re.compile(r"atlas icon /apps/Terminal\.app (\d+) (\d+) (\d+) (\d+)$")
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


def icon(pattern, deadline, after=0):
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        for line in reversed(syslog().splitlines()[after:]):
            match = pattern.search(line)
            if match:
                left, top, width, height = (int(v) for v in match.groups())
                return left + width // 2, top + height // 2
        time.sleep(1.0)
    raise SystemExit("fs-baseline: expected icon was not logged")


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


def evict_file(path):
    """Drop one file's pages from the host page cache, not the whole
    cache -- so held-miss's `big` stays warm while `big2` goes cold."""
    fd = os.open(path, os.O_RDONLY)
    try:
        os.fsync(fd)
        os.posix_fadvise(fd, 0, 0, os.POSIX_FADV_DONTNEED)
    finally:
        os.close(fd)


def check_held_miss(guest, floor_mb_s):
    """held-miss is cached reads of `big` under a concurrently cold `big2`
    (docs/FILESYSTEM_PRODUCTION_PRD.md, "concurrent cached reads under a
    held miss"): its 200 cached 4 KiB reads should sustain real page-cache
    throughput. p95/median looked like the natural signal for `big` being
    evicted too, but it is not reliable -- five runs with the global-drop
    bug reintroduced (the three original pre-fix runs, plus two reproduced
    while diagnosing the fix) measured p95/median anywhere from 1.78x to
    4.03x, overlapping the ratios healthy runs produce under ordinary
    scheduling noise. Throughput separates them cleanly: the same five
    buggy runs measured 12.3-15.3 MB/s; a fixed run measured 27.2 MB/s.
    20 MB/s is the floor, with margin on both sides. --held-miss-floor
    overrides it; 0 disables the check.

    Returns a JSON-able record of the result -- it does not raise. The
    caller writes the whole benchmark record to disk first, regardless of
    this result, and only then decides whether to fail the run."""
    workload = guest.get("workloads", {}).get("held-miss")
    throughput = workload.get("throughput_bps") if workload else None
    mb_s = throughput / 1e6 if throughput else None
    ok = floor_mb_s <= 0 or mb_s is None or mb_s >= floor_mb_s
    return {"ok": ok, "mb_s": mb_s, "floor": floor_mb_s}


def write_record(record, guest, floor_mb_s, output_path):
    """Write the full benchmark record to output_path unconditionally,
    then apply the held-miss regression guard. On a miss this raises
    SystemExit, but only after the write above -- see check_held_miss."""
    record = dict(record, guest=guest)
    check = check_held_miss(guest, floor_mb_s)
    record["checks"] = {"held_miss_floor": check}
    with open(output_path, "w") as output:
        json.dump(record, output, indent=1, sort_keys=True)
    print("fs-baseline: wrote %s" % output_path)
    if not check["ok"]:
        raise SystemExit(
            "fs-baseline: held-miss throughput %.1f MB/s below floor "
            "%.1f MB/s: big may have been evicted (cold point), or the "
            "provider path slowed; record written to %s" %
            (check["mb_s"], check["floor"], output_path))


def budget(pid, output):
    record = {"identity": identity(), "holds": {}}
    for kind, count in (("file", 1000), ("dir", 200)):
        ready = os.path.join(ASTRA, "fsbench-hold-%s.ready" % kind)
        if os.path.exists(ready):
            raise SystemExit("fs-baseline: stale hold marker " + ready)
        before = host_sample(pid)
        if not rfb("--keys", "fsbench --hold %s %d\n" % (kind, count)):
            raise SystemExit("fs-baseline: typing failed")
        deadline = time.monotonic() + 180
        while not os.path.exists(ready):
            if time.monotonic() >= deadline:
                raise SystemExit("fs-baseline: hold never ready: " + kind)
            time.sleep(0.2)
        during = host_sample(pid)
        os.unlink(ready)
        deadline = time.monotonic() + 30
        while host_sample(pid)["fds"] > before["fds"] + 2:
            if time.monotonic() >= deadline:
                raise SystemExit("fs-baseline: hold leaked descriptors: " + kind)
            time.sleep(0.2)
        after = host_sample(pid)
        record["holds"][kind] = dict(count=count, before=before,
            during=during, after=after,
            rss_kib_per_object=max(0, during["rss_kib"] - before["rss_kib"]) / count,
            fds_per_object=(during["fds"] - before["fds"]) / count)
        with open(output, "w") as stream:
            json.dump(record, stream, indent=1, sort_keys=True)
    print("fs-baseline: wrote " + output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("label")
    parser.add_argument("output")
    parser.add_argument("--restart", action="store_true")
    parser.add_argument("--no-cold", action="store_true")
    parser.add_argument("--budget", action="store_true", help="measure held provider objects")
    parser.add_argument("--held-miss-floor", type=float, default=20.0,
                        metavar="MB_S", help="held-miss throughput floor "
                        "in MB/s regression guard (0 disables)")
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
    x, y = icon(DRAWER_ICON, 60)
    before = len(syslog().splitlines())
    if not rfb("--double-click", str(x), str(y)):
        raise SystemExit("fs-baseline: Applications drawer did not open")
    x, y = icon(TERMINAL_ICON, 60, before)
    if not rfb("--double-click", str(x), str(y)):
        raise SystemExit("fs-baseline: double click failed")
    time.sleep(15)
    pid = qemu_pid()
    if arguments.budget:
        budget(pid, arguments.output)
        return
    before = host_sample(pid)
    result_path = os.path.join(ASTRA, "fsbench-%s.json" % arguments.label)
    cold_path = os.path.join(ASTRA, "fsbench-%s.cold" % arguments.label)
    held_cold_path = os.path.join(
        ASTRA, "fsbench-%s.heldmiss.cold" % arguments.label)
    held_file = os.path.join(WORK, "fsbench-%s" % arguments.label, "big2")
    command = "fsbench --label %s%s\n" % (
        arguments.label, "" if arguments.no_cold else " --cold-handshake")
    if not rfb("--keys", command):
        raise SystemExit("fs-baseline: typing failed")
    deadline = time.monotonic() + 1800
    peak = dict(before)
    while not os.path.exists(result_path):
        if time.monotonic() > deadline:
            raise SystemExit("fs-baseline: fsbench never finished")
        if os.path.exists(held_cold_path):
            evict_file(held_file)
            os.unlink(held_cold_path)
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
                       "cpu_s": after["cpu_s"] - before["cpu_s"]}}
    write_record(record, guest, arguments.held_miss_floor, arguments.output)


if __name__ == "__main__":
    main()
