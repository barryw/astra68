#!/usr/bin/env python3
# RAM-exec probe: send one RUN case, print raw reply (markers/exception bytes visible).
# Usage: probe.py [instr_hex] [a0_hex]
#   instr_hex: test instruction bytes, e.g. "d041" (ADD.W D1,D0); "" => ilen=0 (codebuf = RTS only)
#   a0_hex:    optional value to load into a0 (else 0)
import serial, struct, sys, time
sys.path.insert(0, "/home/barry/astra68/sw/harte/host")
from proto import frame, parse, CMD_PING, CMD_RUN

instr = bytes.fromhex(sys.argv[1]) if len(sys.argv) > 1 and sys.argv[1] else b""
a0 = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
d = [1, 2, 0, 0, 0, 0, 0, 0]
a = [a0, 0, 0, 0, 0, 0, 0]

p = serial.Serial("/dev/ttyUSB0", 115740, timeout=1.5)
alive = False
for _ in range(8):
    p.reset_input_buffer(); p.write(frame(CMD_PING, bytes([0x5A]))); time.sleep(0.15)
    if parse(p.read(8)): alive = True; break
pl = b"".join(struct.pack(">I", x & 0xFFFFFFFF) for x in d + a) + bytes([0, len(instr)]) + instr
fr = frame(CMD_RUN, pl)
p.reset_input_buffer()
for byte in fr:                     # pace bytes: device RX has no FIFO, full-baud bursts overrun it
    p.write(bytes([byte])); p.flush(); time.sleep(0.001)
time.sleep(0.6)
r = p.read(80)
p.close()
print(f"alive={alive} instr={instr.hex() or '(ilen=0)'} -> raw={r.hex()} ascii={bytes(c if 32<=c<127 else 46 for c in r)}")
