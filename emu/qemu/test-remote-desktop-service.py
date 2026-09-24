#!/usr/bin/env python3
"""Exercise Astra's paired remote-desktop service and every CLI transition."""

import argparse
import errno
import importlib.util
import os
from pathlib import Path
import queue
import shutil
import socket
import subprocess
import tempfile
import threading
import time


HERE = os.path.dirname(os.path.abspath(__file__))
SPEC = importlib.util.spec_from_file_location(
    "astra_terminal_gate", os.path.join(HERE, "test-terminal.py"))
terminal = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(terminal)


def wait_for(predicate, description, timeout):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.05)
    raise RuntimeError("timed out waiting for " + description)


def port_available(port):
    probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("127.0.0.1", port))
        return True
    except OSError as error:
        if error.errno == errno.EADDRINUSE:
            return False
        raise
    finally:
        probe.close()


class Broker:
    def __init__(self, command, environment, control_path):
        self.lines = queue.Queue()
        self.output = []
        self.process = subprocess.Popen(
            command, env=environment, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1)
        self.thread = threading.Thread(target=self._pump, daemon=True)
        self.thread.start()
        wait_for(lambda: Path(control_path).exists(), "broker control socket",
                 5.0)

    def _pump(self):
        for line in self.process.stdout:
            line = line.rstrip("\n")
            self.output.append(line)
            self.lines.put(line)

    def generations(self):
        return sum("ready generation" in line for line in self.output)

    def close(self):
        if self.process.poll() is None:
            self.process.terminate()
        try:
            self.process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise RuntimeError("remote desktop broker hung on shutdown")
        self.thread.join(timeout=1.0)
        if self.process.returncode != 0:
            raise RuntimeError("remote desktop broker failed:\n" +
                               "\n".join(self.output))


def command_status(machine, text, timeout, number):
    marker = "RD-COMMAND-%u-" % number

    machine.settle()
    before = machine.sequence()
    machine.qmp.type_line(text + "; print -r -- " + marker + "$?")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        lines, _ = machine.said(before)
        for line in lines:
            if line.startswith(marker) and line[len(marker):].isdigit():
                return lines, int(line[len(marker):])
        time.sleep(0.05)
    raise RuntimeError("command hung: %s\n%s" %
                       (text, "\n".join(machine.said(before)[0])))


def command(machine, text, expected, timeout, number):
    lines, status = command_status(machine, text, timeout, number)
    if status != 0:
        raise RuntimeError("command hung or failed: %s\n%s" % (
            text, "\n".join(lines)))
    if expected is not None and not any(expected in line for line in lines):
        raise RuntimeError("command omitted %r: %s\n%s" % (
            expected, text, "\n".join(lines)))
    return lines


def event(machine, phrase, number):
    pattern = phrase.replace(" ", "\\ ")
    return command(machine, "events | while IFS= read -r line; do "
                   "[[ $line == *%s* ]] && print -r -- $line; "
                   "done; true" % pattern, phrase, 30.0, number)


def wait_state(machine, state, timeout, number):
    deadline = time.monotonic() + timeout
    lines = []
    while time.monotonic() < deadline:
        lines = command(machine, "service inspect remote-desktop", None,
                        timeout, number[0])
        number[0] += 1
        if any("state: " + state in line for line in lines):
            return lines
        time.sleep(0.1)
    trace = machine.trace()
    relevant = []
    for index, line in enumerate(trace):
        if "launch /services/remote-d" in line:
            relevant = trace[max(0, index - 20):index + 80]
            break
    raise RuntimeError("remote-desktop never reached state %s:\n%s\n%s\n%s" %
                       (state, "\n".join(lines), "\n".join(relevant[-80:]),
                        "\n".join(trace[-80:])))


def service_pid(lines):
    matches = [line for line in lines if "pid: " in line]
    if len(matches) != 1:
        raise RuntimeError("service inspection did not contain one PID")
    return int(matches[0].split("pid: ", 1)[1].split()[0])


def restarted_service_pid(lines, previous):
    current = service_pid(lines)
    if current == previous:
        raise RuntimeError("service restart retained stale PID %u" % current)
    return current


