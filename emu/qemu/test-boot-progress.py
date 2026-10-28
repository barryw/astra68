#!/usr/bin/env python3
"""The boot progress record: monotonic, exact, and honest about failure.

Boots with the text page backed by a file, as run-arty.sh does on the DE25,
and samples the record the splash helper reads
(sw/include/astra/boot_progress.h) every 2 ms until the kernel prints
stage 8.

In every boot the 32-bit value never decreases, and the kernel's announced
service count equals what the image says boots: the manifest minus hostfs
and `trusted`, plus the enabled start=boot definitions under /config/services.

Only a required or critical service fails the boot; a user service that does
not start is skipped (supervisor_boot_service_result, loader.h).

Normal boot (a copy of BASE_IMAGE):
  - at stage 8 it is DONE: all 18 fixed steps, announced, done == count;
  - the services that did not start are exactly media and remote-desktop,
    which have no host under headless QEMU.
A user service that refuses READY (`astra_image.py --ready-fail`):
  - DONE at stage 8 as above, with fault-probe skipped as well.
A required service that cannot start (`astra_image.py --required-fail`):
  - the boot stops (the initial image exits, no stage 8), and the record is
    FAILED with every fixed step, and done stopping at that service.

Success prints one PASS line per scenario. None contains "FAIL": the beast
verify scripts grep for it.

Usage: test-boot-progress.py QEMU ROM BASE_IMAGE
"""

import argparse
import importlib.util
import mmap
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import time
from qemu_runtime import qemu_test_credentials

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import astra_image  # noqa: E402

SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)

PAGE_BYTES = 4096
HEADER_OFFSET = 0xFE0
VALUE_OFFSET = 0xFE4
HEADER = 0x42500001
BOOTING, DONE, FAILED = 0, 1, 2
SERVICES = re.compile(r"Boot services \.+ (\d+) to start")
LAUNCH_FAILED = re.compile(r"\blaunch-failed (/\S+)")
EXITED = "Initial image ....... EXITED"
# Host-backed user services that refuse READY under headless QEMU.
NO_HOST = {"/services/media", "/services/remote-desktop"}
BOOT_DEADLINE = 240.0


FIXED_STEPS = 18  # ASTRA_BOOT_PROGRESS_FIXED_STEPS (boot_progress.h)


def fields(value):
    """(state, announced, fixed, count, done) -- boot_progress.h's layout."""
    return (value >> 28, (value >> 27) & 1, (value >> 21) & 0x3F,
            (value >> 10) & 0x3FF, value & 0x3FF)


def stable_word(page, offset):
    """One word, read until two reads agree: never half of a store."""
    while True:
        first = struct.unpack_from(">I", page, offset)[0]
        if struct.unpack_from(">I", page, offset)[0] == first:
            return first


def expected_services(configuration,
                      manifest=astra_image.DISPLAY_STARTUP_MANIFEST):
    """What the supervisor must announce for an image built from these.

    Assumes no policy.conf start override under /config/services: the
    images astra_image.py builds have none, and the supervisor would skip a
    resident service the user set to start manually.
    """
    count = 0
    for line in manifest.splitlines():
        words = line.split()
        if (words and words[0] in ("service", "application") and
                words[1] != "/services/hostfs"):
            count += 1
    for path, text in configuration.items():
        if (path.startswith("services/") and path.endswith("/service.conf")
                and "\nenabled true\n" in text and "\nstart boot\n" in text):
            count += 1
    return count


class Sampler(threading.Thread):
    def __init__(self, page):
        super().__init__(daemon=True)
        self.page = page
        self.values = []
        self.stop = threading.Event()

    def run(self):
        while not self.stop.is_set():
            if stable_word(self.page, HEADER_OFFSET) == HEADER:
                value = stable_word(self.page, VALUE_OFFSET)
                if not self.values or self.values[-1] != value:
                    self.values.append(value)
            time.sleep(0.002)


