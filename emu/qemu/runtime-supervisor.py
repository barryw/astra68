#!/usr/bin/env python3
"""Own production provider, display, input and console lifetimes.

Shell preparation owns trusted paths and its runtime lock. This owner uses
retained Popen children instead of numeric /proc liveness and shell job tables.
The provider remains the only owner of storage cleanup and the QEMU peer.
"""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import threading


def stop(child):
    if child is None:
        return
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=1)
        except subprocess.TimeoutExpired:
            child.kill()
    child.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--display", nargs=4, required=True)
    parser.add_argument("--display-splash", default="")
    parser.add_argument("--display-cpu", required=True)
    parser.add_argument("--input", required=True)
    parser.add_argument("--aux-cpu", required=True)
    parser.add_argument("--qmp", required=True)
    parser.add_argument("--remote-qmp", required=True)
    parser.add_argument("--uid", type=int, required=True)
    parser.add_argument("--gid", type=int, required=True)
    parser.add_argument("--console", type=Path, required=True)
    parser.add_argument("--panic", type=Path, required=True)
    parser.add_argument("provider", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if os.geteuid() != 0 or args.uid <= 0 or args.gid <= 0:
        parser.error("root service and explicit non-root QEMU credentials required")
    command = args.provider
    if command and command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("provider command required")
    display_command = ["taskset", "-c", args.display_cpu, *args.display]
    if args.display_splash:
        display_command.append(args.display_splash)
    input_command = ["setpriv", "--reuid", str(args.uid), "--regid", str(args.gid),
        "--clear-groups", "--no-new-privs", "--bounding-set=-all", "--inh-caps=-all",
        "--ambient-caps=-all", "taskset", "-c", args.aux_cpu, "python3", args.input,
        "--qmp", args.qmp]
    stopping = threading.Event()
    service_stop = threading.Event()
    def request_stop(number, frame):
        del frame
        if number == signal.SIGUSR1:
            service_stop.set()
        stopping.set()
    for number in (signal.SIGHUP, signal.SIGINT, signal.SIGTERM, signal.SIGUSR1):
        signal.signal(number, request_stop)
    display = input_child = provider = reader = None
    console_error = []
    status = 1
    try:
        with args.console.open("wb") as console:
            display = subprocess.Popen(display_command, close_fds=True)
            provider = subprocess.Popen(command, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, close_fds=True)
            def copy_console():
                try:
                    for line in iter(provider.stdout.readline, b""):
                        console.write(line)
                        console.flush()
                        sys.stdout.buffer.write(line)
                        sys.stdout.buffer.flush()
                except (OSError, ValueError) as error:
                    console_error.append(error)
                    stopping.set()
            reader = threading.Thread(target=copy_console, name="astra-console")
            reader.start()
            input_child = subprocess.Popen(input_command, close_fds=True)
            while provider.poll() is None and not stopping.wait(0.5):
                for name, child, helper in (("display", display, display_command),
                                           ("input", input_child, input_command)):
                    code = child.poll()
                    if code is None:
                        continue
                    print(f"Astra {name} helper exited with status {code}; restarting", file=sys.stderr)
                    replacement = subprocess.Popen(helper, close_fds=True)
                    if name == "display":
                        display = replacement
                    else:
                        input_child = replacement
            if provider.poll() is None:
                provider.terminate()
            # Never kill the provider: it forwards TERM to its exact peer and
            # retains mounts/loops until real retirement. This wait is deliberate.
            status = provider.wait()
            stop(input_child)
            input_child = None
            stop(display)
            display = None
            reader.join()
            provider.stdout.close()
            reader = None
    finally:
        stop(input_child)
        stop(display)
        if provider is not None and provider.poll() is None:
            provider.terminate()
            provider.wait()
        if reader is not None:
            reader.join()
    for path in (args.qmp, args.remote_qmp):
        Path(path).unlink(missing_ok=True)
    panicked = args.console.exists() and b"*** AXIOM KERNEL PANIC ***" in args.console.read_bytes()
    if panicked:
        shutil.copyfile(args.console, args.panic)
    if stopping.is_set() or console_error:
        return 0 if service_stop.is_set() and status == 0 and not console_error and not panicked else 1
    if status in (88, 89):
        action = "poweroff" if status == 88 else "reboot"
        if subprocess.run(["systemctl", action, "--no-block"], check=False).returncode == 0:
            print(f"Astra guest stopped cleanly; DE25 {action} requested")
            return 0
        print(f"Astra guest stopped, but DE25 {action} failed", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
