import serial
import serial.tools.list_ports
import time
import sys

def find_port():
    ports = serial.tools.list_ports.comports()
    for port in ports:
        if "USB" in port.description or "usbmodem" in port.device:
            return port.device
    return None

def send_command(ser, cmd, wait=0.5):
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    output = []
    while ser.in_waiting:
        line = ser.readline().decode(errors='ignore').strip()
        if line:
            output.append(line)
            print(f"DEVICE: {line}")
        time.sleep(0.01)
    return output

port = find_port()
if not port:
    print("Could not find serial port")
    sys.exit(1)

print(f"Connecting to {port}...")
ser = serial.Serial(port, 115200, timeout=1)
time.sleep(3)
ser.read_all()

commands = [
    "/script/begin",
    "/script/append \"LABEL:demo_finite_goto_pulse\"",
    "/script/append \"step\"",
    "/script/append \"pulse\"",
    "/script/append \"goto 1\"",
    "/script/bank/commit 1",
    "/script/bank/list"
]

for cmd in commands:
    print(f"SENDING: {cmd}")
    send_command(ser, cmd)

ser.close()
