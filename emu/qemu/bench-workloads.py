#!/usr/bin/env python3
"""Profile a fixed set of unrelated workloads, one interval each.

usage: bench-workloads.py QEMU PLUGIN ROM IMAGE OUT.aprof [--only NAME]...

QEMU and PLUGIN are the host-profile build (emu/qemu/build.sh host-profile)
and its libastra_profile.so. IMAGE is a standard system image
(astra_image.py --create, then astra_image.py IMAGE), which carries Terminal
and ChocolateDoom. Not a --test-app image: that replaces the startup manifest,
and the terminal does not open on it.

A performance change is judged against all of these, never against one
program: each exercises a different part of the system, and each interval is
a fixed amount of work, so its guest instruction count is comparable across
kernels and builds. Compare runs with

    tools/astra-prof workloads --kernel BASE.elf BASE.aprof \\
                               --kernel NEW.elf NEW.aprof

Workloads, in the order they run:

  boot        ROM start to the desktop: services, loader, storage, IPC
  spawn       20 x `ls` from zsh: process creation, dynamic loading, exit
  fs          fsstress, fixed seed, 4 forked workers: VFS, ext4, fork/COW
  heap        heapbench's deterministic trace: allocator, commit faults
  lua         a fixed Lua loop: user CPU, small allocations, few syscalls
  doom-start  Chocolate Doom from its icon to 10 rendered frames: display,
              audio, file reads, everything at once

Instruction counts include whatever else the machine does meanwhile (services
polling, the desktop). That is deliberate -- it is the cost a person pays --
and it is also why totals move a few percent between identical runs. Compare
per-function instructions (astra-prof report) for changes smaller than that.
"""

import argparse
import importlib.util
import mmap
import os
import re
import shutil
import socket
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, file))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


doom_gate = load("astra_doom_gate", "test-chocolate-doom.py")
terminal_gate = doom_gate.terminal_gate
mailbox_gate = doom_gate.mailbox_gate
audio_gate = doom_gate.audio_gate

# name, shell command. Each runs to completion from a zsh prompt in /.
# Astra has no /dev/null, so output goes to a file -- except fsstress's:
# fork() refuses while the process has a regular file open
# (astra_posix_file_fork_ready), and its stdout would be one.
SHELL_WORKLOADS = (
    ("spawn", "for i in {1..20}; do ls /apps > bench.out; done"),
    ("fs", "fsstress -n 200 -w 4 -s 7 bench"),
    ("heap", "heapbench > bench.out"),
    ("lua", "lua -e 'local t={} for i=1,20000 do t[i]=i*i%97 end "
            "local s=0 for i=1,#t do s=s+t[i] end print(s)'"),
)
WORKLOADS = ("boot",) + tuple(name for name, _ in SHELL_WORKLOADS) + \
    ("doom-start",)
DOOM_FRAMES = 10


