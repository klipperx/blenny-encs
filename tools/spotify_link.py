#!/usr/bin/env python3
"""One-shot Spotify Authorization Code + PKCE for the Blenny badge.

Complies with Spotify redirect rules: http://127.0.0.1:<port>/callback
(never SoftAP / LAN IPs / localhost hostname).

Usage:
  python3 tools/spotify_link.py --client-id YOUR_CLIENT_ID
  python3 tools/spotify_link.py --client-id YOUR_CLIENT_ID --port 8888

Dashboard setup:
  Redirect URI (exact): http://127.0.0.1:8888/callback

Then on the badge serial:
  SETSPOTIFY_CLIENT <client_id>
  SETSPOTIFY_REFRESH <refresh_token>
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import http.server
import secrets
import ssl
import string
import sys
import threading
import urllib.parse
import urllib.request
import webbrowser


def ssl_context() -> ssl.SSLContext:
    """Prefer certifi on macOS Homebrew/pyenv Pythons that lack system CAs."""
    try:
        import certifi

        return ssl.create_default_context(cafile=certifi.where())
    except Exception:
        return ssl.create_default_context()

SCOPES = " ".join(
    [
        "user-read-currently-playing",
        "user-read-playback-state",
        "user-modify-playback-state",
    ]
)
TOKEN_URL = "https://accounts.spotify.com/api/token"
AUTH_URL = "https://accounts.spotify.com/authorize"


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).decode("ascii").rstrip("=")


def make_verifier(n: int = 64) -> str:
    alphabet = string.ascii_letters + string.digits + "-._~"
    return "".join(secrets.choice(alphabet) for _ in range(n))


def make_challenge(verifier: str) -> str:
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return b64url(digest)


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    code: str | None = None
    error: str | None = None
    event: threading.Event | None = None
    expected_path = "/callback"

    def log_message(self, fmt: str, *args) -> None:  # noqa: A003
        return

    def do_GET(self) -> None:  # noqa: N802
        parsed = urllib.parse.urlparse(self.path)
        if parsed.path != self.expected_path:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"not found")
            return
        qs = urllib.parse.parse_qs(parsed.query)
        if "error" in qs:
            CallbackHandler.error = qs["error"][0]
            body = f"<h2>Spotify error: {CallbackHandler.error}</h2>".encode()
            self.send_response(400)
        else:
            CallbackHandler.code = qs.get("code", [None])[0]
            body = (
                b"<html><body style='font-family:sans-serif'>"
                b"<h2>Linked</h2><p>Return to the terminal - you can close this tab.</p>"
                b"</body></html>"
            )
            self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        if CallbackHandler.event:
            CallbackHandler.event.set()


def exchange(client_id: str, code: str, verifier: str, redirect_uri: str) -> dict:
    data = urllib.parse.urlencode(
        {
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": redirect_uri,
            "client_id": client_id,
            "code_verifier": verifier,
        }
    ).encode()
    req = urllib.request.Request(
        TOKEN_URL,
        data=data,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=30, context=ssl_context()) as resp:
        import json

        return json.loads(resp.read().decode())


def main() -> int:
    ap = argparse.ArgumentParser(description="Blenny Spotify PKCE link helper")
    ap.add_argument("--client-id", required=True, help="Spotify app client id")
    ap.add_argument("--port", type=int, default=8888, help="loopback port (default 8888)")
    args = ap.parse_args()

    redirect_uri = f"http://127.0.0.1:{args.port}/callback"
    verifier = make_verifier()
    challenge = make_challenge(verifier)

    params = {
        "client_id": args.client_id,
        "response_type": "code",
        "redirect_uri": redirect_uri,
        "scope": SCOPES,
        "code_challenge_method": "S256",
        "code_challenge": challenge,
    }
    auth = AUTH_URL + "?" + urllib.parse.urlencode(params)

    event = threading.Event()
    CallbackHandler.event = event
    CallbackHandler.code = None
    CallbackHandler.error = None

    server = http.server.HTTPServer(("127.0.0.1", args.port), CallbackHandler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()

    print("Redirect URI (add exactly in Spotify Dashboard):")
    print(f"  {redirect_uri}")
    print()
    print("Opening browser for Spotify login…")
    print(auth)
    print()
    webbrowser.open(auth)

    if not event.wait(timeout=300):
        print("Timed out waiting for callback.", file=sys.stderr)
        server.shutdown()
        return 1

    server.shutdown()

    if CallbackHandler.error:
        print(f"Authorization failed: {CallbackHandler.error}", file=sys.stderr)
        return 1
    if not CallbackHandler.code:
        print("No authorization code received.", file=sys.stderr)
        return 1

    try:
        token = exchange(args.client_id, CallbackHandler.code, verifier, redirect_uri)
    except Exception as exc:  # noqa: BLE001
        print(f"Token exchange failed: {exc}", file=sys.stderr)
        return 1

    refresh = token.get("refresh_token")
    if not refresh:
        print("No refresh_token in response — check app settings / scopes.", file=sys.stderr)
        print(token, file=sys.stderr)
        return 1

    print()
    print("Success. On the badge serial console, paste:")
    print()
    print(f"SETSPOTIFY_CLIENT {args.client_id}")
    print(f"SETSPOTIFY_REFRESH {refresh}")
    print()
    print("Then: Menu → Post-Con Use → Spotify Remote (home WiFi required).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
