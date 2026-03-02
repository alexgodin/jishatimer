#!/usr/bin/env python3
"""
Sets the RTC to 2000-01-01 and resets the NTP sync counter,
so the device will show a wrong time and then sync after 15 seconds.

Usage: python3 test_ntp_sync.py [port]
  port defaults to /dev/cu.usbmodem*
"""

import sys
import glob
import serial
import time

def find_port():
    ports = glob.glob("/dev/cu.usbmodem*")
    if not ports:
        print("No /dev/cu.usbmodem* found. Pass port as argument.")
        sys.exit(1)
    return ports[0]

port = sys.argv[1] if len(sys.argv) > 1 else find_port()
print(f"Connecting to {port}...")

ser = serial.Serial(port, 115200, timeout=2)
time.sleep(0.5)  # let the connection settle

ser.write(b'T')
print("Sent 'T' command")

# Read back the confirmation
time.sleep(0.5)
while ser.in_waiting:
    print(ser.readline().decode().strip())

ser.close()
print("Done. Watch the display — it should show wrong time, then correct after ~15s.")
