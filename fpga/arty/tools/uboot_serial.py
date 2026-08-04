#!/usr/bin/env python3
"""Interrupt and command the Arty Z7 U-Boot console over its FTDI UART."""

import argparse
import os
from pathlib import Path
import select
import sys
import termios
import time
import tty


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--device", default="/dev/ttyUSB1")
    parser.add_argument("--prompt", default="Zynq> ")
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--interrupt", action="store_true")
    parser.add_argument("--command", action="append", default=[])
    parser.add_argument("--command-file", type=Path)
    return parser.parse_args()


def configure_uart(fd: int) -> None:
    tty.setraw(fd)
    attrs = termios.tcgetattr(fd)
    attrs[4] = termios.B115200
    attrs[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    termios.tcflush(fd, termios.TCIOFLUSH)


def read_until(fd: int, marker: bytes, deadline: float) -> None:
    recent = bytearray()
    while time.monotonic() < deadline:
        readable, _, _ = select.select([fd], [], [], 0.1)
        if not readable:
            continue
        data = os.read(fd, 4096)
        if not data:
            continue
        sys.stdout.buffer.write(data)
        sys.stdout.buffer.flush()
        recent.extend(data)
        if marker in recent:
            return
        if len(recent) > 4096:
            del recent[:-4096]
    raise TimeoutError(f"timed out waiting for {marker!r}")


def main() -> int:
    args = parse_args()
    fd = os.open(args.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure_uart(fd)
        deadline = time.monotonic() + args.timeout
        prompt = args.prompt.encode("ascii")

        if args.interrupt:
            read_until(fd, b"Hit any key to stop autoboot", deadline)
            os.write(fd, b" ")
        else:
            os.write(fd, b"\r")
        read_until(fd, prompt, deadline)

        commands = list(args.command)
        if args.command_file is not None:
            commands.extend(
                line.strip()
                for line in args.command_file.read_text(encoding="ascii").splitlines()
                if line.strip() and not line.lstrip().startswith("#")
            )

        for command in commands:
            os.write(fd, command.encode("ascii") + b"\r")
            read_until(fd, prompt, deadline)
    finally:
        os.close(fd)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, TimeoutError) as error:
        print(f"uboot_serial: {error}", file=sys.stderr)
        raise SystemExit(1)
