#!/usr/bin/env python3
"""
load_game_serial.py — upload one `.game` script to the scoreboard over USB-Serial.

Requires:
    pip install pyserial

Usage:
    python3 load_game_serial.py games/demo1.game
    python3 load_game_serial.py games/demo1.game --port /dev/ttyACM0
    python3 load_game_serial.py games/demo1.game --save demo1_runtime
    python3 load_game_serial.py games/demo1.game --display 1 --trigger-text GO

Notes:
    - This uses the staged runtime-script commands:
        /script/begin
        /script/append "..."
        /script/commit
    - Optional save uses /script/save.
    - Optional trigger selects the loaded script on one display and sends text
      so the animation starts immediately.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import time

import serial
import serial.tools.list_ports


def find_serial_port() -> str | None:
    for port in serial.tools.list_ports.comports():
        if (
            "USB" in port.description
            or "CP210" in port.description
            or "CH340" in port.description
            or "ACM" in port.device
        ):
            return port.device
    ports = list(serial.tools.list_ports.comports())
    if ports:
        return ports[0].device
    return None


def read_serial_until(ser: serial.Serial, predicates, timeout_sec: float) -> tuple[bool, list[str]]:
    deadline = time.time() + timeout_sec
    lines: list[str] = []
    while time.time() < deadline:
        if ser.in_waiting:
            line = ser.readline().decode("utf-8", errors="replace").rstrip()
            if line:
                print(f"  [fw] {line}")
                lines.append(line)
                if any(predicate(line) for predicate in predicates):
                    return True, lines
        else:
            time.sleep(0.02)
    return False, lines


def send(ser: serial.Serial, cmd: str, pause_sec: float = 0.08) -> None:
    ser.write((cmd.rstrip() + "\n").encode("utf-8"))
    print(f"  → {cmd}")
    time.sleep(pause_sec)


def wait_for_firmware_ready(ser: serial.Serial, timeout_sec: float) -> None:
    ok, _ = read_serial_until(
        ser,
        predicates=[
            lambda line: line == "SCRIPT_READY",
            lambda line: line.startswith("SCRIPT_STATUS "),
        ],
        timeout_sec=min(2.0, timeout_sec),
    )
    if ok:
        return

    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        send(ser, "/script/status", pause_sec=0.05)
        ok, output = read_serial_until(
            ser,
            predicates=[
                lambda line: line == "SCRIPT_READY",
                lambda line: line.startswith("SCRIPT_STATUS "),
            ],
            timeout_sec=0.8,
        )
        if ok:
            return
        if output:
            # Boot chatter such as WiFi connection dots means the board is alive
            # and still working through startup, so keep the readiness window open.
            deadline = max(deadline, time.time() + 3.0)
    raise RuntimeError(
        "Timed out waiting for firmware to become ready after opening serial port; "
        "the board may still be booting or connecting to WiFi"
    )


def commit_with_retry(ser: serial.Serial, max_attempts: int = 2) -> None:
    for attempt in range(1, max_attempts + 1):
        if attempt > 1:
            print(f"  ↻ retrying /script/commit ({attempt}/{max_attempts})")
        send(ser, "/script/commit")
        ok, output = read_serial_until(
            ser,
            predicates=[
                lambda line: line.startswith("SCRIPT_LOADED "),
                lambda line: line.startswith("SCRIPT_ERROR "),
            ],
            timeout_sec=5.0,
        )
        if ok:
            if any(line.startswith("SCRIPT_ERROR ") for line in output):
                raise RuntimeError("Firmware reported SCRIPT_ERROR during commit")
            return

        if attempt == max_attempts:
            break

        send(ser, "/script/status", pause_sec=0.05)
        status_ok, _ = read_serial_until(
            ser,
            predicates=[lambda line: line.startswith("SCRIPT_STATUS ")],
            timeout_sec=1.0,
        )
        if not status_ok:
            break

    raise RuntimeError("Timed out waiting for SCRIPT_LOADED / SCRIPT_ERROR")


def parse_script_id(game_path: Path) -> int:
    for raw_line in game_path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line.lower().startswith("id "):
            parts = line.split(maxsplit=1)
            return int(parts[1])
    raise ValueError(f"No 'id' declaration found in {game_path}")


def iter_upload_lines(game_path: Path) -> list[str]:
    lines = game_path.read_text(encoding="utf-8").splitlines()
    for i, line in enumerate(lines, start=1):
        if '"' in line:
            raise ValueError(
                f"Line {i} contains a double quote, which the current /script/append transport "
                "cannot represent safely."
            )
    return lines


def upload_game(
    ser: serial.Serial,
    game_path: Path,
    save_name: str | None,
    display: int | None,
    trigger_text: str | None,
) -> int:
    script_id = parse_script_id(game_path)
    lines = iter_upload_lines(game_path)

    send(ser, "/script/begin")
    ok, output = read_serial_until(
        ser,
        predicates=[
            lambda line: line == "SCRIPT_UPLOAD BEGIN",
            lambda line: line.startswith("SCRIPT_ERROR "),
        ],
        timeout_sec=2.0,
    )
    if not ok:
        raise RuntimeError("Timed out waiting for SCRIPT_UPLOAD BEGIN / SCRIPT_ERROR")
    if any(line.startswith("SCRIPT_ERROR ") for line in output):
        raise RuntimeError("Firmware reported SCRIPT_ERROR during begin")

    for line in lines:
        send(ser, f'/script/append "{line}"')

    commit_with_retry(ser)

    if save_name:
        send(ser, f'/script/save "{save_name}"')
        ok, output = read_serial_until(
            ser,
            predicates=[
                lambda line: line.startswith("SCRIPT_SAVED "),
                lambda line: line.startswith("SCRIPT_ERROR "),
            ],
            timeout_sec=3.0,
        )
        if not ok:
            raise RuntimeError("Timed out waiting for SCRIPT_SAVED / SCRIPT_ERROR")
        if any(line.startswith("SCRIPT_ERROR ") for line in output):
            raise RuntimeError("Firmware reported SCRIPT_ERROR during save")

    if display is not None and trigger_text is not None:
        send(ser, f"/display/{display}/animation {script_id}")
        send(ser, f'/display/{display}/text "{trigger_text}"')

    return script_id


def main() -> int:
    parser = argparse.ArgumentParser(description="Upload one .game file over USB-Serial")
    parser.add_argument("game", type=Path, help="Path to the .game file to upload")
    parser.add_argument("--port", help="Serial port, e.g. /dev/ttyACM0 or COM3")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument(
        "--save",
        metavar="NAME",
        help="Optional SPIFFS save name or path passed to /script/save",
    )
    parser.add_argument(
        "--display",
        type=int,
        help="Optional display number to arm for the loaded script",
    )
    parser.add_argument(
        "--trigger-text",
        help="Optional text to send after --display so the animation starts immediately",
    )
    parser.add_argument(
        "--settle",
        type=float,
        default=1.5,
        help="Seconds to wait after opening the serial port",
    )
    parser.add_argument(
        "--ready-timeout",
        type=float,
        default=30.0,
        help="Seconds to wait for the firmware to become ready after opening the port",
    )
    args = parser.parse_args()

    if args.display is None and args.trigger_text is not None:
        parser.error("--trigger-text requires --display")

    game_path = args.game.resolve()
    if not game_path.exists():
        print(f"Game file not found: {game_path}")
        return 1

    port = args.port or find_serial_port()
    if not port:
        print("No serial port found. Pass one explicitly with --port.")
        return 1

    print(f"Opening {port} at {args.baud} baud …")
    ser = serial.Serial(port, args.baud, timeout=0.2)
    try:
        time.sleep(args.settle)
        ser.reset_input_buffer()
        wait_for_firmware_ready(ser, args.ready_timeout)

        print(f"Uploading {game_path.name} …")
        script_id = upload_game(
            ser,
            game_path=game_path,
            save_name=args.save,
            display=args.display,
            trigger_text=args.trigger_text,
        )
        print(f"Loaded runtime script id={script_id}")
        if args.save:
            print(f"Saved staged script as {args.save}")
        if args.display is not None and args.trigger_text is not None:
            print(f"Triggered script {script_id} on display {args.display}")
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    sys.exit(main())