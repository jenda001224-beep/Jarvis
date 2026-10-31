#!/usr/bin/env python3
"""Sends a message to the J-WALL display: python3 jarvis_notify.py "Text" [--silent] [--source name]"""
import argparse
import json
import os
import socket
import sys
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN_FILE = os.path.join(ROOT, ".tools", "notify-token")
ADDRESS_FILE = os.path.join(ROOT, ".tools", "notify-address")
FALLBACK_ADDRESS = "10.0.0.29"
TIMEOUT_SECONDS = 3


def addresses():
    socket.setdefaulttimeout(TIMEOUT_SECONDS)
    found = []
    try:
        found.append(socket.getaddrinfo("jarvis.local", 80, socket.AF_INET)[0][4][0])
    except OSError:
        pass
    if os.path.exists(ADDRESS_FILE):
        found.append(open(ADDRESS_FILE).read().strip())
    found.append(FALLBACK_ADDRESS)
    return found


def post(path, fields):
    token = open(TOKEN_FILE).read().strip()
    body = json.dumps(fields).encode()
    for address in addresses():
        request = urllib.request.Request(
            f"http://{address}{path}",
            data=body,
            method="POST",
            headers={"Authorization": f"Bearer {token}", "Content-Type": "application/json"},
        )
        try:
            with urllib.request.urlopen(request, timeout=TIMEOUT_SECONDS) as reply:
                if reply.status == 200:
                    with open(ADDRESS_FILE, "w") as stored:
                        stored.write(address)
                    return json.loads(reply.read() or b"{}")
        except OSError:
            continue
    return None


def send(text, source, speak):
    return post("/api/notify", {"text": text, "source": source, "speak": speak}) is not None


def alert(text, title, seconds, speak=True):
    reply = post("/api/alert", {"text": text, "title": title, "seconds": seconds, "source": "claude", "speak": speak})
    return reply.get("id") if reply else None


def clear_alert(notice):
    post("/api/alert/clear", {"id": notice})


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("text")
    parser.add_argument("--source", default="mac")
    parser.add_argument("--silent", action="store_true")
    options = parser.parse_args()
    sys.exit(0 if send(options.text, options.source, not options.silent) else 1)


if __name__ == "__main__":
    main()
