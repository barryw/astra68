import serial, time, sys
p = serial.Serial('/dev/cu.usbserial-D01457', 115200, timeout=0.2, rtscts=False, dsrdtr=False)
p.dtr = False; p.rts = False
p.reset_input_buffer()
end = time.time() + float(sys.argv[1])
buf = b''
while time.time() < end:
    buf += p.read(512)
p.close()
sys.stdout.buffer.write(buf)
sys.stderr.write("\n[read %d bytes]\n" % len(buf))
