SYNC_TX=0x55; SYNC_RX=0xAA; CMD_PING=0x02; CMD_RUN=0x01
def _ck(b): return sum(b) & 0xFF
def frame(cmd, payload):
    body = bytes([cmd]) + payload
    ln = len(body) + 1                       # body + cksum
    return bytes([SYNC_TX, ln]) + body + bytes([_ck(body)])
def parse(buf):
    if len(buf) < 4 or buf[0] != SYNC_RX: return None
    ln = buf[1]; end = 2 + ln
    if len(buf) < end: return None
    body = buf[2:end-1]; ck = buf[end-1]
    if _ck(body) != ck: return None
    return body[0], body[1:]