def boot(qemu, rom, image, services, skipped=None, stops_after=None):
    """One boot. A clean one names the services it @p skipped; one that
    @p stops_after that many services must end FAILED without stage 8."""
    work = tempfile.mkdtemp(prefix="astra-boot-progress-")
    page_path = os.path.join(work, "post-text.bin")
    with open(page_path, "wb") as handle:
        handle.write(bytes(PAGE_BYTES))
    uid, gid = qemu_test_credentials()
    os.chown(work, uid, gid)
    os.chown(page_path, uid, gid)
    os.environ["ASTRA_TEXT_PLANE_PATH"] = page_path
    machine = None
    sampler = None
    page = None
    marker = "stage 8" if stops_after is None else EXITED
    try:
        machine = terminal.Machine(qemu, rom, image, work)
        with open(page_path, "rb") as handle:
            page = mmap.mmap(handle.fileno(), PAGE_BYTES,
                             access=mmap.ACCESS_READ)
        sampler = Sampler(page)
        sampler.start()
        if not machine.wait_for_serial(marker, BOOT_DEADLINE):
            raise RuntimeError("no %r:\n" % marker +
                               "\n".join(machine.recent_serial(60)))
        time.sleep(0.05)
        sampler.stop.set()
        sampler.join()
        header = stable_word(page, HEADER_OFFSET)
        final = stable_word(page, VALUE_OFFSET)
        values = sampler.values
        print("samples=%d first=%08x final=%08x" %
              (len(values), values[0] if values else 0, final))
        if header != HEADER:
            raise RuntimeError("no boot progress header: %08x" % header)
        for before, after in zip(values, values[1:]):
            if after < before:
                raise RuntimeError("record went backwards: %08x -> %08x" %
                                   (before, after))
        state, announced, fixed, count, done = fields(final)
        said = [int(match.group(1)) for match in
                (SERVICES.search(line) for line in machine.log) if match]
        if said != [services] or count != services:
            raise RuntimeError("announced %r (record %d), image boots %d" %
                               (said, count, services))
        # said_with_syslog rejoins the ring's 20-byte records.
        lines, _ = machine.said_with_syslog(0)
        if stops_after is not None:
            if any("stage 8" in line for line in machine.log):
                raise RuntimeError("a failed required service reached "
                                   "stage 8")
            if (state != FAILED or not announced or fixed != FIXED_STEPS or
                    done != stops_after):
                raise RuntimeError(
                    "required failure left state=%d announced=%d "
                    "fixed=%d/%d done=%d/%d, expected FAILED done=%d" %
                    (state, announced, fixed, FIXED_STEPS, done, count,
                     stops_after))
            # The failing service's report is FAILED itself: done never
            # reaches it while BOOTING. Only a SKIPPED report followed by
            # the exit would show that value, if the sampler is quick enough.
            for value in values:
                if (fields(value)[0] == BOOTING and
                        fields(value)[4] == stops_after):
                    raise RuntimeError("the required failure reported as "
                                       "not failing: %08x" % value)
            return
        if not announced or fixed != FIXED_STEPS or done != count:
            raise RuntimeError(
                "stage 8 with announced=%d fixed=%d/%d done=%d/%d" %
                (announced, fixed, FIXED_STEPS, done, count))
        if state != DONE:
            raise RuntimeError("a clean boot ended in state %d" % state)
        if any(fields(value)[0] == FAILED for value in values):
            raise RuntimeError("a clean boot passed through FAILED")
        failed = {match.group(1) for match in
                  (LAUNCH_FAILED.search(line) for line in lines) if match}
        if failed != skipped:
            related = [line for line in lines if
                       "ready-fail" in line or "fault-probe" in line]
            raise RuntimeError("services that did not start: %s, expected "
                               "%s; probe log: %s" %
                               (sorted(failed), sorted(skipped), related))
        # Informational: compared by hand with the pre-change baseline.
        print("ASTRA BOOT LAUNCH ORDER: " + " | ".join(
            match.group(1) for match in
            (re.search(r"\blaunch (/\S+)", line) for line in lines)
            if match))
    finally:
        if sampler is not None:
            sampler.stop.set()
            sampler.join()
        if page is not None:
            page.close()
        if machine is not None:
            machine.close()
        os.environ.pop("ASTRA_TEXT_PLANE_PATH", None)
        shutil.rmtree(work, ignore_errors=True)


def profile(base, scratch, name, option):
    """A copy of @p base given `astra_image.py OPTION`, or a plain copy."""
    path = os.path.join(scratch, name + ".img")
    shutil.copyfile(base, path)
    if option is not None:
        subprocess.run([sys.executable,
                        os.path.join(HERE, "astra_image.py"), option, path],
                       check=True, stdout=subprocess.DEVNULL)
    return path


def main():
    if os.geteuid() != 0:
        raise SystemExit("run as root observer with non-root ASTRA_TEST_QEMU_UID/GID")
    qemu_test_credentials()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    arguments = parser.parse_args()
    scratch = tempfile.mkdtemp(prefix="astra-boot-progress-images-")
    try:
        boot(arguments.qemu, arguments.rom,
             profile(arguments.image, scratch, "clean", None),
             expected_services(astra_image.CONFIGURATION), skipped=NO_HOST)
        print("ASTRA BOOT PROGRESS PASS")
        boot(arguments.qemu, arguments.rom,
             profile(arguments.image, scratch, "ready-fail", "--ready-fail"),
             expected_services(astra_image.READY_FAIL_CONFIGURATION),
             skipped=NO_HOST | {"/services/fault-probe"})
        print("ASTRA BOOT PROGRESS USER-SERVICE-SKIPPED PASS")
        manifest = astra_image.REQUIRED_FAIL_STARTUP_MANIFEST
        # Every service line before the absent one is launched first.
        before = expected_services(
            {}, manifest[:manifest.index("service /services/required-absent")])
        boot(arguments.qemu, arguments.rom,
             profile(arguments.image, scratch, "required-fail",
                     "--required-fail"),
             expected_services(astra_image.CONFIGURATION, manifest),
             stops_after=before + 1)
        print("ASTRA BOOT PROGRESS REQUIRED-SERVICE-STOPS PASS")
    finally:
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    main()
