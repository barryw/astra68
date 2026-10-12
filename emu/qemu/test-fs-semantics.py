#!/usr/bin/env python3
"""Filesystem semantic gate (docs/FILESYSTEM_PRODUCTION_PRD.md, Phase 1).

usage: test-fs-semantics.py QEMU ROM IMAGE [--case NAME]... [--check NAME]...
                            [--bench LABEL OUT.json] [--tree TREE]

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
import subprocess
import sys
import tempfile
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
CASES = ["basic", "raw-lane", "lane-fork-exec", "raw-readonly",
         "raw-cross-grant", "raw-subroot", "raw-delegate", "raw-retire",
         "raw-onward", "raw-orphans", "raw-orphans-death",
         "symlink-in-volume", "symlink-cross-volume", "dotdot-at-root",
         "escape-relative", "link-edges", "alias-identity", "continuation-limit",
         "shared-description", "exec-inherit", "append-race", "file-size-limit", "cwd-identity",
         "many-lanes", "owner-sessions", "owner-files"]
# Orchestrated checks: name -> function(machine, provider_mount). Later tasks
# add entries; each prints "PASS <name>" or calls fail().
CHECKS = {}


def fail(text):
    print("FAIL %s" % text)
    sys.exit(1)


def command(machine, text, timeout, number):
    """Types `text` and returns (lines printed, exit status)."""
    marker = "FSSEM-GATE-%u-" % number
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line(text + "; print -r -- " + marker + "$?")
    deadline = time.monotonic() + timeout
    output = []
    while time.monotonic() < deadline:
        lines, before = machine.said(before)
        output.extend(lines)
        for line in lines:
            if line.startswith(marker) and line[len(marker):].isdigit():
                return output, int(line[len(marker):])
        time.sleep(0.1)
    for line in machine.trace_with_syslog()[-120:]:
        print(line)
    fail("%r never finished: %r" % (text, output))


def run_cases(machine, cases):
    lines, status = command(machine, "fssem " + " ".join(cases), 600, 1)
    done = [line for line in lines if line.startswith("FSSEM DONE")]
    for line in lines:
        if line.startswith("FSSEM "):
            print(line)
    if status != 0 or done != ["FSSEM DONE passed %d failed 0" % len(cases)]:
        print("recent guest trace:")
        for line in machine.trace_with_syslog()[-120:]:
            print(line)
        fail("fssem: status %d, %r" % (status, done))
    print("PASS fssem %s" % " ".join(cases))


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


def scoped_commands(machine):
    return machine.qmp.execute("qom-get", {
        "path": "/machine", "property": "astra-host-scoped-commands"})


# fssem --stat-loop issues LANE_STATS stats; at least LANE_FLOOR scoped
# commands must remain once the machine's own background is taken away.
LANE_STATS = 500
LANE_FLOOR = 400


def check_lane(machine, mount):
    """The lane is the path taken (FS44), attributably: `fssem --stat-loop`
    makes LANE_STATS stats of /work, each at least one scoped command. An
    idle window of about the same length measures the rest of the machine,
    and what the stats moved less that background must still reach
    LANE_FLOOR (re-review N4): background cannot satisfy the check. The
    100-stat margin covers a window that is not exactly the stat loop's.
    raw-lane's forged MKDIR must have left nothing in SOUND."""
    before = scoped_commands(machine)
    started = time.monotonic()
    lines, status = command(machine, "fssem --stat-loop %d /work" %
                            LANE_STATS, 300, 61)
    elapsed = time.monotonic() - started
    if status != 0:
        fail("fssem --stat-loop: status %d, %r" % (status, lines[-5:]))
    moved = scoped_commands(machine) - before
    idle_before = scoped_commands(machine)
    time.sleep(elapsed)
    idle = scoped_commands(machine) - idle_before
    if moved - idle < LANE_FLOOR:
        fail("only %d scoped commands (idle %d) for %d lane stats" %
             (moved, idle, LANE_STATS))
    if os.path.exists(os.path.join(mount, "sound", "raw-lane-forged")):
        fail("a forged volume reached SOUND")
    print("PASS lane %d for %d stats (idle %d in %.1fs)" %
          (moved, LANE_STATS, idle, elapsed))


CHECKS["lane"] = check_lane


# Fault injection needs a user-controllable HostFS, with normal restart policy.
MANIFEST = astra_image.STARTUP_MANIFEST.replace(
    "serves WORK:rw METRICS:r SOUND:rw required priority=20",
    "serves WORK:rw METRICS:r SOUND:rw priority=20")
FINAL_CHECKS = ("hostfs-kill",)


