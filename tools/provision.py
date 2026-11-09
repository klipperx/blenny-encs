#!/usr/bin/env python3
"""Assign a badge to an attendee over USB serial after the filesystem is flashed."""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ROSTER = ROOT / "data" / "attendees.json"
# Site Wi-Fi seeded onto each badge after SETID. Gitignored; never goes in data/ (that's
# packed into the LittleFS image) or the firmware.
WIFI = ROOT / "wifi.local.json"


# Fleet roster key for encrypted OTA roster updates. Lives outside the repo.
ROSTER_KEY = Path.home() / ".blenny" / "roster-key.hex"


def load_roster_key() -> str | None:
    if not ROSTER_KEY.exists():
        return None
    key = ROSTER_KEY.read_text().strip()
    return key if len(key) == 64 else None


def seed_badge(port: str, talk_fn, no_wifi: bool = False) -> list[str]:
    """After SETID: push site Wi-Fi and the roster key. Returns status lines (no secrets)."""
    notes = []
    wifi = None if no_wifi else load_wifi()
    if wifi:
        ssid, password = wifi
        reply = talk_fn(port, f"SETWIFI {ssid}\t{password}")
        notes.append(f"wifi {ssid}: {'saved' if f'OK WIFI {ssid}' in reply else 'no reply'}")
    key = load_roster_key()
    if key:
        reply = talk_fn(port, f"SETROSTERKEY {key}")
        notes.append(f"roster key: {'saved' if 'OK ROSTERKEY' in reply else 'no reply'}")
    else:
        notes.append(f"roster key: none at {ROSTER_KEY} (OTA roster updates disabled)")
    return notes


def load_wifi() -> tuple[str, str] | None:
    if not WIFI.exists():
        return None
    data = json.loads(WIFI.read_text())
    ssid = str(data.get("ssid", "")).strip()
    if not ssid:
        return None
    return ssid, str(data.get("password", ""))


def load_roster() -> list[dict]:
    data = json.loads(ROSTER.read_text())
    people = data.get("attendees", [])
    if not people:
        raise SystemExit(f"no attendees in {ROSTER}")
    return people


def find_port(explicit: str | None) -> str:
    try:
        from serial.tools import list_ports
    except ImportError as exc:
        raise SystemExit("pip install pyserial") from exc

    if explicit:
        return explicit
    ports = list(list_ports.comports())
    for p in ports:
        blob = f"{p.device} {p.description} {p.hwid}".lower()
        if any(s in blob for s in ("usbmodem", "wch", "esp32", "usbserial", "lilygo", "cdc")):
            return p.device
    if len(ports) == 1:
        return ports[0].device
    if not ports:
        raise SystemExit("no serial ports found")
    print("ports:")
    for p in ports:
        print(f"  {p.device:24} {p.description}")
    raise SystemExit("pass --port")


def talk(port: str, line: str, wait: float = 2.0) -> str:
    import serial

    ser = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(1.6)  # S3 CDC reboots when the port opens
    ser.reset_input_buffer()
    ser.write(b"\n")
    time.sleep(0.1)
    ser.write((line.strip() + "\n").encode())
    deadline = time.time() + wait
    chunks: list[str] = []
    while time.time() < deadline:
        raw = ser.read(256)
        if raw:
            chunks.append(raw.decode("utf-8", "replace"))
            if "OK " in "".join(chunks) or "ERR " in "".join(chunks) or "ID " in "".join(chunks):
                if "".join(chunks).endswith("\n"):
                    break
        time.sleep(0.05)
    ser.close()
    return "".join(chunks)


def main() -> None:
    people = load_roster()
    parser = argparse.ArgumentParser(description="Provision a blenny badge")
    parser.add_argument("--port", help="USB serial device")
    parser.add_argument("--id", help="attendee id from data/attendees.json")
    parser.add_argument("--list", action="store_true", help="print roster and exit")
    parser.add_argument("--who", action="store_true", help="ask the badge who it is")
    parser.add_argument("--no-wifi", action="store_true", help="skip seeding wifi.local.json")
    args = parser.parse_args()

    if args.list or (not args.id and not args.who):
        for a in people:
            print(f"{a['id']}\t{a['name']}\t{a['truth']}")
        if args.list or not args.id:
            if not args.who and not args.id:
                return

    port = find_port(args.port)
    print(f"port {port}")

    if args.who:
        print(talk(port, "WHO"))
        return

    ids = {a["id"]: a for a in people}
    if args.id not in ids:
        raise SystemExit(f"unknown id {args.id!r}. known: {', '.join(ids)}")
    person = ids[args.id]
    out = talk(port, f"SETID {args.id}")
    print(out.strip() or "(no reply — is firmware running?)")
    if "OK" in out:
        print(f"badge is now {person['name']} ({person['id']})")
        for note in seed_badge(port, talk, no_wifi=args.no_wifi):
            print(note)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(1)
