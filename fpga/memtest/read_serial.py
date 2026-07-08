#!/usr/bin/env python3
# Raw serial reader (no pyserial). Usage: read_serial.py [device] [seconds]
import sys, os, time, select, termios

dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbserial-D01457'
dur = float(sys.argv[2]) if len(sys.argv) > 2 else 35.0

fd = os.open(dev, os.O_RDONLY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0                                             # iflag
a[1] = 0                                             # oflag
a[2] = (a[2] & ~termios.PARENB & ~termios.CSTOPB & ~termios.CSIZE) \
       | termios.CS8 | termios.CLOCAL | termios.CREAD
a[3] = a[3] & ~(termios.ICANON | termios.ECHO | termios.ISIG)
a[4] = termios.B115200                               # ispeed
a[5] = termios.B115200                               # ospeed
termios.tcsetattr(fd, termios.TCSANOW, a)

end = time.time() + dur
while time.time() < end:
    r, _, _ = select.select([fd], [], [], 0.5)
    if r:
        try:
            d = os.read(fd, 4096)
        except OSError:
            continue
        if d:
            sys.stdout.write(d.decode('latin1'))
            sys.stdout.flush()
os.close(fd)
sys.stdout.write('\n[reader done]\n')
