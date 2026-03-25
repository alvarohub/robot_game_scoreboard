#!/usr/bin/env python3
"""
test_serial_send.py — smoke-test the scoreboard via USB-Serial commands.

Requires:  pip install pyserial

Usage:
    python test_serial_send.py               # auto-detect first USB serial port
    python test_serial_send.py /dev/ttyACM0  # explicit port
    python test_serial_send.py COM3          # Windows
"""

import sys
import time
import serial
import serial.tools.list_ports


def find_serial_port():
    """Auto-detect the first USB serial port."""
    ports = serial.tools.list_ports.comports()
    for p in ports:
        # Common ESP32 / M5Stack USB-CDC identifiers
        if "USB" in p.description or "CP210" in p.description or "CH340" in p.description or "ACM" in p.device:
            return p.device
    if ports:
        return ports[0].device
    return None


def send(ser, cmd):
    """Send a command line and pause briefly."""
    line = cmd.strip() + "\n"
    ser.write(line.encode("utf-8"))
    print(f"  → {cmd}")
    time.sleep(0.15)


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_serial_port()
    if port is None:
        print("No serial port found. Pass the port as an argument.")
        sys.exit(1)

    baud = 115200
    print(f"Opening {port} at {baud} baud …")
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(2)  # wait for ESP32 to reset after DTR toggle

    # Drain any boot messages
    while ser.in_waiting:
        print(ser.readline().decode("utf-8", errors="replace"), end="")

    print("\n── Setting scores ──")
    send(ser, '/display/1/text "P1"')
    send(ser, "/display/2 100")
    send(ser, '/display/3/text "P2"')
    send(ser, "/display/4 200")
    send(ser, '/display/5/text "TM"')
    send(ser, "/display/6 59")
    time.sleep(2)

    print("\n── Setting colours ──")
    send(ser, "/display/1/color 255 0 0")    # red
    send(ser, "/display/2/color 255 0 0")
    send(ser, "/display/3/color 0 0 255")    # blue
    send(ser, "/display/4/color 0 0 255")
    send(ser, "/display/5/color 0 255 0")    # green
    send(ser, "/display/6/color 0 255 0")
    time.sleep(2)

    print("\n── Brightness sweep ──")
    for b in [10, 40, 80, 40, 20]:
        send(ser, f"/brightness {b}")
        time.sleep(0.5)

    print("\n── Scroll mode test ──")
    send(ser, "/display/2/scroll 1")
    send(ser, "/display/2 999")
    time.sleep(1)
    send(ser, "/display/2 888")
    time.sleep(1)
    send(ser, "/display/2/scroll 0")

    print("\n── Clear all ──")
    send(ser, "/clearall")
    time.sleep(1)

    # Print any responses from the board
    print("\n── Board output ──")
    deadline = time.time() + 2
    while time.time() < deadline:
        if ser.in_waiting:
            print(ser.readline().decode("utf-8", errors="replace"), end="")
        else:
            time.sleep(0.05)

    ser.close()
    print("\nDone.")


if __name__ == "__main__":
    main()
