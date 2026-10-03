#!/usr/bin/env python3
"""Check that each thread's FPU state is its own (docs/USERSPACE_FPU.md 5.2).

Boots the terminal image with `fpucheck` installed and runs each of its
subtests from zsh. A subtest passes when it prints `FPU <NAME> PASS` and
exits 0. `privilege` must retire the process with vector 8 instead, and
`contract` -- an `fsin`, which the MC68040 leaves to software -- with the
F-line vector 11 (qemu-9.2/target-m68k-68040-fpu-unimplemented.patch).
"""

import argparse
import importlib.util
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
HERE = os.environ.get("ASTRA_QEMU_TEST_ROOT",
                      os.path.dirname(os.path.abspath(__file__)))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)
astra_image = terminal.astra_image

# `dirty` must run immediately before `fresh`: it leaves the pattern that a
# new process must not see.
SUBTESTS = ("self", "threads", "processes", "fork", "signals", "dirty",
            "fresh")
EXIT_MARK = "ASTRA-FPU-EXIT-"


def run_subtest(machine, name, deadline):
    """Return (lines, exit status), or (lines, None) if zsh never answered."""
    before = machine.sequence()
    # The quotes keep the echoed command line from matching the mark.
    machine.qmp.type_line('fpucheck %s; print ASTRA-FPU-""EXIT-$?' % name)
    said = terminal.wait_for_command(machine, EXIT_MARK, deadline, before)
    if said is None:
        return machine.said(before)[0], None
    for line in said:
        match = re.search(re.escape(EXIT_MARK) + r"(\d+)", line)
        if match:
            return said, int(match.group(1))
    return said, None


def retires_with(machine, name, vector, deadline):
    """The subtest must not return: the kernel retires it with `vector`."""
    serial_before = len(machine.recent_serial(100000))
    said, status = run_subtest(machine, name, deadline)
    serial = machine.recent_serial(100000)[serial_before:]
    report = " ".join(serial)
    if (status is not None and status != 0 and "*** user fault" in report
            and re.search(r"vector %d\b" % vector, report)):
        print("PASS %s (vector %d)" % (name, vector))
        return True
    print("FAIL %s (exit %s)" % (name, status))
    for line in [l for l in said if "FPU " in l] + serial[-10:]:
        print("    |%s|" % line)
    return False


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("--image", required=True)
    parser.add_argument("--catalog", default=astra_image.DEFAULT_CATALOG)
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    parser.add_argument("--command-deadline", type=float, default=120.0)
    args = parser.parse_args()

    if not terminal.refresh_workspace_rom(args.rom):
        return 1
    built = subprocess.run(
        ["make", "-C", os.path.join(terminal.ROOT, "sw", "userspace",
                                    "commands"), "build/m68k/fpucheck"],
        check=False)
    if built.returncode != 0:
        print("FAIL: could not build fpucheck")
        return 1
    failures = []
    with tempfile.TemporaryDirectory(prefix="astra-fpu-") as temporary:
        scratch = os.path.join(temporary, "card.img")
        shutil.copyfile(args.image, scratch)
        astra_image.install(scratch, args.catalog, vim_runtime=None)
        astra_image.install_test_command(scratch, "fpucheck")
        run_dir = os.path.join(temporary, "run")
        os.mkdir(run_dir)
        machine = terminal.Machine(args.qemu, args.rom, scratch, run_dir)
        try:
            if not terminal.open_terminal(machine, args.boot_deadline,
                                          args.command_deadline):
                return 1
            for name in SUBTESTS:
                said, status = run_subtest(machine, name,
                                           args.command_deadline)
                verdict = [line for line in said if "FPU " in line]
                expected = ("FPU DIRTY DONE" if name == "dirty" else
                            "FPU %s PASS" % name.upper())
                if status == 0 and any(expected in line for line in verdict):
                    print("PASS %s" % name)
                    continue
                failures.append(name)
                print("FAIL %s (exit %s)" % (name, status))
                for line in verdict or said[-20:]:
                    print("    |%s|" % line)

            if not retires_with(machine, "contract", 11,
                                args.command_deadline):
                failures.append("contract")

            if not retires_with(machine, "privilege", 8,
                                args.command_deadline):
                failures.append("privilege")
        finally:
            machine.close()
    if failures:
        print("FAIL: test-fpu: %s" % " ".join(failures))
        return 1
    print("ASTRA FPU GATE PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
