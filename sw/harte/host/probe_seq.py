#!/usr/bin/env python3
# Probe one RUN case (paced, overrun-safe). Usage: probe_seq.py <instr_hex|->
# Prints markers + whether harte_exec returned ('A').
import serial, struct, sys, time
sys.path.insert(0, ".")
from proto import frame, parse, CMD_PING, CMD_RUN

arg = sys.argv[1] if len(sys.argv) > 1 else "-"
afill = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0   # value to load into a0..a6
instr = b"" if arg == "-" else bytes.fromhex(arg)
p = serial.Serial("/dev/ttyUSB0", 115200, timeout=1.0)
time.sleep(0.5)
good = 0
for _ in range(25):
    p.reset_input_buffer(); p.write(frame(CMD_PING, bytes([0x5A]))); time.sleep(0.12)
    if parse(p.read(8)):
        good += 1
    if good >= 3:
        break
d = [1, 2, 0, 0, 0, 0, 0, 0]; a = [afill] * 7
pl = b"".join(struct.pack(">I", x) for x in d + a) + bytes([0, len(instr)]) + instr
fr = frame(CMD_RUN, pl)
p.reset_input_buffer()
for byte in fr:
    p.write(bytes([byte])); p.flush(); time.sleep(0.001)
time.sleep(0.6)
r = p.read(70)
ascii_view = bytes(c if 32 <= c < 127 else 46 for c in r)
returned = "A=RETURNED" if 0x41 in r else "HANG(no A)"
print("instr=%-6s alive=%d  %s  raw=%s  %s" % (arg, good, returned, r.hex()[:40], ascii_view))
p.close()
