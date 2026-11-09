from __future__ import annotations

import argparse
import json
import sys
import zipfile
from pathlib import Path

import pytest

# tools/ is on pythonpath via pytest.ini
import flash as flash_mod
import package as package_mod
import provision as provision_mod
import xlsx_to_attendees as xlsx_mod


def test_email_to_id():
    assert xlsx_mod.email_to_id("Ada.Lovelace@example.com") == "ada.lovelace"
    assert xlsx_mod.email_to_id("  Foo+Bar@x.org ") == "foo+bar"
    with pytest.raises(ValueError):
        xlsx_mod.email_to_id("@only-domain.com")


def test_xlsx_load_rows_first_last_and_skips(tmp_path: Path):
    from openpyxl import Workbook

    wb = Workbook()
    ws = wb.active
    ws.append(
        [
            xlsx_mod.COL_EMAIL,
            xlsx_mod.COL_FIRST,
            xlsx_mod.COL_LAST,
            xlsx_mod.COL_FACT,
        ]
    )
    ws.append(["ada@example.com", "Ada", "Lovelace", "Invented programming"])
    ws.append(["", "Skip", "Me", ""])  # incomplete
    ws.append(["grace@example.com", "Grace", "Hopper", "Debugged a moth"])
    ws.append(["ada@example.com", "Dup", "Ada", "Duplicate id"])  # dup
    path = tmp_path / "roster.xlsx"
    wb.save(path)

    rows = xlsx_mod.load_rows(path)
    assert len(rows) == 2
    assert rows[0] == {
        "id": "ada",
        "name": "Ada Lovelace",
        "truth": "Invented programming",
    }
    assert rows[1]["id"] == "grace"


def test_package_one_zip_contents(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    root = tmp_path / "proj"
    env = "t-display-s3-touch"
    build = root / ".pio" / "build" / env
    build.mkdir(parents=True)
    for name in ("bootloader.bin", "partitions.bin", "firmware.bin"):
        (build / name).write_bytes(b"BIN:" + name.encode())
    fs = build / "littlefs.bin"
    fs.write_bytes(b"FS")
    boot_app0 = tmp_path / "boot_app0.bin"
    boot_app0.write_bytes(b"APP0")

    monkeypatch.setattr(package_mod, "ROOT", root)
    monkeypatch.setattr(package_mod, "DIST", root / "dist")
    monkeypatch.setattr(package_mod, "BOOT_APP0", boot_app0)

    zpath = package_mod.package_one("cst328", env, fs)
    assert zpath.exists()
    with zipfile.ZipFile(zpath) as zf:
        names = set(zf.namelist())
    assert names == {
        "bootloader.bin",
        "partitions.bin",
        "firmware.bin",
        "boot_app0.bin",
        "littlefs.bin",
    }


def test_flash_firmware_parts_skip_littlefs():
    names = [name for _, name in flash_mod.FIRMWARE_PARTS]
    assert "firmware.bin" in names
    assert "littlefs.bin" not in names
    assert len(flash_mod.FIRMWARE_PARTS) == len(flash_mod.PARTS) - 1


def test_flash_littlefs_parts_only():
    names = [name for _, name in flash_mod.LITTLEFS_PARTS]
    assert names == ["littlefs.bin"]
    assert flash_mod.LITTLEFS_PARTS[0][0] == 0xC90000


def test_flash_load_dir_and_extract_zip(tmp_path: Path):
    folder = tmp_path / "blenny-cst816"
    folder.mkdir()
    for _, name in flash_mod.PARTS:
        (folder / name).write_bytes(b"x")
    assert flash_mod.load_dir(folder) == folder

    zpath = tmp_path / "blenny-cst816.zip"
    with zipfile.ZipFile(zpath, "w") as zf:
        for p in folder.iterdir():
            zf.write(p, p.name)
    extracted = flash_mod.extract_zip(zpath)
    assert (extracted / "firmware.bin").exists()

    missing = tmp_path / "bad"
    missing.mkdir()
    with pytest.raises(SystemExit):
        flash_mod.load_dir(missing)


def test_flash_find_image_prefers_newer_zip(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    dist = tmp_path / "dist"
    dist.mkdir()
    folder = dist / "blenny-cst816"
    folder.mkdir()
    for _, name in flash_mod.PARTS:
        (folder / name).write_bytes(b"old")
    # Make folder look older
    import os
    import time

    old = time.time() - 1000
    for p in folder.iterdir():
        os.utime(p, (old, old))
    os.utime(folder, (old, old))

    zpath = dist / "blenny-cst816.zip"
    with zipfile.ZipFile(zpath, "w") as zf:
        for p in folder.iterdir():
            zf.write(p, p.name)
    # zip is newer by default

    monkeypatch.setattr(flash_mod, "DIST", dist)
    args = argparse.Namespace(image=None, variant="cst816")
    image = flash_mod.find_image(args)
    assert image != folder
    assert (image / "littlefs.bin").exists()


def test_provision_roster_lookup(tmp_path: Path, monkeypatch: pytest.MonkeyPatch):
    roster = tmp_path / "attendees.json"
    roster.write_text(
        json.dumps(
            {
                "attendees": [
                    {"id": "ada", "name": "Ada", "truth": "Math"},
                    {"id": "grace", "name": "Grace", "truth": "Moth"},
                ]
            }
        ),
        encoding="utf-8",
    )
    monkeypatch.setattr(provision_mod, "ROSTER", roster)
    people = provision_mod.load_roster()
    ids = {a["id"]: a for a in people}
    assert "ada" in ids
    assert ids["grace"]["truth"] == "Moth"

    monkeypatch.setattr(provision_mod, "find_port", lambda explicit: "/dev/fake")
    # Never read the real wifi.local.json / ~/.blenny/roster-key.hex from a test.
    wifi = roster.parent / "wifi.local.json"
    wifi.write_text(json.dumps({"ssid": "TestNet", "password": "pw"}), encoding="utf-8")
    key = roster.parent / "roster-key.hex"
    key.write_text("ab" * 32, encoding="utf-8")
    monkeypatch.setattr(provision_mod, "WIFI", wifi)
    monkeypatch.setattr(provision_mod, "ROSTER_KEY", key)
    calls: list[str] = []

    def fake_talk(port: str, line: str, wait: float = 2.0) -> str:
        calls.append(line)
        return "OK SETID ada\n"

    monkeypatch.setattr(provision_mod, "talk", fake_talk)
    monkeypatch.setattr(
        sys,
        "argv",
        ["provision.py", "--id", "ada", "--port", "/dev/fake"],
    )
    provision_mod.main()
    # SETID, then the site Wi-Fi and the roster key are seeded (provisioning since 1.8).
    assert calls == ["SETID ada", "SETWIFI TestNet\tpw", "SETROSTERKEY " + "ab" * 32]
