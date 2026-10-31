#!/usr/bin/env python3
"""Send CLI commands to a Wokwi ESP32 console over RFC2217.

Examples:
    python scripts/hub.py "cocis status"
    python scripts/hub.py "help"
    python scripts/hub.py                       # interactive mode
    python scripts/hub.py -p r1 "status"        # Room 1 (port 4001)
    python scripts/hub.py -p r2 "sim motion on" # Room 2 (port 4002)
    python scripts/hub.py --wait 60 "help"      # wait for the sim to resume

Wokwi pauses the simulation (and stops reading/writing the UART) whenever its
tab is hidden, so keep the simulator tab visible while this runs. With --wait,
you can start this first and then click the simulator tab; it will send the
command as soon as the console answers.

Requires pyserial: python -m pip install pyserial
"""

import argparse
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is missing: python -m pip install pyserial")

PORTS = {"hub": 4000, "r1": 4001, "r2": 4002}

ANSI = re.compile(rb"\x1b\[[0-9;?]*[ -/]*[@-~]")
BACKSPACE = re.compile(rb"\x08 \x08")

PAUSED_HELP = (
    "no console response: the Wokwi simulator is paused or not running.\n"
    "Bring its tab into view in VS Code, then re-run (or use --wait 60 to wait)."
)


def clean(raw):
    """Decode bytes, strip ANSI colours and backspace echoes, normalise EOLs."""
    raw = ANSI.sub(b"", raw)
    raw = BACKSPACE.sub(b"", raw)
    text = raw.decode("utf-8", errors="replace")
    return text.replace("\r\n", "\n").replace("\r", "\n")


def drain(ser, idle, overall):
    """Read until the line stays quiet for `idle` seconds or `overall` elapses."""
    buf = bytearray()
    start = last = time.monotonic()
    while True:
        waiting = ser.in_waiting
        if waiting:
            buf += ser.read(waiting)
            last = time.monotonic()
            continue
        now = time.monotonic()
        if buf and now - last >= idle:
            break
        if now - start >= overall:
            break
        time.sleep(0.02)
    return bytes(buf)


def send_line(ser, text, idle, overall):
    ser.write(text.encode("utf-8") + b"\r\n")
    ser.flush()
    return drain(ser, idle, overall)


def probe(ser):
    """Send an empty line; the firmware answers with its prompt when alive."""
    return send_line(ser, "", idle=0.3, overall=0.8)


def wait_for_console(ser, seconds):
    """Keep probing until the console answers or `seconds` elapse."""
    deadline = time.monotonic() + seconds
    while True:
        reply = probe(ser)
        if reply:
            return reply
        if time.monotonic() >= deadline:
            return b""
        time.sleep(0.3)


def main():
    parser = argparse.ArgumentParser(
        description="Send a command to a Wokwi serial console over RFC2217.")
    parser.add_argument("command", nargs="*",
                        help="command to send; omit for interactive mode")
    parser.add_argument("-p", "--port", default="hub",
                        help="hub, r1, r2 or a TCP port number (default: hub/4000)")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--wait", type=float, default=0,
                        help="seconds to wait for the console to respond (default: 0)")
    parser.add_argument("--idle", type=float, default=0.5,
                        help="seconds of silence that ends a reply (default: 0.5)")
    parser.add_argument("--timeout", type=float, default=6.0,
                        help="maximum seconds to wait for a reply (default: 6)")
    args = parser.parse_args()

    port = PORTS.get(str(args.port).lower(), args.port)
    url = f"rfc2217://{args.host}:{port}"

    try:
        ser = serial.serial_for_url(
            url, baudrate=args.baud, timeout=0.1,
            rtscts=False, dsrdtr=False)
    except Exception as exc:  # noqa: BLE001 - report any connection failure
        sys.exit(f"cannot connect to {url}: {exc}\n"
                 "Is 'Wokwi: Start Simulator' running for this project?")

    try:
        boot = drain(ser, idle=0.2, overall=0.8)
        if boot:
            print(clean(boot), end="")

        alive = wait_for_console(ser, args.wait) if args.wait > 0 else probe(ser)
        if not alive:
            print(PAUSED_HELP)
            return 2
        print(clean(alive), end="")

        if args.command:
            reply = send_line(ser, " ".join(args.command), args.idle, args.timeout)
            print(clean(reply), end="")
            return 0

        print("Type a command, or 'quit' to exit. Keep the simulator tab visible.")
        while True:
            try:
                line = input("> ")
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if line.strip().lower() in ("quit", "exit"):
                break
            if not line.strip():
                continue
            reply = send_line(ser, line, args.idle, args.timeout)
            print(clean(reply) if reply else "(no reply)", end="")
        return 0
    finally:
        ser.close()


if __name__ == "__main__":
    raise SystemExit(main())
