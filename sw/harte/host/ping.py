#!/usr/bin/env python3

import os
from pathlib import Path
import sys
import time

import serial

sys.path.insert(0, str(Path(__file__).resolve().parent))
from proto import CMD_PING, CMD_PONG
from transport import find_port, transact


def main() -> int:
    baud = int(os.environ.get("ASTRA_BAUD", "460800"))
    with serial.Serial(find_port(), baud, timeout=0.1) as serial_port:
        time.sleep(0.25)
        response = transact(serial_port, CMD_PING, b"\xa3", 0.5)
    if response != (CMD_PONG, b"\xa3"):
        print(f"PING failed: {response}")
        return 1
    print("PING PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