def loss(machine, mode, act):
    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line("fssem --loss-probe /work/loss-%s 120 %s &" % (mode, mode))
    if machine.wait_for_text("FSSEM LOSS armed", 60, before)[0] is None:
        fail("loss probe never armed: %r" % machine.said(before)[0][-20:])
    machine.settle()
    after = machine.sequence()
    act()
    deadline = time.monotonic() + 180
    observed = []
    while time.monotonic() < deadline:
        lines, after = machine.said(after)
        observed.extend(lines)
        for line in lines:
            if line.startswith("FSSEM LOSS PASS"):
                print(line)
                return
            if line.startswith(("FSSEM LOSS FAIL", "FSSEM LOSS none")):
                for trace in machine.trace_with_syslog()[-120:]:
                    print(trace)
                fail("loss probe (%s): %s" % (mode, line))
        time.sleep(0.1)
    for line in observed:
        print(line)
    for line in machine.trace_with_syslog():
        print(line)
    fail("loss probe (%s) timed out" % mode)


def check_provider_reset(machine, mount):
    loss(machine, "reopen", lambda: machine.qmp.execute("qom-set", {
        "path": "/machine", "property": "astra-host-fs-reset", "value": True}))
    lines, status = command(machine, "fssem basic", 120, 31)
    if status != 0:
        fail("fssem after provider reset: %r" % lines[-20:])
    print("PASS provider-reset")


def check_hostfs_kill(machine, mount):
    killed_at = time.time()
    kill = ("ps | while read -r id rest; do "
            "[[ $rest == *' /services/hostfs' ]] && "
            "print kill > /proc/$id/ctl; done")
    loss(machine, "dead", lambda: machine.qmp.type_line(kill))
    deadline = time.monotonic() + 60
    sample = os.path.join(mount, "work", ".astra", "sample")
    while not os.path.exists(sample) or os.stat(sample).st_mtime <= killed_at + 1.0:
        if time.monotonic() > deadline:
            fail("sampler did not regain WORK after HostFS restart: %r" % machine.trace_with_syslog()[-50:])
        time.sleep(0.5)
    before = machine.sequence()
    machine.qmp.double_click(*machine.desktop_icon())
    if machine.wait_for_text(terminal.BANNER, 60, before)[0] is None or not machine.wait_for_ready(60, before):
        fail("no fresh terminal after HostFS restart")
    lines, status = command(machine, "print -r -- alive > /work/after-kill && cat /work/after-kill", 60, 41)
    if status != 0 or "alive" not in lines:
        fail("WORK after HostFS restart: %r" % lines[-20:])
    print("PASS hostfs-kill")


CHECKS["provider-reset"] = check_provider_reset
CHECKS["hostfs-kill"] = check_hostfs_kill

GAUGES = ("fs-descriptors", "fs-directories", "fs-objects", "rss-kib", "fds")


def gauges(machine):
    return {name: machine.qmp.execute("qom-get", {"path": "/machine",
        "property": "astra-host-" + name}) for name in GAUGES}