def stable_service_pid(lines, expected):
    current = service_pid(lines)
    if current != expected:
        raise RuntimeError("service PID changed from %u to %u" %
                           (expected, current))
    return current


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("qemu")
    parser.add_argument("rom")
    parser.add_argument("image")
    parser.add_argument("broker")
    parser.add_argument("--qemu-user")
    parser.add_argument("--sysroot")
    parser.add_argument("--boot-deadline", type=float, default=90.0)
    parser.add_argument("--command-deadline", type=float, default=30.0)
    arguments = parser.parse_args()
    number = [1]

    with tempfile.TemporaryDirectory(prefix="astra-remote-service-") as root:
        image = os.path.join(root, "storage.img")
        capture = os.path.join(root, "capture.bin")
        control = os.path.join(root, "control.sock")
        run_directory = os.path.join(root, "machine")
        shutil.copyfile(arguments.image, image)
        os.mkdir(run_directory)
        with open(capture, "wb") as frame:
            frame.truncate(1920 * 1080 * 3)
        probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        probe.bind(("127.0.0.1", 0))
        port = probe.getsockname()[1]
        probe.close()
        environment = os.environ.copy()
        environment.update({
            "ASTRA_CAPTURE_DEVICE": capture,
            "ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET": control,
            "ASTRA_RFB_PORT": str(port),
        })
        broker_command = []
        if arguments.qemu_user:
            broker_command.append(arguments.qemu_user)
            if arguments.sysroot:
                broker_command.extend(("-L", arguments.sysroot))
        broker_command.append(arguments.broker)
        previous_control = os.environ.get(
            "ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET")
        os.environ["ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET"] = control
        broker = Broker(broker_command, environment, control)
        machine = None
        try:
            assert port_available(port), "broker listened before guest lease"
            machine = terminal.Machine(arguments.qemu, arguments.rom, image,
                                       run_directory)
            if not terminal.open_terminal(machine, arguments.boot_deadline,
                                          arguments.command_deadline):
                raise RuntimeError("Astra terminal did not start")
            command(machine, "service list", "remote-desktop", 30.0,
                    number[0]); number[0] += 1
            try:
                wait_state(machine, "running", 30.0, number)
            except RuntimeError as error:
                raise RuntimeError("%s\nbroker output:\n%s" %
                                   (error, "\n".join(broker.output))) from error
            wait_for(lambda: not port_available(port),
                     "automatic VNC activation", 10.0)
            command(machine, "service stop remote-desktop", None, 30.0,
                    number[0]); number[0] += 1
            wait_for(lambda: port_available(port), "VNC stop", 10.0)
            wait_state(machine, "stopped", 30.0, number)
            command(
                machine,
                "service add remote-probe /services/remote-desktop "
                "--paired --disabled --manual --restart=never "
                "--grant=HOST_DEVICE", None, 30.0, number[0])
            number[0] += 1
            command(machine, "service inspect remote-probe", "runs: paired",
                    30.0, number[0]); number[0] += 1
            command(machine, "service enable remote-probe", None, 30.0,
                    number[0]); number[0] += 1
            command(machine, "service inspect remote-probe", "enabled: yes",
                    30.0, number[0]); number[0] += 1
            command(machine, "service disable remote-probe", None, 30.0,
                    number[0]); number[0] += 1
            command(machine, "service inspect remote-probe", "enabled: no",
                    30.0, number[0]); number[0] += 1
            command(machine, "service delete remote-probe", None, 30.0,
                    number[0]); number[0] += 1

            command(machine, "service start remote-desktop", None, 30.0,
                    number[0]); number[0] += 1
            wait_for(lambda: not port_available(port), "VNC activation", 10.0)
            wait_state(machine, "running", 30.0, number)
            event(machine, "remote desktop ready", number[0]); number[0] += 1

            command(machine, "service pause remote-desktop", None, 30.0,
                    number[0]); number[0] += 1
            wait_for(lambda: port_available(port), "VNC pause", 10.0)
            wait_state(machine, "paused", 30.0, number)
            command(machine, "service resume remote-desktop", None, 30.0,
                    number[0]); number[0] += 1
            wait_for(lambda: not port_available(port), "VNC resume", 10.0)
            running = wait_state(machine, "running", 30.0, number)

            generation = broker.generations()
            pid = service_pid(running)
            command(machine, "service restart remote-desktop", None, 30.0,
                    number[0]); number[0] += 1
            wait_for(lambda: broker.generations() > generation,
                     "VNC restart", 10.0)
            running = wait_state(machine, "running", 30.0, number)
            pid = restarted_service_pid(running, pid)
            generation = broker.generations()
            time.sleep(0.5)
            running = wait_state(machine, "running", 30.0, number)
            stable_service_pid(running, pid)
            if broker.generations() != generation:
                raise RuntimeError("stable service reacquired its host lease")

            broker.close()
            wait_for(lambda: port_available(port), "broker failure", 10.0)
            wait_state(machine, "failed", 30.0, number)
            event(machine, "remote desktop peer", number[0]); number[0] += 1
            listed = command(machine, "service list", "remote-desktop",
                             30.0, number[0]); number[0] += 1
            if not any(line.startswith("remote-desktop") and
                       "failed" in line for line in listed):
                raise RuntimeError("service list hid remote-desktop failure")
            broker = Broker(broker_command, environment, control)
            deadline = time.monotonic() + 20.0
            while port_available(port) and time.monotonic() < deadline:
                command(machine, "service inspect remote-desktop", None,
                        30.0, number[0]); number[0] += 1
                time.sleep(0.1)  # Manager traffic must not defer the retry.
            if port_available(port):
                raise RuntimeError("broker recovery was not retried")
            running = wait_state(machine, "running", 30.0, number)
            pid = restarted_service_pid(running, pid)
            event(machine, "remote desktop ready", number[0]); number[0] += 1

            command(machine, "service stop remote-desktop", None, 30.0,
                    number[0]); number[0] += 1
            wait_for(lambda: port_available(port), "VNC stop", 10.0)
            wait_state(machine, "stopped", 30.0, number)
        finally:
            if machine is not None:
                machine.close()
            broker.close()
            if previous_control is None:
                os.environ.pop("ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET", None)
            else:
                os.environ["ASTRA_REMOTE_DESKTOP_CONTROL_SOCKET"] = \
                    previous_control
    print("ASTRA REMOTE DESKTOP SERVICE PASS")


if __name__ == "__main__":
    raise SystemExit(main())
