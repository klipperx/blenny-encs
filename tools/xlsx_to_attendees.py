#!/usr/bin/env python3
"""Convert a C3CON Game Answers spreadsheet into data/attendees.json (local only)."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUT = ROOT / "data" / "attendees.json"

COL_EMAIL = "Email"
COL_FIRST = "Attendee First Name"
COL_LAST = "Attendee Last Name"
COL_FACT = (
    "Tell us an interesting fact about yourself that no one "
    "(or very few) people at the meeting will know"
)


def email_to_id(email: str) -> str:
    local = email.strip().split("@", 1)[0].strip().lower()
    if not local:
        raise ValueError(f"empty local-part in email {email!r}")
    # Keep ids filesystem/serial-friendly.
    cleaned = re.sub(r"[^a-z0-9._+-]+", "", local)
    if not cleaned:
        raise ValueError(f"no usable id from email {email!r}")
    return cleaned


def cell_str(value: object) -> str:
    if value is None:
        return ""
    return str(value).strip()


def load_rows(path: Path) -> list[dict]:
    try:
        from openpyxl import load_workbook
    except ImportError as exc:
        raise SystemExit("pip install openpyxl") from exc

    wb = load_workbook(path, read_only=True, data_only=True)
    ws = wb.active
    rows_iter = ws.iter_rows(values_only=True)
    try:
        header = [cell_str(h) for h in next(rows_iter)]
    except StopIteration:
        raise SystemExit(f"{path} has no rows") from None

    idx = {name: i for i, name in enumerate(header) if name}
    required = (COL_EMAIL, COL_FIRST, COL_LAST, COL_FACT)
    missing = [c for c in required if c not in idx]
    if missing:
        raise SystemExit(
            f"missing columns: {missing}\nfound: {header}"
        )

    attendees: list[dict] = []
    seen: dict[str, int] = {}
    for row_num, row in enumerate(rows_iter, start=2):
        def get(col: str) -> str:
            i = idx[col]
            return cell_str(row[i]) if i < len(row) else ""

        email = get(COL_EMAIL)
        first = get(COL_FIRST)
        last = get(COL_LAST)
        truth = get(COL_FACT)
        if not email and not first and not last and not truth:
            continue
        if not email or not first or not last or not truth:
            print(
                f"skip row {row_num}: need email, first, last, and fact "
                f"(got email={email!r} first={first!r} last={last!r} "
                f"fact={bool(truth)})",
                file=sys.stderr,
            )
            continue
        try:
            pid = email_to_id(email)
        except ValueError as exc:
            print(f"skip row {row_num}: {exc}", file=sys.stderr)
            continue
        if pid in seen:
            print(
                f"skip row {row_num}: duplicate id {pid!r} "
                f"(first seen row {seen[pid]})",
                file=sys.stderr,
            )
            continue
        seen[pid] = row_num
        attendees.append(
            {"id": pid, "name": f"{first} {last}", "truth": truth}
        )

    wb.close()
    if not attendees:
        raise SystemExit("no attendees parsed")
    return attendees


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert C3CON Game Answers .xlsx to attendees.json"
    )
    parser.add_argument("xlsx", type=Path, help="path to .xlsx")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=DEFAULT_OUT,
        help=f"output JSON (default: {DEFAULT_OUT})",
    )
    args = parser.parse_args()

    if not args.xlsx.exists():
        raise SystemExit(f"missing {args.xlsx}")

    attendees = load_rows(args.xlsx)
    payload = {"attendees": attendees}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {len(attendees)} attendees -> {args.output}")


if __name__ == "__main__":
    main()
