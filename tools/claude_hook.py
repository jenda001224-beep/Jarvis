#!/usr/bin/env python3
"""Claude Code hook: tells Jarvis when a long piece of work has finished."""
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = os.path.join(ROOT, ".tools", "hook")
LONG_SECONDS = 120


def main():
    event = json.load(sys.stdin)
    session = "".join(c for c in event.get("session_id", "x") if c.isalnum() or c == "-")
    path = os.path.join(STATE, session)
    os.makedirs(STATE, exist_ok=True)
    if event.get("hook_event_name") == "UserPromptSubmit":
        with open(path, "w") as stored:
            stored.write(str(time.time()))
        return
    if not os.path.exists(path):
        return
    started = float(open(path).read())
    os.remove(path)
    if time.time() - started >= LONG_SECONDS:
        import jarvis_notify

        jarvis_notify.send("Claude dokončil práci.", "mac", True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
    sys.exit(0)
