import serial
import serial.tools.list_ports
import time
import sys
import os

def find_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "USB" in port.description or "usbmodem" in port.device:
            return port.device
    return None

port = find_port()
if not port:
    print("Could not find serial port")
    sys.exit(1)

print(f"Connecting to {port}...")
ser = serial.Serial(port, 115200, timeout=2)

def send_command(cmd, wait=1):
    print(f"Sending: {cmd}")
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    lines = []
    # Read all available lines
    while ser.in_waiting:
        line = ser.readline().decode(errors='ignore').strip()
        if line:
            lines.append(line)
        time.sleep(0.01)
    return lines

# Initial wait for boot
time.sleep(3)
ser.read_all()

# Send list command
results = send_command("/script/bank/list")
for line in results:
    if "BANK_SLOTS" in line:
        print(f"RESULT: {line}")

ser.close()
