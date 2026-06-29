#!/usr/bin/env python3
"""
demo_serial_count_all.py - Serial display update test for all six displays.

Counts 0000 -> 9999 -> 0000 continuously over USB-Serial. Each frame
sends one batch of six immediate text commands, one per display. The
controller does not run the counter; this script sends every value.

Requires:
    pip install pyserial

Usage:
    python test/demo_serial_count_all.py
    python test/demo_serial_count_all.py /dev/cu.usbmodem2101
    python test/demo_serial_count_all.py /dev/cu.usbmodem2101 --period-ms 100
    python test/demo_serial_count_all.py /dev/cu.usbmodem2101 --period-ms 100 --frames 50
"""

import argparse
import sys
import time

import serial
import serial.tools.list_ports


BAUD = 115200
DISPLAY_COLORS = [
    (255, 0, 0),      # D1 red
    (255, 140, 0),    # D2 orange
    (255, 255, 0),    # D3 yellow
    (0, 255, 0),      # D4 green
    (0, 120, 255),    # D5 blue
    (180, 0, 255),    # D6 violet
]


def find_serial_port():
    """Auto-detect the first likely USB serial port."""
    ports = list(serial.tools.list_ports.comports())
    for port in ports:
        desc = port.description or ""
        if (
            "USB" in desc
            or "CP210" in desc
            or "CH340" in desc
            or "ACM" in port.device
            or "usbmodem" in port.device
            or "usbserial" in port.device
        ):
            return port.device
    return ports[0].device if ports else None


def normalize_port(port):
    if port and (port.startswith("cu.") or port.startswith("tty.")):
        return f"/dev/{port}"
    return port


def write_lines(ser, commands):
    """Write several serial commands as one batch to reduce display skew."""
    payload = "".join(cmd.strip() + "\n" for cmd in commands)
    ser.write(payload.encode("utf-8"))
    ser.flush()


def init_displays(ser):
    commands = [
        "/serial/quiet 1",
        "/animation/stop",
        "/animation 0",
        "/clearqueue",
        "/particles/enable 0",
        "/particles/pause 1",
        "/text/clear",
        "/mode text",
        "/text/enable 1",
    ]
    for display, (red, green, blue) in enumerate(DISPLAY_COLORS, start=1):
        commands.extend([
            f"/display/{display}/animation/stop",
            f"/display/{display}/animation 0",
            f"/display/{display}/clearqueue",
            f"/display/{display}/particles/clear",
            f"/display/{display}/particles/enable 0",
            f"/display/{display}/particles/pause 1",
            f"/display/{display}/particles/resettransform",
            f"/display/{display}/text/clear",
            f"/display/{display}/mode text",
            f"/display/{display}/text/enable 1",
            f"/display/{display}/text/brightness 255",
            f"/display/{display}/color {red} {green} {blue}",
            f'/display/{display}/text "0000"',
        ])
    write_lines(ser, commands)


def next_value(value, direction):
    value += direction
    if value >= 9999:
        return 9999, -1
    if value <= 0:
        return 0, 1
    return value, direction


def run_counter(ser, period_ms, frames=None):
    period_s = max(1, period_ms) / 1000.0
    value = 0
    direction = 1
    next_frame = time.monotonic()
    sent = 0

    while frames is None or sent < frames:
        text = f"{value:04d}"
        write_lines(ser, [f'/display/{display}/text "{text}"' for display in range(1, 7)])
        print(f"\r{text}", end="", flush=True)
        sent += 1

        value, direction = next_value(value, direction)
        next_frame += period_s
        delay = next_frame - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        else:
            next_frame = time.monotonic()


def main():
    parser = argparse.ArgumentParser(description="Serial count test for all six scoreboard displays.")
    parser.add_argument("port", nargs="?", help="Serial port. Auto-detected if omitted.")
    parser.add_argument("--period-ms", type=int, default=250, help="Update period in milliseconds (default: 250).")
    parser.add_argument("--frames", type=int, help="Stop after this many frames. Default: run until Ctrl+C.")
    args = parser.parse_args()

    port = normalize_port(args.port) or find_serial_port()
    if not port:
        print("No serial port found. Pass the port as an argument.", file=sys.stderr)
        return 1

    print(f"Opening {port} at {BAUD} baud")
    print(f"Counting all displays 0000 -> 9999 -> 0000 every {args.period_ms} ms")
    if args.period_ms < 50:
        print("Warning: full-chain NeoPixel refresh is about 46 ms before overhead; periods below 50 ms are expected to jitter.")
    print("Press Ctrl+C to stop.\n")

    with serial.Serial(port, BAUD, timeout=0.05, write_timeout=1) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        init_displays(ser)
        time.sleep(0.1)
        try:
            run_counter(ser, args.period_ms, args.frames)
        except KeyboardInterrupt:
            print("\nStopped.")
        finally:
            write_lines(ser, ["/serial/quiet 0"])
        print()
        return 0


if __name__ == "__main__":
    raise SystemExit(main())