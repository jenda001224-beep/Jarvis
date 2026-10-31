#!/usr/bin/env python3
"""A stand-in for CloudKit Web Services that checks every request the way Apple does."""
import base64
import hashlib
import json
import os
import sys
import threading
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

PORT = 5233
SUBPATH = "/database/1/iCloud.com.jenda.jarvis/development/public/records/"
RECORDS = {}
NOTICES = []
PROBLEMS = []
REQUESTS = {"lookup": 0, "modify": 0}


def key_for(token):
    return hashlib.sha256(("jarvis-cloud" + token).encode()).digest()


def phone_for(token):
    return hashlib.sha256(token.encode()).hexdigest()[:16]


def seal(token, text):
    nonce = os.urandom(12)
    return base64.b64encode(nonce + AESGCM(key_for(token)).encrypt(nonce, text.encode(), None)).decode()


def unseal(token, data):
    raw = base64.b64decode(data)
    return AESGCM(key_for(token)).decrypt(raw[:12], raw[12:], None).decode()


class Handler(BaseHTTPRequestHandler):
    public_key = None

    def log_message(self, *args):
        pass

    def fail(self, why):
        PROBLEMS.append(why)
        self.send_response(401)
        self.end_headers()
        self.wfile.write(json.dumps({"serverErrorCode": "AUTHENTICATION_FAILED", "reason": why}).encode())

    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        operation = self.path.rsplit("/", 1)[-1]
        if not self.path.startswith(SUBPATH):
            return self.fail("bad path " + self.path)
        date = self.headers["X-Apple-CloudKit-Request-ISO8601Date"]
        signature = base64.b64decode(self.headers["X-Apple-CloudKit-Request-SignatureV1"])
        moment = datetime.strptime(date, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=timezone.utc)
        if abs((datetime.now(timezone.utc) - moment).total_seconds()) > 300:
            return self.fail("date too far off: " + date)
        message = f"{date}:{base64.b64encode(hashlib.sha256(body).digest()).decode()}:{self.path}"
        try:
            Handler.public_key.verify(signature, message.encode(), ec.ECDSA(hashes.SHA256()))
        except InvalidSignature:
            return self.fail("signature does not verify")
        if self.headers["X-Apple-CloudKit-Request-KeyID"] != "test-key":
            return self.fail("wrong key id")
        request = json.loads(body)
        REQUESTS[operation] += 1
        answer = {"records": []}
        if operation == "lookup":
            for record in request["records"]:
                name = record["recordName"]
                if name in RECORDS:
                    answer["records"].append(RECORDS[name])
                else:
                    answer["records"].append({"recordName": name, "serverErrorCode": "NOT_FOUND", "reason": "not found"})
        else:
            for item in request["operations"]:
                record = item["record"]
                name = record.get("recordName") or f"auto-{len(NOTICES)}"
                kind = item["operationType"]
                if kind == "forceReplace" and name not in RECORDS:
                    answer["records"].append({"recordName": name, "serverErrorCode": "NOT_FOUND"})
                    continue
                if kind == "create" and name in RECORDS:
                    answer["records"].append({"recordName": name, "serverErrorCode": "CONFLICT"})
                    continue
                stored = {"recordName": name, "recordType": record["recordType"], "fields": record["fields"]}
                RECORDS[name] = stored
                if record["recordType"] == "JarvisNotice":
                    NOTICES.append(record["fields"]["text"]["value"])
                answer["records"].append(stored)
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        data = json.dumps(answer).encode()
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def command(token, seq, method, path, body=None):
    fields = {"method": method, "path": path}
    if body is not None:
        fields["body"] = json.dumps(body, ensure_ascii=False)
    RECORDS["cmd-" + phone_for(token)] = {
        "recordName": "cmd-" + phone_for(token),
        "recordType": "JarvisMail",
        "fields": {"seq": {"value": seq}, "data": {"value": seal(token, json.dumps(fields, ensure_ascii=False))}},
    }


def wait_reply(token, seq, seconds=45):
    name = "rep-" + phone_for(token)
    end = time.time() + seconds
    while time.time() < end:
        record = RECORDS.get(name)
        if record and record["fields"]["seq"]["value"] == seq:
            return json.loads(unseal(token, record["fields"]["data"]["value"]))
        time.sleep(0.5)
    return None


def main():
    key_file, token_file = sys.argv[1], sys.argv[2]
    private = serialization.load_pem_private_key(open(key_file, "rb").read(), None)
    Handler.public_key = private.public_key()
    token = open(token_file).read().strip()
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    print("mock ready, phone", phone_for(token), flush=True)
    while REQUESTS["lookup"] < 2:
        time.sleep(0.5)
    print("board polls, problems so far:", PROBLEMS, flush=True)
    seq = 100
    steps = [
        ("GET", "/api/state", None),
        ("POST", "/api/notify", {"text": "Zkouška přes iCloud", "speak": False}),
        ("POST", "/api/presence", {"home": False}),
        ("POST", "/api/notify", {"text": "Zpráva pro telefon, pán je pryč"}),
        ("POST", "/api/presence", {"home": True}),
        ("POST", "/api/chat", {"text": "Kolik je dvě a dvě? Odpověz jedním číslem."}),
    ]
    for method, path, body in steps:
        seq += 1
        command(token, seq, method, path, body)
        reply = wait_reply(token, seq)
        print(method, path, "->", (reply or {}).get("status"), (reply or {}).get("reply", "")[:90], flush=True)
    end = time.time() + 40
    answer = None
    while time.time() < end and not answer:
        seq += 1
        command(token, seq, "GET", "/api/chat")
        reply = wait_reply(token, seq, 12)
        if reply:
            chat = json.loads(reply["reply"])
            if not chat["busy"] and len(chat["messages"]) >= 2:
                answer = chat["messages"][-1]["text"]
    print("chat answer:", answer, flush=True)
    time.sleep(8)
    snapshot = RECORDS.get("snap-" + phone_for(token))
    if snapshot:
        data = json.loads(unseal(token, snapshot["fields"]["data"]["value"]))
        print("snapshot keys:", sorted(data.keys()), "size", len(snapshot["fields"]["data"]["value"]), flush=True)
        print("snapshot state:", data["state"]["state"], "chat messages:", len(data["chat"]["messages"]), flush=True)
    else:
        print("no snapshot", flush=True)
    print("pushed notices:", NOTICES, flush=True)
    print("requests:", REQUESTS, "problems:", PROBLEMS, flush=True)


if __name__ == "__main__":
    main()
