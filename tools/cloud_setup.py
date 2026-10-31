#!/usr/bin/env python3
"""Connects the board to iCloud: python3 cloud_setup.py [--key-id ID] [--token-file FILE]

Without a key id it creates the key if needed and prints the public part for the CloudKit console.
With a key id it stores the key in the board, which then restarts.
"""
import argparse
import json
import socket
import subprocess
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRIVATE = ROOT / ".tools" / "cloud" / "eckey.pem"
TOKEN = ROOT / ".tools" / "notify-token"


def ensure_key():
    if not PRIVATE.exists():
        PRIVATE.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["openssl", "ecparam", "-name", "prime256v1", "-genkey", "-noout", "-out", str(PRIVATE)], check=True, capture_output=True)
        PRIVATE.chmod(0o600)
    public = subprocess.run(["openssl", "ec", "-in", str(PRIVATE), "-pubout"], check=True, capture_output=True, text=True)
    return public.stdout


def store(key_id, token_file):
    address = socket.getaddrinfo("jarvis.local", 80, socket.AF_INET)[0][4][0]
    body = json.dumps({"cloud_key_id": key_id, "cloud_private_key": PRIVATE.read_text()}).encode()
    request = urllib.request.Request(
        f"http://{address}/api/settings",
        data=body,
        method="PUT",
        headers={"Authorization": f"Bearer {Path(token_file).read_text().strip()}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=10) as reply:
        print("stored, the display restarts:", reply.status)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--key-id")
    parser.add_argument("--token-file", default=str(TOKEN))
    options = parser.parse_args()
    public = ensure_key()
    if options.key_id:
        store(options.key_id, options.token_file)
    else:
        print(public, end="")


if __name__ == "__main__":
    main()
