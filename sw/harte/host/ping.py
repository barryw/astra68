import serial, sys, time
sys.path.insert(0,'.')
from proto import frame, parse, CMD_PING
p = serial.Serial('/dev/cu.usbserial-D01457', 115740, timeout=0.5)
p.reset_input_buffer()
p.write(frame(CMD_PING, bytes([0xA3])))
time.sleep(0.2); r = p.read(8); p.close()
print("sent PING 0xA3, got:", r.hex(), "->", parse(r))