def control(path, command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.connect(path)
        connection.sendall((command + "\n").encode())
        return connection.recv(4096).decode().strip()


def wait_status(machine, name, after, deadline):
    """The exit status the shell printed for workload @p name, or None.

    Matched on the value, not the text: the terminal echoes the typed
    command, which carries the marker followed by `$?`."""
    pattern = re.compile(r"ASTRA-BENCH-%s=(\d+)" % re.escape(name))
    end = time.monotonic() + deadline
    while time.monotonic() < end:
        lines, _ = machine.said(after)
        for line in lines:
            match = pattern.search(line)
            if match:
                return int(match.group(1))
        time.sleep(0.1)
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("qemu")
    parser.add_argument("plugin")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("output")
    parser.add_argument("--shell", action="append", default=[],
                        metavar="NAME=COMMAND",
                        help="also profile an ad-hoc shell workload; with "
                             "--only, name it there to select it")
    parser.add_argument("--probe", action="append", default=[],
                        help="guest address whose callers the profile "
                             "records (astra-prof report lists them)")
    parser.add_argument("--register",
                        help="register whose values each --probe records, "
                             "e.g. d0 at the syscall trap for a syscall "
                             "histogram")
    parser.add_argument("--only", action="append",
                        help="profile only these workloads (boot and the "
                             "terminal still run, unprofiled)")
    arguments = parser.parse_args()
    shell_workloads = list(SHELL_WORKLOADS)
    for spec in arguments.shell:
        name, separator, command = spec.partition("=")
        if not separator or not re.fullmatch(r"[a-z0-9-]+", name) or \
                name in WORKLOADS:
            parser.error("--shell wants a new NAME=COMMAND: %r" % spec)
        shell_workloads.append((name, command))
    names = WORKLOADS + tuple(name for name, _ in shell_workloads
                              if name not in WORKLOADS)
    for name in arguments.only or ():
        if name not in names:
            parser.error("unknown workload %r" % name)
    wanted = set(arguments.only or names)
    failures = []

    with tempfile.TemporaryDirectory(prefix="astra-bench-workloads-") as work:
        image = os.path.join(work, "bench.img")
        shutil.copyfile(arguments.image, image)
        batches = doom_gate.Batches()
        mailbox = os.path.join(work, "mailbox.bin")
        mailbox_gate.create_mailbox(mailbox)
        stream = open(mailbox, "r+b")
        view = mmap.mmap(stream.fileno(), mailbox_gate.HEADER_BYTES)
        helper = mailbox_gate.Helper(view, observe=batches.observe)
        helper.thread.start()
        os.environ["ASTRA_DISPLAY_MAILBOX_PATH"] = mailbox
        os.environ["ASTRA_DISPLAY_PAYLOAD_PATH"] = \
            mailbox_gate.payload_path(mailbox)
        audio_socket = os.path.join(work, "audio.sock")
        audio_host = audio_gate.AudioHost(audio_socket)
        os.environ["ASTRA_AUDIO_HOST_SOCKET"] = audio_socket
        ctl = os.path.join(work, "prof.sock")
        boot = "boot" in wanted
        machine = terminal_gate.Machine(
            arguments.qemu, arguments.rom, image, work, extra_args=[
                "-plugin", "%s,output=%s,control=%s,autostart=%s,label=%s%s" %
                (arguments.plugin, os.path.abspath(arguments.output), ctl,
                 "true" if boot else "false", "boot" if boot else "idle",
                 "".join(",probe=%s" % probe for probe in arguments.probe) +
                 (",register=%s" % arguments.register
                  if arguments.register else ""))])
        try:
            if not machine.wait_for_serial(terminal_gate.BOOT_MARKER, 300):
                print("FAIL boot: never reached the desktop")
                return 1
            if boot:
                control(ctl, "stop")
                print("boot ok", flush=True)
            # Read the icon layout now: under QEMU the remote desktop's
            # relaunches wrap the trace ring within seconds of boot.
            terminal_icon = machine.desktop_icon()
            doom_icon = machine.desktop_icon("/apps/ChocolateDoom.app")

            # open_terminal() less its boot wait, which already happened.
            time.sleep(2.0)
            before = machine.sequence()
            machine.qmp.double_click(*terminal_icon)
            lines, _ = machine.wait_for_text(terminal_gate.BANNER, 120, before)
            if lines is None or not machine.wait_for_ready(120, before):
                print("FAIL: no terminal (%s)" %
                      ("no banner" if lines is None else "never ready"))
                for line in machine.said(before)[0][-30:]:
                    print("    |%s|" % line)
                return 1
            for name, command in shell_workloads:
                if name not in wanted:
                    continue
                machine.settle()
                before = machine.sequence()
                control(ctl, "start " + name)
                machine.qmp.type_line(
                    "%s; print ASTRA-BENCH-%s=$?" % (command, name))
                status = wait_status(machine, name, before, 600)
                control(ctl, "stop")
                print("%s %s" % (name, "ok" if status == 0 else
                                 "FAIL status=%s" % status), flush=True)
                if status != 0:
                    failures.append(name)

            if "doom-start" in wanted:
                base = len(batches.times)
                control(ctl, "start doom-start")
                machine.qmp.double_click(*doom_icon)
                end = time.monotonic() + 600
                while (len(batches.times) - base < DOOM_FRAMES and
                       time.monotonic() < end):
                    time.sleep(0.05)
                control(ctl, "stop")
                rendered = len(batches.times) - base >= DOOM_FRAMES
                print("doom-start %s" % ("ok" if rendered else
                                         "FAIL never rendered"), flush=True)
                if not rendered:
                    failures.append("doom-start")
        finally:
            machine.close()
            helper.stopping = True
            helper.thread.join(timeout=1)
            helper.release()
            view.close()
            stream.close()
            audio_host.close()
    if failures:
        print("FAIL: " + " ".join(failures))
        return 1
    print("BENCH-WORKLOADS PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