def check_budgets(machine, mount):
    record = {}
    for kind, count in (("file", 1000), ("dir", 200)):
        machine.settle()
        before = gauges(machine)
        start = machine.sequence()
        machine.qmp.type_line("fsbench --hold %s %d" % (kind, count))
        if machine.wait_for_text("fsbench: holding", 180, start)[0] is None:
            fail("budget hold did not start: %r" % machine.trace_with_syslog()[-50:])
        during = gauges(machine)
        marker = os.path.join(mount, "work", ".astra", "fsbench-hold-%s.ready" % kind)
        os.unlink(marker)
        if machine.wait_for_text("fsbench: hold done", 120, start)[0] is None:
            fail("budget hold did not cleanly finish")
        machine.settle()
        after = gauges(machine)
        if during["fs-descriptors"] - before["fs-descriptors"] != count:
            fail("%s descriptor accounting: %r %r" % (kind, before, during))
        if after["fs-descriptors"] != before["fs-descriptors"] or after["fs-directories"] != before["fs-directories"]:
            fail("%s leaked descriptors: %r %r" % (kind, before, after))
        record[kind] = dict(count=count, before=before, during=during, after=after,
            rss_kib_per_object=max(0, during["rss-kib"] - before["rss-kib"]) / count)
    machine.settle()
    before = gauges(machine)
    start = machine.sequence()
    machine.qmp.type_line("fssem --hold-sessions 16")
    if machine.wait_for_text("FSSEM SESSIONS holding 16", 120, start)[0] is None:
        fail("control-session hold did not start")
    during = gauges(machine)
    os.unlink(os.path.join(mount, "work", ".astra", "fssem-sessions.ready"))
    if machine.wait_for_text("FSSEM SESSIONS done", 120, start)[0] is None:
        fail("control-session hold did not finish")
    machine.settle()
    after = gauges(machine)
    if during["fs-descriptors"] != before["fs-descriptors"] or after["fs-descriptors"] != before["fs-descriptors"]:
        fail("control sessions opened/leaked provider descriptors")
    record["session"] = dict(count=16, before=before, during=during, after=after)
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
    before = machine.sequence()
    machine.qmp.double_click(*machine.desktop_icon())
    if machine.wait_for_text(terminal.BANNER, 60, before)[0] is None or not machine.wait_for_ready(60, before):
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
    parser.add_argument("--bootstrap", choices=("match", "mismatch", "absent"), default="match")
    arguments = parser.parse_args()
    # fssem is a TEST_COMMAND: "make all" never builds it, so it is built
    # directly here before being installed, the same as every other gate's
    # test command (test-fpu.py's fpucheck, test-system-log.py's logburst).
    if not arguments.bench:
        built = subprocess.run(
            ["make", "-C", os.path.join(ROOT, "sw", "userspace", "commands"),
             "build/m68k/fssem"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if built.returncode != 0:
            fail("could not build fssem: %s" % built.stdout[-400:].decode(
                "utf-8", "replace"))
    work = tempfile.mkdtemp(prefix="astra-fs-sem-", dir="/tmp")
    try:
        image = os.path.join(work, "card.img")
        shutil.copyfile(arguments.image, image)
        if not arguments.bench:
            astra_image.install(image)
            astra_image.install_test_command(image, "fssem")
        if not arguments.bench and "hostfs-kill" in (arguments.check or CHECKS):
            if MANIFEST == astra_image.STARTUP_MANIFEST:
                fail("HostFS startup manifest changed")
            manifest = os.path.join(work, "startup")
            with open(manifest, "w", encoding="ascii", newline="\n") as out:
                out.write(MANIFEST)
            astra_image._replace_volume_file(image, manifest, "/startup/system")
        selected_manifest = MANIFEST if not arguments.bench and "hostfs-kill" in (arguments.check or CHECKS) else astra_image.STARTUP_MANIFEST
        bootstrap = None
        if not arguments.bench and arguments.bootstrap != "absent":
            bootstrap = os.path.join(work, "provider-startup")
            with open(bootstrap, "w", encoding="ascii", newline="\n") as stream:
                stream.write(selected_manifest if arguments.bootstrap == "match" else "mismatch\n")
        launcher, provider = qemu_runtime.root_provider(work, system=bootstrap)
        run = tempfile.mkdtemp(prefix="astra-fs-run-", dir="/tmp")
        machine = terminal.Machine(arguments.qemu, arguments.rom, image, run,
                                   launcher=launcher, source_root=arguments.tree)
        mount = machine.reader.hostfs_root
        try:
            if not terminal.open_terminal(machine, 120, 60):
                fail("no terminal")
            if arguments.bench:
                run_bench(machine, mount, *arguments.bench, tree=arguments.tree)
            else:
                verdict = "bootstrap-provider " + arguments.bootstrap
                if not any(verdict in line for line in machine.trace_with_syslog()):
                    for line in machine.trace_with_syslog():
                        print(line)
                    fail("missing bootstrap verdict " + verdict)
                if arguments.bootstrap == "match" and not any(
                        "bootstrap-provider match bytes=%d" % len(selected_manifest.encode("ascii")) in line
                        for line in machine.trace_with_syslog()):
                    fail("bootstrap compared wrong manifest bytes")
                print("PASS bootstrap " + arguments.bootstrap)
                run_cases(machine, arguments.case or CASES)
                for name in sorted(arguments.check or CHECKS, key=lambda name: (name in FINAL_CHECKS, name)):
                    CHECKS[name](machine, mount)
        finally:
            machine.close()
            shutil.rmtree(run, ignore_errors=True)
        # close() returns once the helper has written the image back; a
        # non-zero exit means the write-back may not have happened.
        if machine.process.returncode != 0:
            fail("launcher exited %d" % machine.process.returncode)
        checked = subprocess.run(["/usr/sbin/e2fsck", "-fn", provider],
                                 capture_output=True, text=True)
        if checked.returncode != 0:
            fail("provider e2fsck: %s" % checked.stdout[-400:])
        print("PASS provider e2fsck")
        print("ASTRA FS SEMANTICS PASS")
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    main()
