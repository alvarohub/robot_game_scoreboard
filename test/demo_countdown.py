#!/usr/bin/env python3
"""
demo_countdown.py — Counts down from 1000 to 0 on display 1,
updating every 0.5 seconds.

Usage:
    python demo_countdown.py <board_ip> [port] [start] [step_ms]

Examples:
    python demo_countdown.py 192.168.1.42
    python demo_countdown.py 192.168.1.42 9000 500 250

Requires:
    pip install python-osc
"""

import sys
import time
from pythonosc import udp_client


def main():
    ip       = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.42"
    port     = int(sys.argv[2]) if len(sys.argv) > 2 else 9000
    start    = int(sys.argv[3]) if len(sys.argv) > 3 else 1000
    step_ms  = int(sys.argv[4]) if len(sys.argv) > 4 else 500

    client = udp_client.SimpleUDPClient(ip, port)
    step_s = step_ms / 1000.0

    print(f"Countdown {start} → 0  on /display/1")
    print(f"Target: {ip}:{port}  interval: {step_ms} ms\n")

    for n in range(start, -1, -1):
        client.send_message("/display/1", str(n))
        print(f"\r  {n:>5}", end="", flush=True)
        time.sleep(step_s)

    print("\n\nDone!")


if __name__ == "__main__":
    main()
