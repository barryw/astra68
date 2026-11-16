#!/usr/bin/env python3
"""Filesystem semantic gate (docs/FILESYSTEM_PRODUCTION_PRD.md, Phase 1).

usage: test-fs-semantics.py QEMU ROM IMAGE [--case NAME]... [--check NAME]...
                            [--bench LABEL OUT.json] [--tree TREE]
                            [--prepared] [--no-checks]

Boots a copy of IMAGE with `fssem` installed. The provider root is a
Linux-mounted ext4 image; QEMU itself runs as the caller, never as root
(FS11). This gate opens a terminal the way a person does and runs
`fssem` over the selected cases (all of CASES by default). Then it runs
the selected orchestrated CHECKS (all by default). After the machine
stops, it runs `e2fsck -fn` on the provider image.

--bench runs `fsbench --label LABEL` instead and copies its result to
OUT.json: the QEMU-on-beast record that per-task comparisons use. It boots
the image's own libraries and commands without installing this tree's ABI.
Use --tree for that image's source tree so trace strings match its binaries.
"""

import argparse
import importlib.util
import json
import os
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import astra_image  # noqa: E402
import qemu_runtime  # noqa: E402


def load(name, file):
    spec = importlib.util.spec_from_file_location(
        name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


terminal = load("astra_terminal_gate", "test-terminal.py")

# Every fssem case the suite runs. Later tasks append here and in fssem.c.
CASES = ["basic", "durable-save", "volume-catalog", "volume-root", "raw-lane", "lane-fork-exec", "raw-readonly",
         "raw-cross-grant", "raw-subroot", "raw-delegate", "raw-retire",
         "raw-onward", "raw-orphans", "raw-orphans-death",
         "symlink-in-volume", "symlink-cross-volume", "dotdot-at-root",
         "escape-relative", "link-edges", "alias-identity", "continuation-limit", "path-boundaries",
         "shared-description", "anonymous-map", "partial-map", "shared-map", "shared-posix", "mapped-faults", "unmap-errors", "mapped-usage", "file-map", "map-publication", "page-writeback", "cache-pressure", "description-io", "scoped-io", "area-range", "exec-inherit", "append-race", "file-size-limit", "cwd-identity",
         "many-lanes", "owner-sessions", "owner-files", "byte-prefix", "generation-exhaustion"]
# Orchestrated checks: name -> function(machine, provider_mount). Later tasks
# add entries; each prints "PASS <name>" or calls fail().
CHECKS = {}


def fail(text):
    print("FAIL %s" % text)
    sys.exit(1)


def command(machine, text, timeout, number):
    """Types `text` and returns (lines printed, exit status)."""
    marker = "FSSEM-GATE-%u-" % number
    print("RUN %s" % text, flush=True)
    machine.settle()
    before = machine.sequence()
    started = before
    machine.qmp.type_line(text + "; print -r -- " + marker + "$?")
    deadline = time.monotonic() + timeout
    output = []
    while time.monotonic() < deadline:
        lines, before = machine.said(before)
        output.extend(lines)
        for line in lines:
            if line.startswith("FSSEM "):
                print(line, flush=True)
            if line.startswith(marker) and line[len(marker):].isdigit():
                if terminal.wait_for_command(machine, line,
                        max(0.0, deadline - time.monotonic()), started,
                        exact=True) is None:
                    fail("%r finished without the next shell prompt" % text)
                return output, int(line[len(marker):])
        time.sleep(0.1)
    for line in machine.trace_with_syslog()[-120:]:
        print(line)
    fail("%r never finished: %r" % (text, output))


def run_cases(machine, cases):
    lines, status = command(machine, "fssem " + " ".join(cases), 600, 1)
    done = [line for line in lines if line.startswith("FSSEM DONE")]
    if status != 0 or done != ["FSSEM DONE passed %d failed 0" % len(cases)]:
        print("recent guest trace:")
        for line in machine.trace_with_syslog()[-120:]:
            print(line)
        fail("fssem: status %d, %r" % (status, done))
    print("PASS fssem %s" % " ".join(cases))


def check_external_mapping(machine, mount):
    """Only the host changes EOF; the guest retains no target descriptor."""
    directory = tempfile.mkdtemp(prefix="external-map-", dir=os.path.join(mount, "work"))
    data = os.path.join(directory, "data")
    with open(data, "wb") as output:
        output.write(b"a" * 8192)
    _, uid, gid = struct.unpack("3i", machine.qmp.socket.getsockopt(
        socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
    if os.geteuid() == 0:
        os.chown(directory, uid, gid)
        os.chown(data, uid, gid)
    stopped = threading.Event()
    errors = []

    def mutate():
        try:
            started = time.monotonic()
            with open(data, "r+b", buffering=0) as output:
                for marker, length in (("ready", 4105), ("partial", 1), ("eof", None)):
                    deadline = time.monotonic() + 30
                    while not os.path.exists(os.path.join(directory, marker)):
                        if stopped.wait(0.01):
                            return
                        if time.monotonic() >= deadline:
                            raise RuntimeError("external mapping handshake: " + marker)
                    if length is None:
                        assert os.pwrite(output.fileno(), b"g", 4096) == 1
                    else:
                        output.truncate(length)
                    os.fsync(output.fileno())
                    with open(os.path.join(directory, marker + ".done"), "xb"):
                        pass
                    print("EXTERNAL_MAPPING_HOST", marker, "committed",
                          "elapsed=%.3fs" % (time.monotonic() - started), flush=True)
        except Exception as error:
            errors.append(error)

    # The observer never reads QMP alongside command's trace reader.
    worker = threading.Thread(target=mutate)
    worker.start()
    try:
        lines, status = command(machine,
            "fssem --external-map /work/" + os.path.basename(directory), 120, 76)
        worker.join(1)
        assert not worker.is_alive() and not errors, errors
        assert status == 0 and \
            "FSSEM EXTERNAL MAP idle-owner partial-tail full-eof growth PASS" in lines
    finally:
        stopped.set()
        worker.join()
    shutil.rmtree(directory)
    print("PASS external-mapping idle owner follows host EOF without target I/O", flush=True)


CHECKS["external-mapping"] = check_external_mapping


def check_shared_mapping(machine, mount, case="shared-map"):
    """Read backing through the host after native mapped-write sync completes."""
    work = os.path.join(mount, "work")
    before = set(os.listdir(work))
    assert case in ("shared-map", "shared-posix"), case
    run_cases(machine, [case])
    created = set(os.listdir(work)) - before
    directories = [name for name in created if "-" + case + "-" in name]
    assert len(directories) == 1, directories
    with open(os.path.join(work, directories[0], "data"), "rb") as source:
        expected = bytearray(b"a" * (8192 + 9))
        for offset, value in ((0, b"p"), (4, b"b"), (4096, b"c"),
                              (8193, b"t"), (8200, b"y")):
            expected[offset:offset + 1] = value
        assert source.read(len(expected)) == expected, "mapped bytes differ in backing file"
        assert source.read(8) == bytes(8), "old partial EOF tail was persisted on growth"
        source.seek(128 * 1024 * 1024 - 1)
        assert source.read() == b"xy" + bytes(7) + b"z", "cross-window bytes or EOF differ"
    if case == "shared-posix":
        expected = b"ASYNC" + bytes(4096 + 9 - 6) + b"!"
        asynchronous = os.path.join(work, directories[0], "async-data")
        started = time.monotonic()
        deadline = started + 30
        while True:
            with open(asynchronous, "rb") as source:
                if source.read() == expected:
                    break
            assert time.monotonic() < deadline, "async mapped bytes not persisted after final unmap"
            time.sleep(0.05)
        print("ASYNC_MAPPING_HOST persisted elapsed=%.3fs" % (time.monotonic() - started), flush=True)
    print("PASS " + case + " writes persist through native retained writer", flush=True)


CHECKS["shared-mapping"] = check_shared_mapping


def run_bench(machine, mount, label, output, tree=ROOT):
    lines, status = command(machine, "fsbench --label %s" % label, 3600, 2)
    if status != 0 or "fsbench: done %s" % label not in lines:
        fail("fsbench: status %d, %r" % (status, lines[-10:]))
    with open(os.path.join(mount, "work", ".astra",
                           "fsbench-%s.json" % label)) as source:
        guest = json.load(source)
    commit = subprocess.run(["git", "-C", tree, "rev-parse", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
    with open(output, "w") as out:
        json.dump({"identity": {"host": "beast-qemu", "tree": tree, "commit": commit},
                   "guest": guest}, out, indent=1, sort_keys=True)
    print("PASS bench %s -> %s" % (label, output))


def native_requests(machine, number):
    lines, status = command(machine, "metrics", 60, number)
    values = [line.split()[1:] for line in lines
              if line.startswith("hostfs.vfs.work.requests ")]
    if status != 0 or len(values) != 1 or len(values[0]) != 1 or \
            not values[0][0].isdigit():
        fail("missing native WORK request accounting: %r" % values)
    return int(values[0][0])


# fssem --stat-loop issues LANE_STATS stats; at least LANE_FLOOR native
# requests must remain once the machine's own background is taken away.
LANE_STATS = 500
LANE_FLOOR = 400


def check_lane(machine, mount):
    """The native service is the path taken (FS44): `fssem --stat-loop`
    makes LANE_STATS stats of /work, each at least one native VFS request. An
    idle window of about the same length measures the rest of the machine,
    and what the stats moved less that background must still reach
    LANE_FLOOR (re-review N4): background cannot satisfy the check. The
    100-stat margin covers a window that is not exactly the stat loop's.
    raw-lane must not obtain provider authority or mutate SOUND."""
    before = native_requests(machine, 62)
    started = time.monotonic()
    lines, status = command(machine, "fssem --stat-loop %d /work" %
                            LANE_STATS, 300, 61)
    elapsed = time.monotonic() - started
    if status != 0:
        fail("fssem --stat-loop: status %d, %r" % (status, lines[-5:]))
    moved = native_requests(machine, 63) - before
    idle_before = native_requests(machine, 64)
    time.sleep(elapsed)
    idle = native_requests(machine, 65) - idle_before
    if moved - idle < LANE_FLOOR:
        fail("only %d native requests (idle %d) for %d stats" %
             (moved, idle, LANE_STATS))
    if os.path.exists(os.path.join(mount, "sound", "raw-lane-forged")):
        fail("a forged volume reached SOUND")
    print("PASS native lane %d for %d stats (idle %d in %.1fs)" %
          (moved, LANE_STATS, idle, elapsed))


CHECKS["lane"] = check_lane


# Fault injection needs a user-controllable HostFS, with normal restart policy.
_manifest_lines = astra_image.STARTUP_MANIFEST.splitlines()
assert _manifest_lines[0].startswith("service /services/hostfs ")
_manifest_lines[0] = _manifest_lines[0].replace(" critical priority=20", " priority=20")
MANIFEST = "\n".join(_manifest_lines) + "\n"
HOSTFS_KILL = ("ps | while read -r id rest; do "
               "[[ $rest == *' /services/hostfs' ]] && "
               "print kill > /proc/$id/ctl; done")
FINAL_CHECKS = ("hostfs-kill",)


def loss(machine, mode, act):
    before = machine.trace_cursor()
    path = "/work/loss-%s-%x" % (mode, time.monotonic_ns())
    machine.qmp.type_line("fssem --loss-probe %s 120 %s &" % (path, mode))
    if not machine.wait_for_trace_text("FSSEM LOSS armed", 60, before):
        fail("loss probe never armed: %r" % machine.trace_with_syslog()[-20:])
    machine.settle()
    act()
    deadline = time.monotonic() + 180
    while time.monotonic() < deadline:
        for marker in ("FSSEM LOSS FAIL", "FSSEM LOSS none", "FSSEM LOSS PASS"):
            if not machine.wait_for_trace_text(marker, 0, before):
                continue
            if marker == "FSSEM LOSS PASS":
                print(marker)
                return
            for trace in machine.trace_with_syslog()[-120:]:
                print(trace)
            fail("loss probe (%s): %s" % (mode, marker))
        time.sleep(0.1)
    for line in machine.trace_with_syslog():
        print(line)
    fail("loss probe (%s) timed out" % mode)


def check_provider_reset(machine, mount):
    loss(machine, "reopen", lambda: machine.qmp.execute("qom-set", {
        "path": "/machine", "property": "astra-host-fs-reset", "value": True}))
    run_cases(machine, ("basic", "file-map"))
    print("PASS provider-reset")


def wait_for_hostfs_recovery(machine, mount, cursor, timeout):
    if not machine.wait_for_trace_text(terminal.DESKTOP_READY, timeout, cursor):
        fail("desktop did not regain its namespace after HostFS restart")
    deadline = time.monotonic() + timeout
    sample = os.path.join(mount, "work", ".astra", "sample")

    def sample_stamp():
        try:
            return os.stat(sample).st_mtime_ns
        except FileNotFoundError:
            return None

    previous = sample_stamp()
    while True:
        current = sample_stamp()
        if current is not None and current != previous:
            return
        if time.monotonic() > deadline:
            fail("sampler did not regain WORK after HostFS restart: %r" %
                 machine.trace_with_syslog()[-50:])
        time.sleep(0.5)


def check_durability(machine, mount):
    path = "/work/fs14-ack"
    payload = "ack-68040"
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("posix --durability-gated %s %s; print DUR-EXIT-$?" %
                          (path, payload))
    if machine.wait_for_text("ASTRA DURABILITY READY", 60, before)[0] is None:
        lines = machine.said(before)[0]
        fail("POSIX durability probe did not arm: %r ... %r" % (lines[:20], lines[-20:]))
    machine.qmp.key("ret")
    if machine.wait_for_text("ASTRA DURABILITY SYNCED", 60, before)[0] is None:
        fail("file and parent directory did not acknowledge sync")
    machine.qmp.execute("qom-set", {"path": "/machine",
        "property": "astra-host-fs-reset", "value": True})
    machine.qmp.key("ret")
    if machine.wait_for_text("DUR-EXIT-88", 90, before)[0] is None:
        fail("stale post-ack fsync did not report failure")
    lines = machine.said(before)[0]
    if not any("durability post-sync barrier" in line for line in lines) or \
            any(line == "ASTRA DURABILITY PASS" for line in lines):
        fail("post-ack fsync error was not distinguished: %r" % lines[-20:])
    print("PASS durability post-ack sync loss reported failure")
    check = "posix --durability-check %s %s" % (path, payload)
    lines, status = command(machine, check, 90, 32)
    if status != 0 or "ASTRA DURABILITY EXACT" not in lines:
        fail("acknowledged bytes after provider reset: %r" % lines[-20:])
    print("PASS durability acknowledged bytes after provider reset")
    before = machine.trace_cursor()
    machine.qmp.type_line(HOSTFS_KILL)
    wait_for_hostfs_recovery(machine, mount, before, 90)
    if not terminal.open_terminal_window(machine, 90):
        fail("no Terminal after HostFS restart in durability probe")
    lines, status = command(machine, check, 90, 33)
    if status != 0 or "ASTRA DURABILITY EXACT" not in lines:
        fail("acknowledged bytes after HostFS death: %r" % lines[-20:])
    print("PASS durability POSIX file/parent barriers "
          "sync-loss refusal provider-death acknowledged readback")


def check_hostfs_kill(machine, mount):
    killed_cursor = None
    def kill_now():
        nonlocal killed_cursor
        killed_cursor = machine.trace_cursor()
        machine.qmp.type_line(HOSTFS_KILL)
    loss(machine, "dead", kill_now)
    wait_for_hostfs_recovery(machine, mount, killed_cursor, 90)
    if not terminal.open_terminal_window(machine, 60):
        fail("no fresh terminal after HostFS restart")
    lines, status = command(machine, "print -r -- alive > /work/after-kill && cat /work/after-kill", 60, 41)
    if status != 0 or "alive" not in lines:
        fail("WORK after HostFS restart: %r" % lines[-20:])
    print("PASS hostfs-kill")


CHECKS["provider-reset"] = check_provider_reset
CHECKS["durability"] = check_durability
CHECKS["hostfs-kill"] = check_hostfs_kill

GAUGES = ("fs-descriptors", "fs-directories", "fs-objects", "rss-kib", "fds")


def gauges(machine):
    return {name: machine.qmp.execute("qom-get", {"path": "/machine",
        "property": "astra-host-" + name}) for name in GAUGES}


def fixture_descriptors(machine, path, include_path=False):
    """Independent Linux oracle for this fixture's exact retained inode.

    QMP peer credentials identify the emulator rather than its privileged
    launcher. A global descriptor delta cannot attribute a guest operation
    while the sampler/event store opens and closes unrelated files.
    """
    pid, uid, gid = struct.unpack("3i", machine.qmp.socket.getsockopt(
        socket.SOL_SOCKET, socket.SO_PEERCRED, struct.calcsize("3i")))
    if pid <= 0 or uid <= 0 or gid <= 0:
        fail("descriptor oracle requires the authenticated non-root QEMU peer")
    try:
        expected = os.stat(path, follow_symlinks=False)
    except FileNotFoundError:
        return 0
    count = 0
    for name in os.listdir("/proc/%d/fd" % pid):
        try:
            info = os.stat("/proc/%d/fd/%s" % (pid, name))
            if (info.st_dev, info.st_ino) != (expected.st_dev, expected.st_ino):
                continue
            with open("/proc/%d/fdinfo/%s" % (pid, name)) as stream:
                flags = next(int(line.split()[1], 8) for line in stream if line.startswith("flags:"))
            if include_path or not flags & os.O_PATH:
                count += 1
        except FileNotFoundError:
            continue  # an unrelated descriptor closed during the observation
    return count


def check_budgets(machine, mount):
    record = {}
    for kind, count in (("file", 1000), ("dir", 200)):
        machine.settle()
        target = os.path.join(mount, "work", ".astra", "fsbench-hold-file") if kind == "file" else \
            os.path.join(mount, "work", ".astra")
        fixture_before = fixture_descriptors(machine, target)
        before = gauges(machine)
        start = machine.sequence()
        machine.qmp.type_line("fsbench --hold %s %d" % (kind, count))
        if machine.wait_for_text("fsbench: holding", 180, start)[0] is None:
            fail("budget hold did not start: %r" % machine.trace_with_syslog()[-50:])
        during = gauges(machine)
        fixture_during = fixture_descriptors(machine, target)
        marker = os.path.join(mount, "work", ".astra", "fsbench-hold-%s.ready" % kind)
        os.unlink(marker)
        if machine.wait_for_text("fsbench: hold done", 120, start)[0] is None:
            fail("budget hold did not cleanly finish")
        machine.settle()
        after = gauges(machine)
        fixture_after = fixture_descriptors(machine, target)
        if fixture_during - fixture_before != count:
            fail("%s fixture descriptor accounting: %d -> %d, expected %d" %
                 (kind, fixture_before, fixture_during, count))
        if fixture_after != fixture_before:
            fail("%s fixture leaked descriptors: %d -> %d" % (kind, fixture_before, fixture_after))
        record[kind] = dict(count=count, before=before, during=during, after=after,
            fixture=dict(before=fixture_before, during=fixture_during, after=fixture_after),
            rss_kib_per_object=max(0, during["rss-kib"] - before["rss-kib"]) / count)
    machine.settle()
    before = gauges(machine)
    target = os.path.join(mount, "work")
    fixture_before = fixture_descriptors(machine, target, include_path=True)
    start = machine.sequence()
    machine.qmp.type_line("fssem --hold-sessions 16")
    if machine.wait_for_text("FSSEM SESSIONS holding 16", 120, start)[0] is None:
        fail("control-session hold did not start")
    during = gauges(machine)
    fixture_during = fixture_descriptors(machine, target, include_path=True)
    os.unlink(os.path.join(mount, "work", ".astra", "fssem-sessions.ready"))
    if machine.wait_for_text("FSSEM SESSIONS done", 120, start)[0] is None:
        fail("control-session hold did not finish")
    machine.settle()
    after = gauges(machine)
    fixture_after = fixture_descriptors(machine, target, include_path=True)
    if fixture_during != fixture_before or fixture_after != fixture_before:
        fail("control sessions opened/leaked provider descriptors")
    record["session"] = dict(count=16, before=before, during=during, after=after,
        fixture=dict(before=fixture_before, during=fixture_during, after=fixture_after))
    print("PASS budgets " + json.dumps(record, sort_keys=True))


def check_flood(machine, mount):
    lines, status = command(machine, "mkdir /work/flood", 30, 50)
    if status != 0:
        fail("flood fixture: %r" % lines)
    start = machine.sequence()
    machine.qmp.type_line("fssem --flood /work/flood 1024 &")
    if machine.wait_for_text("FSSEM FLOOD HOLD count=1024", 180, start)[0] is None:
        fail("flood did not reach quota: %r" % machine.trace_with_syslog()[-50:])
    lines, status = command(machine, "ls /work", 30, 51)
    if status != 0:
        fail("shell stopped under quota: %r" % lines)
    if not terminal.open_terminal_window(machine, 60):
        fail("desktop could not open terminal under quota")
    machine.qmp.execute("qom-set", {"path": "/machine",
        "property": "astra-host-fs-reset", "value": True})
    with open(os.path.join(mount, "work", "flood", "go"), "wb"):
        pass
    if machine.wait_for_text("FSSEM FLOOD PASS", 120, start)[0] is None:
        fail("quota reset/recovery failed: %r" % machine.trace_with_syslog()[-80:])
    lines, status = command(machine, "fssem basic", 120, 52)
    if status != 0:
        fail("filesystem after quota reset: %r" % lines)
    print("PASS flood full-quota reset old-handles-lost shell desktop recovery")


CHECKS["budgets"] = check_budgets
CHECKS["flood"] = check_flood

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--check", action="append", choices=sorted(CHECKS))
    parser.add_argument("--bench", nargs=2, metavar=("LABEL", "OUT"))
    parser.add_argument("--tree", default=ROOT,
                        help="source tree supplying this image's trace catalogs")
    parser.add_argument("--prepared", action="store_true",
                        help="IMAGE already contains current system products")
    parser.add_argument("--no-checks", action="store_true",
                        help="run selected fssem cases without orchestrated checks")
    arguments = parser.parse_args()
    if arguments.no_checks and arguments.check:
        parser.error("--no-checks cannot be combined with --check")
    selected_checks = () if arguments.no_checks else (arguments.check or CHECKS)
    # fssem is a TEST_COMMAND: "make all" never builds it, so it is built
    # directly here before being installed, the same as every other gate's
    # test command (test-fpu.py's fpucheck, test-system-log.py's logburst).
    if not arguments.bench:
        targets = ["build/m68k/fssem"]
        if "durability" in selected_checks:
            targets.append("build/m68k/posix")
        built = subprocess.run(
            astra_image._build_invocation(["make", "-C", os.path.join(ROOT, "sw", "userspace", "commands"),
             *targets]),
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if built.returncode != 0:
            fail("could not build filesystem test commands: %s" % built.stdout[-400:].decode(
                "utf-8", "replace"))
    work = tempfile.mkdtemp(prefix="astra-fs-sem-", dir="/tmp")
    try:
        image = os.path.join(work, "card.img")
        shutil.copyfile(arguments.image, image)
        if not arguments.bench:
            if not arguments.prepared:
                astra_image.install(image)
            astra_image.install_test_command(image, "fssem")
            if "durability" in selected_checks:
                astra_image.install_test_command(image, "posix")
        if not arguments.bench and {"hostfs-kill", "durability"} & set(selected_checks):
            if MANIFEST == astra_image.STARTUP_MANIFEST:
                fail("HostFS startup manifest changed")
            manifest = os.path.join(work, "startup")
            with open(manifest, "w", encoding="ascii", newline="\n") as out:
                out.write(MANIFEST)
            astra_image._replace_volume_file(image, manifest, "/startup/system")
        run = tempfile.mkdtemp(prefix="astra-fs-run-", dir="/tmp")
        machine = terminal.Machine(arguments.qemu, arguments.rom, image, run,
                                   source_root=arguments.tree)
        mount = machine.reader.hostfs_root
        try:
            if not terminal.open_terminal(machine, 120, 60):
                fail("no terminal")
            if arguments.bench:
                run_bench(machine, mount, *arguments.bench, tree=arguments.tree)
            else:
                run_cases(machine, arguments.case or CASES)
                for name in sorted(selected_checks, key=lambda name: (name in FINAL_CHECKS, name)):
                    CHECKS[name](machine, mount)
        except BaseException:
            print("native failure syslog:")
            for line in machine.syslog_lines():
                if " error " in line or "FSSEM" in line:
                    print(line)
            print("native failure serial:")
            for line in machine.log[-250:]:
                print(line)
            if machine.process.poll() is None:
                try:
                    print("native kernel failure trace:")
                    for line in machine.trace()[-500:]:
                        print(line)
                    print("native failure trace:")
                    for line in machine.trace_with_syslog()[-250:]:
                        print(line)
                except (OSError, EOFError, RuntimeError) as error:
                    print("native trace unavailable:", error)
            raise
        finally:
            machine.close()
            shutil.rmtree(run, ignore_errors=True)
        # close() returns once the helper has written the image back; a
        # non-zero exit means the write-back may not have happened.
        if machine.process.returncode != 0:
            fail("launcher exited %d" % machine.process.returncode)
        offset, length = astra_image.ext4_partition(image)
        volume = os.path.join(work, "system.ext4")
        astra_image._slice(image, offset, length, volume)
        checked = subprocess.run(["/usr/sbin/e2fsck", "-fn", volume],
                                 capture_output=True, text=True)
        if checked.returncode != 0:
            fail("provider e2fsck: %s" % checked.stdout[-400:])
        print("PASS provider e2fsck")
        print("ASTRA FS SEMANTICS PASS")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
