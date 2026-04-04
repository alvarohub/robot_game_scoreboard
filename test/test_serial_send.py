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
import colorsys
import serial
import serial.tools.list_ports
import math


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

def raster_scan(ser, delay_ms=30):
    """Send the /rasterscan command to light each LED in sequence."""
    send(ser, f"/rasterscan {delay_ms}")
    # Wait for the scan to finish — it prints "RASTER_SCAN_DONE" when done
    t0 = time.time()
    while time.time() - t0 < 10:  # timeout after 10 seconds
        if ser.in_waiting:
            line = ser.readline().decode("utf-8", errors="replace").rstrip()
            print(f"  [fw] {line}")
            if "RASTER_SCAN_DONE" in line:
                print("Raster scan complete!")
                return True
        else:
            time.sleep(0.01)
    print("⚠ timeout waiting for raster scan to complete")
    return False    


def wait_scroll_done(ser, display_num=1, timeout_sec=2.0):
    """Wait until the firmware sends 'SCROLL_DONE <N>' for the given display.

    Returns True if SCROLL_DONE was received, False on timeout.
    Any other serial output is printed as it arrives.
    """
    tag = f"SCROLL_DONE {display_num}"
    t0 = time.time()
    while time.time() - t0 < timeout_sec:
        if ser.in_waiting:
            line = ser.readline().decode("utf-8", errors="replace").rstrip()
            if tag in line:
                return True
            if line:
                print(f"  [fw] {line}")
        else:
            time.sleep(0.01)
    print(f"  ⚠ timeout waiting for {tag}")
    return False


def wait_for_ready(ser, timeout_sec=25):
    """Read serial lines until we see 'Ready' from the firmware.

    Returns True if 'Ready' was seen, False on timeout.
    """
    t0 = time.time()
    while time.time() - t0 < timeout_sec:
        if ser.in_waiting:
            line = ser.readline().decode("utf-8", errors="replace")
            print(f"  [boot] {line}", end="" if line.endswith("\n") else "\n")
            if "Ready" in line:
                print("Board is ready!")
                return True
        else:
            time.sleep(0.1)
    return False


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_serial_port()
    if port is None:
        print("No serial port found. Pass the port as an argument.")
        sys.exit(1)

    baud = 115200
    print(f"Opening {port} at {baud} baud …")
    ser = serial.Serial(port, baud, timeout=1)

    # Opening the port may reset the ESP32-S3 (DTR toggle).
    # Wait for the firmware to finish booting and print "Ready".
    # print("Waiting for board to be ready …")
    # if not wait_for_ready(ser, timeout_sec=5):
    #     print("No 'Ready' seen (board may already be running). Proceeding …")

    # ── Particle mode ─────────────────────────────────────────
    print("\n── Particle mode (tilt the board!) ──")
    send(ser, "/display/1/color 0 255 100")   # bright green
    send(ser, "/display/1/mode particles")
    print("  Running particles for 10 s — tilt the AtomS3 to move them …")
    time.sleep(10)

    # Back to text mode
    print("\n── Back to text mode ──")
    send(ser, "/display/1/mode text")
    send(ser, "/display/1/color 255 255 255")
    send(ser, '/display/1/text "DONE"')
    time.sleep(2)

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
