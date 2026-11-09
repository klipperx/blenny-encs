#!/usr/bin/env python3
"""Pack a readable agenda into the badge's compact data/schedule.json.

Input (e.g. roster.local/schedule.local.json):
  {"tz": "MST7MDT,M3.2.0,M11.1.0",
   "days": [{"date": "2026-11-02", "label": "Mon 11/2",
             "items": [["08:30", "12:00", "Arrivals"], ...]}]}

Output adds a YYMMDDHHMM "version" (fits uint32; badges install newer versions over the
air) and stores times as minutes after midnight. Ship it with
  tools/ota_release.py ... --schedule data/schedule.json --roster-key ~/.blenny/roster-key.hex
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def minutes(hhmm: str) -> int:
    h, _, m = hhmm.partition(":")
    value = int(h) * 60 + int(m)
    if not 0 <= value < 24 * 60:
        raise ValueError(hhmm)
    return value


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source")
    ap.add_argument("--out", default=str(ROOT / "data" / "schedule.json"))
    args = ap.parse_args()

    src = json.loads(Path(args.source).read_text())
    days = []
    for d in src["days"]:
        items = []
        for start, end, title in d["items"]:
            s, e = minutes(start), minutes(end)
            if e <= s:
                sys.exit(f"{d['date']} {title!r}: ends before it starts")
            items.append([s, e, " ".join(title.split())])
        items.sort(key=lambda it: it[0])
        days.append({"d": d["date"], "l": d.get("label", d["date"]), "s": items})
    out = {"version": int(time.strftime("%y%m%d%H%M")), "tz": src.get("tz", ""), "days": days}
    Path(args.out).write_text(json.dumps(out, separators=(",", ":")) + "\n")
    count = sum(len(d["s"]) for d in days)
    print(f"schedule v{out['version']}: {len(days)} days, {count} sessions -> {args.out}")


if __name__ == "__main__":
    main()
