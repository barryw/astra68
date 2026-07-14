#!/usr/bin/env python3
# Hand-built RUN smoke test — proves load -> single-step -> dump on silicon.
# Sends a few register-only cases with known ALU results and checks regs + CCR.
import os
from pathlib import Path
import serial
import struct
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from proto import CMD_RESULT, CMD_RUN
from transport import find_port, transact

PORT = find_port()
BAUD = int(os.environ.get("ASTRA_BAUD", "460800"))

# CCR bits (low byte of SR): X N Z V C
X, N, Z, V, C = 0x10, 0x08, 0x04, 0x02, 0x01

def run(p, d, a, ccr, instr, tries=6):
    """d: 8 longs, a: 7 longs, ccr: byte, instr: bytes -> (regs[15], ccr) or None.
    Retries on timeout/bad frame — the device drops corrupt frames (checksum) instead of
    executing them, so a lost/garbled frame just needs resending (spec §5)."""
    pl = b"".join(struct.pack(">I", x & 0xFFFFFFFF) for x in d + a)
    pl += bytes([ccr & 0xFF, len(instr)]) + instr
    for _ in range(tries):
        response = transact(p, CMD_RUN, pl, 0.5)
        if response is not None and response[0] == CMD_RESULT and len(response[1]) == 61:
            body = response[1]
            regs = [struct.unpack(">I", body[i*4:i*4+4])[0] for i in range(15)]
            return regs, body[60]
    return None

# case: (name, d_in, a_in, ccr_in, instr_bytes, check(regs,ccr)->bool, expect_str)
CASES = [
    ("ADD.W D1,D0  1+2",     [1,2,0,0,0,0,0,0], [0]*7, 0,
     b"\xD0\x41", lambda rg,cc: rg[0] & 0xFFFF == 3,            "D0.w=3"),
    ("ADD.L D1,D0  -1+1",    [0xFFFFFFFF,1,0,0,0,0,0,0], [0]*7, 0,
     b"\xD0\x81", lambda rg,cc: rg[0]==0 and (cc&(Z|C|X))==(Z|C|X), "D0=0, Z+C+X set"),
    ("MOVEQ #-1,D3",         [0,0,0,0,0,0,0,0], [0]*7, 0,
     b"\x76\xFF", lambda rg,cc: rg[3]==0xFFFFFFFF and (cc&N) and not (cc&Z), "D3=FFFFFFFF, N set"),
    ("SUB.L D0,D0  -> 0",    [0x12345678,0,0,0,0,0,0,0], [0]*7, C,
     b"\x90\x80", lambda rg,cc: rg[0]==0 and (cc&Z) and not (cc&C), "D0=0, Z set C clear"),
    ("AND.L D1,D0",          [0xFF00FF00,0x0FF00FF0,0,0,0,0,0,0], [0]*7, 0,
     b"\xC0\x81", lambda rg,cc: rg[0]==0x0F000F00,               "D0=0F000F00"),
]

def main():
    for attempt in range(10):
        try:
            p = serial.Serial(PORT, BAUD, timeout=0.5); break
        except Exception:
            time.sleep(0.4)
    else:
        print("could not open port"); return 1
    time.sleep(0.2)
    ok = 0
    for name, d, a, ccr, instr, check, exp in CASES:
        res = run(p, d, a, ccr, instr)
        if res is None:
            print(f"FAIL  {name:22} (no result)"); continue
        regs, rccr = res
        good = check(regs, rccr)
        ok += good
        print(f"{'PASS' if good else 'FAIL'}  {name:22} D0={regs[0]:08X} D3={regs[3]:08X} CCR={rccr:02X}  want {exp}")
    p.close()
    print(f"\n{ok}/{len(CASES)} cases PASS")
    return 0 if ok == len(CASES) else 1

if __name__ == "__main__":
    sys.exit(main())
