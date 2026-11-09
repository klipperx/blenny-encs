#!/usr/bin/env python3
"""Build a signed OTA manifest for blenny badges.

Badges fetch <base-url>/manifest.json and <base-url>/manifest.json.sig when Wi-Fi
connects. The .sig is an ECDSA P-256 / SHA-256 signature (DER) over the exact bytes
of manifest.json, checked against the public key in src/ota_pubkey.h.

Usage:
  python3 tools/ota_release.py --version 1.7.0 \\
      --base-url https://example.com/blenny \\
      --build cst816=.pio/build/t-display-s3-touch-cst816/firmware.bin \\
      --build cst328=.pio/build/t-display-s3-touch/firmware.bin \\
      --key ~/.blenny/ota-signing-key.pem --out dist/ota

Add the roster (names + facts) encrypted, so the public host never sees it in clear:
      --roster data/attendees.json --roster-key ~/.blenny/roster-key.hex
  roster.enc = openssl "Salted__" || salt(8) || AES-256-CTR, key+IV from
  PBKDF2-HMAC-SHA256(keyfile text, salt, 10000). Badges hold the key in NVS
  (pushed over USB at provisioning) and install it when its "version" is newer.
  --build is optional for a roster-only release.

Add the event schedule the same way (packed by tools/pack_schedule.py, same key):
      --schedule data/schedule.json

Upload everything in --out to --base-url. The signing key never leaves this machine
(or the CI secret); only the public half is in the firmware.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VARIANTS = ("cst816", "cst328", "scoreboard")


def firmware_version() -> str:
    text = (ROOT / "src" / "config.h").read_text()
    marker = 'kFirmwareVersion = "'
    start = text.index(marker) + len(marker)
    return text[start : text.index('"', start)]


def openssl(*args: str) -> None:
    subprocess.run(["openssl", *args], check=True, capture_output=True)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--version", help="defaults to kFirmwareVersion in src/config.h")
    ap.add_argument("--base-url", required=True, help="public URL the files will be served from")
    ap.add_argument("--build", action="append", default=[], metavar="VARIANT=PATH")
    ap.add_argument("--key", required=True, help="ECDSA P-256 private key (PEM)")
    ap.add_argument("--roster", help="attendees.json to publish encrypted")
    ap.add_argument("--schedule", help="packed schedule.json to publish encrypted")
    ap.add_argument("--roster-key", help="64-hex-char AES-256 key file (with --roster/--schedule)")
    ap.add_argument("--out", default=str(ROOT / "dist" / "ota"))
    args = ap.parse_args()

    version = args.version or firmware_version()
    if args.version and args.version != firmware_version():
        sys.exit(f"--version {args.version} != kFirmwareVersion {firmware_version()}; "
                 "badges compare against the version baked into the image")
    base = args.base_url.rstrip("/")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    builds = {}
    for spec in args.build:
        variant, _, path = spec.partition("=")
        if variant not in VARIANTS or not path:
            sys.exit(f"bad --build {spec!r}; variants: {', '.join(VARIANTS)}")
        src = Path(path).expanduser()
        data = src.read_bytes()
        name = f"blenny-{variant}-{version}.bin"
        shutil.copyfile(src, out / name)
        builds[variant] = {
            "url": f"{base}/{name}",
            "size": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
        }
    def encrypted(kind: str, path: str) -> dict:
        if not args.roster_key:
            sys.exit(f"--{kind} needs --roster-key")
        plain = Path(path).expanduser().read_bytes()
        kversion = json.loads(plain).get("version")
        if not isinstance(kversion, int) or kversion <= 0:
            sys.exit(f"{kind} needs a positive integer \"version\" (rerun its packer)")
        key_file = Path(args.roster_key).expanduser()
        if len(key_file.read_text().strip()) != 64:
            sys.exit("roster key must be 64 hex chars")
        # Key is read from the file by openssl, never placed on the command line.
        blob = subprocess.run(["openssl", "enc", "-aes-256-ctr", "-pbkdf2", "-md", "sha256",
                               "-iter", "10000", "-pass", f"file:{key_file}"], input=plain,
                              check=True, capture_output=True).stdout
        if not blob.startswith(b"Salted__") or len(blob) <= 16:
            sys.exit("unexpected openssl output")
        name = f"{kind}-{kversion}.enc"
        (out / name).write_bytes(blob)
        return {"version": kversion, "url": f"{base}/{name}", "size": len(blob),
                "sha256": hashlib.sha256(blob).hexdigest()}

    roster = encrypted("roster", args.roster) if args.roster else None
    schedule = encrypted("schedule", args.schedule) if args.schedule else None
    if not builds and not roster and not schedule:
        sys.exit("need at least one --build, --roster or --schedule")

    body = {"version": version, "builds": builds}
    if roster:
        body["roster"] = roster
    if schedule:
        body["schedule"] = schedule
    manifest = json.dumps(body, sort_keys=True, separators=(",", ":")).encode()
    mpath = out / "manifest.json"
    spath = out / "manifest.json.sig"
    mpath.write_bytes(manifest)
    key = str(Path(args.key).expanduser())
    openssl("dgst", "-sha256", "-sign", key, "-out", str(spath), str(mpath))

    # Verify with the derived public key before anything gets published.
    with tempfile.TemporaryDirectory() as tmp:
        pub = Path(tmp) / "pub"
        openssl("ec", "-in", key, "-pubout", "-out", str(pub))
        openssl("dgst", "-sha256", "-verify", str(pub), "-signature", str(spath), str(mpath))

    print(f"manifest v{version}: {', '.join(sorted(builds)) or 'no firmware'}")
    if roster:
        print(f"  roster     v{roster['version']}  {roster['size']:>8} bytes  encrypted")
    if schedule:
        print(f"  schedule   v{schedule['version']}  {schedule['size']:>8} bytes  encrypted")
    for v, b in sorted(builds.items()):
        print(f"  {v:10} {b['size']:>8} bytes  {b['sha256'][:16]}…")
    print(f"signed + verified → {out}")


if __name__ == "__main__":
    main()
