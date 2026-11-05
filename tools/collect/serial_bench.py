"""Drive the UART bench console and print the replies."""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
CMDS = sys.argv[2:]

ser = serial.Serial()
ser.port = PORT
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False
ser.rts = False
ser.open()
ser.reset_input_buffer()

for cmd in CMDS:
    print(f"\n>>> {cmd}")
    ser.write((cmd + "\r\n").encode())
    ser.flush()
    deadline = time.time() + 12.0
    buf = b""
    quiet = time.time() + 1.5
    while time.time() < deadline:
        chunk = ser.read(512)
        if chunk:
            buf += chunk
            quiet = time.time() + 1.2
        elif time.time() > quiet and buf:
            break
    for line in buf.decode("utf-8", "replace").splitlines():
        line = line.rstrip("\r")
        if line.strip():
            print(line)
ser.close()
