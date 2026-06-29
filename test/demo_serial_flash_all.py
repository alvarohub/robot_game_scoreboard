#!/usr/bin/env python3
"""
demo_serial_flash_all.py - Serial full-display flash test for all six displays.

Flashes every display between two full-panel colors over USB-Serial. Each
frame sends one batch of six /display/N/fill commands. The controller does
not run the flashing loop; this script sends every frame.

Requires:
    pip install pyserial

Usage:
    python test/demo_serial_flash_all.py
    python test/demo_serial_flash_all.py /dev/cu.usbmodem2101
    python test/demo_serial_flash_all.py /dev/cu.usbmodem2101 --period-ms 250
    python test/demo_serial_flash_all.py /dev/cu.usbmodem2101 --a 255,0,0 --b 0,0,255
    python test/demo_serial_flash_all.py /dev/cu.usbmodem2101 --period-ms 100 --frames 50
"""

import argparse
import sys
import time

import serial
import serial.tools.list_ports


BAUD = 115200


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


def parse_color(value):
    parts = value.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("color must be R,G,B")

    try:
        color = tuple(int(part.strip()) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("color values must be integers") from exc

    if any(channel < 0 or channel > 255 for channel in color):
        raise argparse.ArgumentTypeError("color values must be in 0..255")
    return color


def color_text(color):
    return f"{color[0]},{color[1]},{color[2]}"


def write_lines(ser, commands):
    """Write several serial commands as one batch to reduce display skew."""
    payload = "".join(cmd.strip() + "\n" for cmd in commands)
    ser.write(payload.encode("utf-8"))
    ser.flush()


def init_displays(ser, initial_color):
    red, green, blue = initial_color
    commands = [
        "/serial/quiet 1",
        "/animation/stop",
        "/animation 0",
        "/clearqueue",
        "/particles/enable 0",
        "/particles/pause 1",
        "/text/clear",
        "/mode text",
        "/text/enable 0",
    ]
    for display in range(1, 7):
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
            f"/display/{display}/text/enable 0",
            f"/display/{display}/fill {red} {green} {blue}",
        ])
    write_lines(ser, commands)


def run_flash(ser, color_a, color_b, period_ms, frames=None):
    period_s = max(1, period_ms) / 1000.0
    next_frame = time.monotonic()
    sent = 0
    colors = (color_a, color_b)

    while frames is None or sent < frames:
        color = colors[sent % 2]
        red, green, blue = color
        write_lines(ser, [f"/display/{display}/fill {red} {green} {blue}" for display in range(1, 7)])
        print(f"\rframe {sent + 1} color {color_text(color)}", end="", flush=True)
        sent += 1

        next_frame += period_s
        delay = next_frame - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        else:
            next_frame = time.monotonic()


def main():
    parser = argparse.ArgumentParser(description="Serial full-display flash test for all six scoreboard displays.")
    parser.add_argument("port", nargs="?", help="Serial port. Auto-detected if omitted.")
    parser.add_argument("--period-ms", type=int, default=250, help="Update period in milliseconds (default: 250).")
    parser.add_argument("--frames", type=int, help="Stop after this many frames. Default: run until Ctrl+C.")
    parser.add_argument("--a", type=parse_color, default=(255, 0, 0), help="First color as R,G,B (default: 255,0,0).")
    parser.add_argument("--b", type=parse_color, default=(0, 0, 255), help="Second color as R,G,B (default: 0,0,255).")
    args = parser.parse_args()

    port = normalize_port(args.port) or find_serial_port()
    if not port:
        print("No serial port found. Pass the port as an argument.", file=sys.stderr)
        return 1

    print(f"Opening {port} at {BAUD} baud")
    print(f"Flashing all displays between {color_text(args.a)} and {color_text(args.b)} every {args.period_ms} ms")
    if args.period_ms < 50:
        print("Warning: full-chain NeoPixel refresh is about 46 ms before overhead; periods below 50 ms are expected to jitter.")
    print("Press Ctrl+C to stop.\n")

    with serial.Serial(port, BAUD, timeout=0.05, write_timeout=1) as ser:
        time.sleep(0.2)
        ser.reset_input_buffer()
        init_displays(ser, args.a)
        time.sleep(0.1)
        try:
            run_flash(ser, args.a, args.b, args.period_ms, args.frames)
        except KeyboardInterrupt:
            print("\nStopped.")
        finally:
            write_lines(ser, ["/serial/quiet 0"])
        print()
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
