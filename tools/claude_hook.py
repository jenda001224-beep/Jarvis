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
TAIL_BYTES = 2_000_000


def chat_title(event):
    path = event.get("transcript_path", "")
    if path and os.path.exists(path):
        with open(path, "rb") as transcript:
            transcript.seek(max(0, os.path.getsize(path) - TAIL_BYTES))
            tail = transcript.read()
        for line in reversed(tail.split(b"\n")):
            if b'"customTitle"' in line:
                try:
                    title = json.loads(line).get("customTitle")
                except ValueError:
                    continue
                if title:
                    return title
    return os.path.basename(event.get("cwd", "").rstrip("/"))


def finished_text(event):
    title = chat_title(event)
    return f"Claude dokončil práci v chatu {title}." if title else "Claude dokončil práci."


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

        jarvis_notify.send(finished_text(event), "mac", True)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
    sys.exit(0)
