#!/usr/bin/env python3
import json
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / ".tools" / "tmp" / "api-check.log"


def call(base, method, path, body=None, token=None):
    request = urllib.request.Request(base + path, method=method,
                                     data=json.dumps(body).encode() if body is not None else None,
                                     headers={"Content-Type": "application/json"})
    if token:
        request.add_header("Authorization", f"Bearer {token}")
    try:
        with urllib.request.urlopen(request, timeout=8) as response:
            return response.status, json.loads(response.read() or b"{}")
    except urllib.error.HTTPError as error:
        return error.code, json.loads(error.read() or b"{}")


def expect(condition, message):
    print(("ok   " if condition else "FAIL ") + message)
    if not condition:
        sys.exit(1)


def main():
    address = sys.argv[1] if len(sys.argv) > 1 else "jarvis.local"
    base = f"http://{address}"
    monitor = subprocess.Popen([sys.executable, str(ROOT / "tools" / "bench.py"), "log", "--seconds", "90"],
                               stdout=open(LOG, "w"), stderr=subprocess.STDOUT)
    try:
        time.sleep(25)
        status, hello = call(base, "GET", "/api/hello")
        expect(status == 200 and hello["name"] == "J-WALL Display", f"hello {hello}")
        status, _ = call(base, "GET", "/api/state")
        expect(status == 401, "state needs pairing")
        status, _ = call(base, "POST", "/api/pair/start", {})
        expect(status == 200, "pairing started")
        time.sleep(1)
        codes = re.findall(r"pairing code (\d{6})", LOG.read_text(errors="replace"))
        expect(bool(codes), "code found in the board log")
        status, _ = call(base, "POST", "/api/pair", {"code": "000000" if codes[-1] != "000000" else "111111"})
        expect(status == 401, "wrong code refused")
        status, paired = call(base, "POST", "/api/pair", {"code": codes[-1]})
        expect(status == 200 and len(paired.get("token", "")) == 32, "paired")
        token = paired["token"]
        status, state = call(base, "GET", "/api/state", token=token)
        expect(status == 200 and "prefs" in state, f"state {state['version']} {state['state']}")
        status, todos = call(base, "POST", "/api/todos", {"text": "Zkušební úkol z testu"}, token)
        item = next(t for t in todos["todos"] if t["text"] == "Zkušební úkol z testu")
        expect(status == 200, "to-do added")
        status, todos = call(base, "PATCH", f"/api/todos/{item['id']}", {"done": True}, token)
        expect(status == 200 and any(t["id"] == item["id"] and t["done"] for t in todos["todos"]), "to-do done")
        status, todos = call(base, "DELETE", f"/api/todos/{item['id']}", None, token)
        expect(status == 200 and all(t["id"] != item["id"] for t in todos["todos"]), "to-do deleted")
        status, alarms = call(base, "POST", "/api/alarms", {"hour": 6, "minute": 45, "days": 31, "label": "Test"}, token)
        alarm = next(a for a in alarms["alarms"] if a["label"] == "Test")
        expect(status == 200, "alarm added")
        status, alarms = call(base, "PATCH", f"/api/alarms/{alarm['id']}", {"enabled": False}, token)
        expect(status == 200 and not next(a for a in alarms["alarms"] if a["id"] == alarm["id"])["enabled"], "alarm disabled")
        status, _ = call(base, "DELETE", f"/api/alarms/{alarm['id']}", None, token)
        expect(status == 200, "alarm deleted")
        prefs = state["prefs"]
        status, reply = call(base, "PUT", "/api/prefs", {"night_from": prefs["night_from"]}, token)
        expect(status == 200 and reply["prefs"]["night_from"] == prefs["night_from"], "prefs accepted")
        status, _ = call(base, "PUT", "/api/prefs", {"volume": 400}, token)
        expect(status == 400, "bad prefs refused")
        status, _ = call(base, "GET", "/api/reminders", token=token)
        expect(status == 200, "reminders listed")
        status, timers = call(base, "POST", "/api/timers", {"seconds": 600, "label": "Test"}, token)
        timer = next(t for t in timers["timers"] if t["label"] == "Test")
        expect(status == 200 and timer["left"] in ("10:00", "9:59"), "timer started")
        status, timers = call(base, "DELETE", f"/api/timers/{timer['id']}", None, token)
        expect(status == 200 and all(t["id"] != timer["id"] for t in timers["timers"]), "timer cancelled")
        status, _ = call(base, "POST", "/api/timers", {"seconds": 0}, token)
        expect(status == 400, "bad timer refused")
        status, memory = call(base, "GET", "/api/memory", token=token)
        expect(status == 200 and memory["available"], f"memory has {memory.get('lines')} lines")
        status, found = call(base, "POST", "/api/memory/search", {"query": "zzzzzzzz"}, token)
        expect(status == 200 and found["lines"] == [], "memory search finds nothing silly")
        status, sent = call(base, "POST", "/api/notify", {"text": "Zkouška ze skriptu", "speak": False}, token)
        expect(status == 200 and sent["id"] > 0, "notice accepted")
        status, notices = call(base, "DELETE", "/api/notices", None, token)
        expect(status == 200 and notices["unread"] == 0 and notices["notices"][0]["text"] == "Zkouška ze skriptu", "notice read")
        status, _ = call(base, "POST", "/api/notify", {}, token)
        expect(status == 400, "empty notice refused")
        status, media = call(base, "GET", "/api/media", token=token)
        expect(status == 200 and len(media["stations"]) >= 10, "stations listed")
        status, chat = call(base, "GET", "/api/chat", token=token)
        expect(status == 200 and "messages" in chat, "chat readable")
        status, calendars = call(base, "GET", "/api/calendars", token=token)
        expect(status == 200 and "configured" in calendars, "calendar status readable")
        status, reply = call(base, "POST", "/api/presence", {"home": True}, token)
        expect(status == 200 and reply["away"] is False, "presence accepted")
        status, snapshot = call(base, "GET", "/api/snapshot", token=token)
        expect(status == 200 and "state" in snapshot and "mark" in snapshot and "todos" in snapshot, "snapshot complete")
        status, _ = call(base, "POST", "/api/forget", {}, token)
        status, _ = call(base, "GET", "/api/state", token=token)
        expect(status == 401, "forgotten token refused")
    finally:
        monitor.terminate()


if __name__ == "__main__":
    main()
