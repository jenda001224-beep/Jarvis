#!/usr/bin/env python3
"""Claude Code hook: tells Jarvis when a long piece of work has finished or when Claude waits for a permission."""
import json
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STATE = os.path.join(ROOT, ".tools", "hook")
PENDING = os.path.join(STATE, "pending")
LONG_SECONDS = 120
TAIL_BYTES = 2_000_000
SAME_PROMPT_SECONDS = 15
ACTION_LIMIT = 110
TITLE_LIMIT = 60
ALERT_TITLE = "CLAUDE ČEKÁ NA POVOLENÍ"
ALERT_SECONDS = 600


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


def clip(text, limit):
    text = " ".join(str(text).split())
    return text if len(text) <= limit else text[: limit - 1].rstrip() + "…"


def describe_tool(event):
    tool = event.get("tool_name", "")
    given = event.get("tool_input") or {}
    if not tool:
        found = re.search(r"to use (.+)$", event.get("message", ""))
        return "použít nástroj " + found.group(1) if found else ""
    if tool == "Bash":
        return "spustit příkaz: " + (given.get("description") or given.get("command") or "")
    if tool in ("Edit", "Write", "NotebookEdit", "MultiEdit"):
        return "upravit soubor " + os.path.basename(given.get("file_path") or given.get("notebook_path") or "")
    if tool == "Read":
        return "číst soubor " + os.path.basename(given.get("file_path", ""))
    if tool == "WebFetch":
        return "otevřít web " + given.get("url", "").split("//")[-1].split("/")[0]
    if tool == "WebSearch":
        return "hledat na webu: " + given.get("query", "")
    if tool.startswith("mcp__"):
        return "použít nástroj " + tool.split("__")[-1].replace("_", " ")
    return "použít nástroj " + tool


def permission_text(event):
    action = describe_tool(event)
    text = f"Claude čeká na povolení, chce {clip(action, ACTION_LIMIT).rstrip('.')}." if action else "Claude čeká na povolení."
    if event.get("tool_name") == "AskUserQuestion":
        text = "Claude se na něco ptá."
    title = chat_title(event)
    return f"{text} Chat {clip(title, TITLE_LIMIT)}." if title else text


def load_pending():
    try:
        with open(PENDING) as stored:
            return json.load(stored)
    except (OSError, ValueError):
        return {}


def save_pending(pending):
    if pending:
        with open(PENDING, "w") as stored:
            json.dump(pending, stored)
    elif os.path.exists(PENDING):
        os.remove(PENDING)


def raise_alert(event, session):
    pending = load_pending()
    known = pending.get(session)
    if known and event.get("hook_event_name") == "Notification" and time.time() - known["at"] < SAME_PROMPT_SECONDS:
        return
    import jarvis_notify

    notice = jarvis_notify.alert(permission_text(event), ALERT_TITLE, ALERT_SECONDS)
    if notice:
        pending[session] = {"id": notice, "at": time.time()}
        save_pending(pending)


def clear_alert(session):
    pending = load_pending()
    known = pending.pop(session, None)
    if known:
        save_pending(pending)
        import jarvis_notify

        jarvis_notify.clear_alert(known["id"])


def main():
    event = json.load(sys.stdin)
    name = event.get("hook_event_name")
    session = "".join(c for c in event.get("session_id", "x") if c.isalnum() or c == "-")
    os.makedirs(STATE, exist_ok=True)
    if name == "PermissionRequest" or (name == "Notification" and event.get("notification_type") == "permission_prompt"):
        raise_alert(event, session)
        return
    clear_alert(session)
    path = os.path.join(STATE, session)
    if name == "UserPromptSubmit":
        with open(path, "w") as stored:
            stored.write(str(time.time()))
    elif name == "Stop" and os.path.exists(path):
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
