#!/usr/bin/env python3
"""Own production provider, display, input and console lifetimes.

Shell preparation owns trusted paths and its runtime lock. This owner uses
retained Popen children instead of numeric /proc liveness and shell job tables.
The provider remains the only owner of storage cleanup and the QEMU peer.
"""
import argparse
import os
from pathlib import Path
import runpy
import shutil
import signal
import subprocess
import sys
import threading
import time


class GuestWatchdog:
    """Bound guest startup and liveness without treating idle as a hang."""

    def __init__(self, qmp, input_script):
        self.qmp = qmp
        self.input_script = input_script
        self.started = time.monotonic()
        self.last_advance = None
        self.counter = 0
        self.client_type = None
        self.next_probe = self.started
        self.last_error = ""

    def observe(self, now, counter):
        if counter is not None and counter != self.counter:
            self.counter = counter
            self.last_advance = now
        if self.last_advance is None:
            return "guest heartbeat did not start" if now - self.started >= 120 else None
        return "guest heartbeat stalled" if now - self.last_advance >= 60 else None

    def poll(self):
        now = time.monotonic()
        if now < self.next_probe:
            return None
        self.next_probe = now + 3
        counter = None
        if self.qmp.is_socket():
            try:
                if self.client_type is None:
                    self.client_type = runpy.run_path(str(self.input_script))["QmpClient"]
                client = self.client_type(str(self.qmp), timeout=2.0)
                try:
                    counter = client.execute("qom-get", {
                        "path": "/machine", "property": "astra-guest-heartbeat"})
                    if not isinstance(counter, int) or counter < 0:
                        raise ValueError("invalid guest heartbeat")
                    self.last_error = ""
                finally:
                    client.close()
            except (OSError, RuntimeError, ValueError, KeyError) as error:
                self.last_error = str(error)
        return self.observe(now, counter)


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


def record_text(path, contents):
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("w") as stream:
        stream.write(contents)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)
    directory = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
    try:
        os.fsync(directory)
    finally:
        os.close(directory)


def record_watchdog(path, reason, counter, error):
    record_text(path, f"{time.time():.0f} {reason}; heartbeat={counter}; qmp={error or 'ok'}\n")


def reset_cause(cmdline):
    words = [part.partition("=")[2] for part in cmdline.split()
             if part.startswith("astra.watchdog_preload=")]
    if len(words) != 1:
        return "unknown"
    try:
        word = int(words[0], 16)
    except ValueError:
        return "unknown"
    return {0xA57A68D0: "no-watchdog-event-observed",
            0xA57A68D1: "watchdog-event"}.get(word, "unknown")


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
    try:
        cmdline = Path("/proc/cmdline").read_text()
        boot_id = Path("/proc/sys/kernel/random/boot_id").read_text().strip()
        record_text(args.panic.with_name("reset-cause-latest.log"),
                    f"{time.time():.0f} boot={boot_id} cause={reset_cause(cmdline)} "
                    f"preload={next((part.partition('=')[2] for part in cmdline.split() if part.startswith('astra.watchdog_preload=')), 'missing')}\n")
    except OSError as error:
        print(f"Cannot retain Astra reset cause: {error}", file=sys.stderr)
    display_command = ["taskset", "-c", args.display_cpu, *args.display]
    if args.display_splash:
        display_command.append(args.display_splash)
    input_command = ["setpriv", "--reuid", str(args.uid), "--regid", str(args.gid),
        "--clear-groups", "--no-new-privs", "--bounding-set=-all", "--inh-caps=-all",
        "--ambient-caps=-all", "taskset", "-c", args.aux_cpu, "python3", args.input,
        "--qmp", args.qmp]
    stopping = threading.Event()
    service_stop = threading.Event()
    watchdog = GuestWatchdog(Path(args.qmp), Path(args.input))
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
                reason = watchdog.poll()
                if reason is not None:
                    try:
                        record_watchdog(args.panic.with_name("watchdog-latest.log"),
                                        reason, watchdog.counter, watchdog.last_error)
                    except OSError as error:
                        print(f"Cannot retain Astra watchdog reason: {error}", file=sys.stderr)
                    print(f"Astra watchdog: {reason}; heartbeat={watchdog.counter}",
                          file=sys.stderr)
                    stopping.set()
                    break
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
