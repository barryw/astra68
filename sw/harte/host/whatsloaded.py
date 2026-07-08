#!/usr/bin/env python3
# Ask the running harness what build it is (CMD_ID -> BUILD_ID). Cross-references BUILD_LOG.md
# so you ALWAYS know exactly what bitstream is loaded and what fixes/features it carries.
import serial, sys, time, glob, os, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from proto import frame, parse

CMD_ID, CMD_IDR = 0x03, 0x83
LOG = os.path.join(os.path.dirname(__file__), "..", "BUILD_LOG.md")

def find_port():
    if os.environ.get("ASTRA_PORT"):
        return os.environ["ASTRA_PORT"]
    for pat in ("/dev/ttyUSB*", "/dev/cu.usbserial*"):
        m = sorted(glob.glob(pat))
        if m:
            return m[0]
    return "/dev/ttyUSB0"

def query_build_id(tries=20):
    p = serial.Serial(find_port(), 115740, timeout=1.0)
    time.sleep(0.4)
    bid = None
    for _ in range(tries):
        p.reset_input_buffer(); p.write(frame(CMD_ID, b"\x00")); time.sleep(0.15)
        r = parse(p.read(8))
        if r and r[0] == CMD_IDR and len(r[1]) >= 4:
            bid = int.from_bytes(r[1][:4], "big"); break
    p.close()
    return bid

def log_line(bid):
    try:
        for ln in open(LOG):
            if f"0x{bid:08x}" in ln.lower():
                return ln.strip()
    except FileNotFoundError:
        pass
    return None

if __name__ == "__main__":
    bid = query_build_id()
    if bid is None:
        print("device: NO RESPONSE (not running a CMD_ID-capable harness, or wedged)")
        sys.exit(2)
    print(f"device BUILD_ID = 0x{bid:08x}")
    ln = log_line(bid)
    print("  " + ln if ln else "  (not in BUILD_LOG.md — unlogged build)")
