#!/usr/bin/env python3
"""Talks to the board over Wi-Fi: no USB and no reset, so a quiet or mute setting stays as it is."""
import argparse
import functools
import http.server
import json
import socket
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOKEN = ROOT / ".tools" / "notify-token"
IMAGE = ROOT / "firmware" / "build" / "jarvis.bin"
PORT = 8765
FAR_AHEAD = 4294967295


def board():
    return socket.getaddrinfo("jarvis.local", 80, socket.AF_INET)[0][4][0]


def call(address, method, path, body=None):
    request = urllib.request.Request(
        f"http://{address}{path}",
        method=method,
        data=None if body is None else json.dumps(body).encode(),
        headers={"Authorization": f"Bearer {TOKEN.read_text().strip()}", "Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=8) as reply:
        return json.loads(reply.read().decode("utf-8", "replace") or "{}")


def log(options):
    address = board()
    planned = sorted((float(item.partition(":")[0]), item.partition(":")[2]) for item in options.send or [])
    started = time.time()
    position = call(address, "POST", "/api/log", {"after": FAR_AHEAD})["next"]
    while time.time() - started < options.seconds:
        while planned and time.time() - started >= planned[0][0]:
            call(address, "POST", "/api/console", {"line": planned.pop(0)[1]})
        try:
            chunk = call(address, "POST", "/api/log", {"after": position})
            position = chunk["next"]
            sys.stdout.write(chunk["text"])
            sys.stdout.flush()
        except (OSError, urllib.error.URLError):
            pass
        time.sleep(0.5)


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def ota(options):
    address = board()
    image = Path(options.image)
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((address, 80))
    own = probe.getsockname()[0]
    probe.close()
    handler = functools.partial(Quiet, directory=str(image.parent))
    server = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    before = call(address, "GET", "/api/update")
    print(f"running {before['current']} from {before['partition']}, sending {image.stat().st_size} bytes")
    call(address, "POST", "/api/update", {"install": True, "url": f"http://{own}:{PORT}/{image.name}", "size": image.stat().st_size})
    started = time.time()
    seen = -1
    while time.time() - started < options.seconds:
        try:
            status = call(address, "GET", "/api/update")
        except (OSError, urllib.error.URLError):
            time.sleep(1)
            continue
        if status["phase"] == "downloading" and status["percent"] // 20 != seen:
            seen = status["percent"] // 20
            print(f"{status['percent']} %", flush=True)
        if status["phase"] == "failed":
            sys.exit(f"failed: {status['problem']}")
        if status["partition"] != before["partition"] and status["phase"] == "idle":
            print(f"now {status['current']} from {status['partition']} after {time.time() - started:.0f} s")
            return
        time.sleep(1)
    sys.exit("no new version came up in time")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    logging = commands.add_parser("log")
    logging.add_argument("--seconds", type=float, default=30)
    logging.add_argument("--send", action="append")
    logging.set_defaults(run=log)
    updating = commands.add_parser("ota")
    updating.add_argument("--image", default=str(IMAGE))
    updating.add_argument("--seconds", type=float, default=180)
    updating.set_defaults(run=ota)
    options = parser.parse_args()
    options.run(options)


if __name__ == "__main__":
    main()
