"""Read COM4 for a fixed time and print the ESP32 log."""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM4"
SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

ser = serial.Serial()
ser.port = PORT
ser.baudrate = 115200
ser.timeout = 0.2
ser.dtr = False
ser.rts = False
ser.open()

# reset pulse: assert EN low then release (CH343 auto-download circuit)
try:
    ser.dtr = False
    ser.rts = True
    time.sleep(0.15)
    ser.rts = False
    ser.dtr = False
except Exception as exc:  # noqa: BLE001
    print("reset pulse failed:", exc)

t0 = time.time()
buf = b""
while time.time() - t0 < SECONDS:
    chunk = ser.read(512)
    if not chunk:
        continue
    buf += chunk
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        text = line.decode("utf-8", "replace").rstrip("\r")
        if text.strip():
            print(f"[{(time.time() - t0):6.2f}] {text}")
ser.close()
print("--- end ---")
