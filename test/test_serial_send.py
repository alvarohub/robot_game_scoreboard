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

    #Start with a raster scan test to verify basic communication and LED control.
    print("\n── Raster scan test ──")
    if not raster_scan(ser, delay_ms=20):
        print("Raster scan failed or timed out. Continuing anyway …")     


    print("\n── Setting scores ──")
    send(ser, '/display/1/text "HELLO"')
    time.sleep(1)
    send(ser, "/display/1 1234")
    time.sleep(2)

    # print("\n── Setting colours ──")
    # send(ser, "/display/1/color 255 0 0")    # red
    # send(ser, "/display/2/color 255 0 0")
    # send(ser, "/display/3/color 0 0 255")    # blue
    # send(ser, "/display/4/color 0 0 255")
    # send(ser, "/display/5/color 0 255 0")    # green
    # send(ser, "/display/6/color 0 255 0")
    # time.sleep(2)


    print("\n── Scroll mode test ──")
    send(ser, "/display/1/scroll 1") # set scroll mode up
    send(ser, "/display/1 abcd")
    wait_scroll_done(ser, 1)
    send(ser, "/display/1 1234")
    wait_scroll_done(ser, 1)
    
    send(ser, "/display/1/scroll 2") # set scroll mode down
    send(ser, "/display/1 Hello")
    wait_scroll_done(ser, 1)
    send(ser, "/display/1 You")
    wait_scroll_done(ser, 1)

    # print("\n── Brightness breathe ──")
    # steps_per_cycle = 40        # 40 steps × 25 ms = 1 s per cycle
    # for cycle in range(4):
    #     for s in range(steps_per_cycle):
    #         # sine wave 0→1→0 over one cycle
    #         b = int(5 + 75 * (0.5 - 0.5 * math.cos(2 * math.pi * s / steps_per_cycle)))
    #         send(ser, f"/brightness {b}")
    #         time.sleep(0.5 / steps_per_cycle)


    # Fire-and-forget: send many values quickly — the queue buffers them
    # and they scroll one-by-one automatically.
    print("\n── Scroll queue test (fire-and-forget 0→20) ──")
    send(ser, "/display/1/scroll 1")
    for n in range(0, 21):
        send(ser, f"/display/1 {n}")
    # Now just wait until the board is idle (all queued scrolls finish)
    print("  Waiting for queue to drain …")
    while True:
        if not wait_scroll_done(ser, 1, timeout_sec=3.0):
            break  # timeout = no more SCROLL_DONE coming → queue is empty

    # Switch to instant mode — automatically flushes any remaining queue
    print("\n── Instant mode (flush queue) ──")
    send(ser, "/display/1/scroll 0")
    time.sleep(0.5)

    # Scroll countdown 9999→9980 with rainbow colours and scroll blank
    print("\n── Scroll countdown 9999→9980 (rainbow, blank between) ──")
    send(ser, "/display/1/scroll 2")  # scroll down
    send(ser, "/scrollblank 1")       # blank frame between items
    steps = list(range(9999, 9979, -1))
    for i, n in enumerate(steps):
        hue = i / len(steps)  # 0→1 over the countdown
        r, g, b = [int(255 * c) for c in colorsys.hsv_to_rgb(hue, 1.0, 1.0)]
        send(ser, f"/display/1/color {r} {g} {b}")
        send(ser, f"/display/1 {n}")
        time.sleep(1)
    send(ser, "/scrollblank 0")       # restore default
   

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
