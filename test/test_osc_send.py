#!/usr/bin/env python3
"""
test_osc_send.py — Quick smoke-test for the Robot Game Scoreboard.

Usage:
    python test_osc_send.py <board_ip> [port]

Requires:
    pip install python-osc
"""

import sys
import time
from pythonosc import udp_client


def main():
    ip   = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.42"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 9000

    client = udp_client.SimpleUDPClient(ip, port)
    print(f"Sending OSC to {ip}:{port}\n")

    # ── 1. Set scores on all six displays ─────────────────────
    print("Setting scores on all displays …")
    scores = ["1234", "5678", "90", "42", "HI", "GO!"]
    for i, score in enumerate(scores, start=1):
        client.send_message(f"/display/{i}", score)
        print(f"  /display/{i} ← \"{score}\"")
        time.sleep(0.3)

    time.sleep(2)

    # ── 2. Colour each display ────────────────────────────────
    print("\nSetting colours …")
    colours = [
        (255,   0,   0),   # red
        (  0, 255,   0),   # green
        (  0,   0, 255),   # blue
        (255, 255,   0),   # yellow
        (255,   0, 255),   # magenta
        (  0, 255, 255),   # cyan
    ]
    for i, (r, g, b) in enumerate(colours, start=1):
        client.send_message(f"/display/{i}/color", [r, g, b])
        print(f"  /display/{i}/color ← ({r}, {g}, {b})")
        time.sleep(0.3)

    time.sleep(2)

    # ── 3. Brightness sweep ───────────────────────────────────
    print("\nBrightness sweep 10 → 80 → 10 …")
    for b in list(range(10, 81, 10)) + list(range(80, 9, -10)):
        client.send_message("/brightness", b)
        time.sleep(0.15)

    time.sleep(1)

    # ── 4. Clear all ──────────────────────────────────────────
    print("\nClearing all displays …")
    client.send_message("/clearall", [])

    print("\nDone!")


if __name__ == "__main__":
    main()
