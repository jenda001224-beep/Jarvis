#!/usr/bin/env python3
import argparse
import asyncio
import json
import os
import re
import subprocess
import sys
import wave
from pathlib import Path

import websockets

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
MAIN = HERE.parent / "main" / "main.c"
WORK = Path(os.environ.get("TMPDIR", "/tmp")) / "jarvis-live"
CJSON = Path(os.environ.get("IDF_PATH", "/Volumes/Jan/Z-Macu/Library/Caches/esphome/idf/frameworks/5.5.5")) / "components/json/cJSON"
ENDPOINT = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
CHUNK_SECONDS = 1024 / 16000
AUDIO_PREFIX = '{"realtimeInput":{"audio"'
DEVICES = "Zařízení (id; název; místnost; typ):\nlamp_1; Lampa; Obývák; světlo\nplug_1; Zásuvka u stolu; Pokoj; zásuvka\n"


def c_string(name):
    source = MAIN.read_text(encoding="utf-8")
    match = re.search(rf"static const char {name}\[\] =(.*?);\n", source, re.DOTALL)
    if not match:
        sys.exit(f"{name} not found in main.c")
    return "".join(json.loads(f'"{part}"') for part in re.findall(r'"((?:[^"\\]|\\.)*)"', match.group(1)))


def build_helper():
    WORK.mkdir(parents=True, exist_ok=True)
    helper = WORK / "live_host"
    subprocess.run(
        ["cc", "-std=c11", "-w", "-O1", f"-I{HERE.parent / 'main'}", f"-I{CJSON}", "-o", str(helper),
         str(HERE / "live_host.c"), str(HERE.parent / "main" / "proto.c"), str(HERE.parent / "main" / "resample.c"),
         str(CJSON / "cJSON.c"), "-lm"],
        check=True,
    )
    return helper


def speech(index, text, voice):
    aiff = WORK / f"say_{index}.aiff"
    wav = WORK / f"say_{index}.wav"
    pcm = WORK / f"say_{index}.pcm"
    subprocess.run(["say", "-v", voice, "-o", str(aiff), text], check=True)
    subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1", str(aiff), str(wav)], check=True)
    with wave.open(str(wav), "rb") as source:
        frames = source.readframes(source.getnframes())
    pcm.write_bytes(bytes(8000) + frames + bytes(16000 * 2 * 3))
    return pcm


async def converse(endpoint, helper, key, model, voice, instruction, functions, search, utterances, label):
    reply = WORK / f"reply_{label}.pcm"
    arguments = [str(helper), model, voice, instruction, functions or "", str(reply)] + ([] if search else ["nosearch"])
    process = await asyncio.create_subprocess_exec(*arguments, stdin=asyncio.subprocess.PIPE, stdout=asyncio.subprocess.PIPE, limit=8 * 1024 * 1024)
    result = {"label": label, "ready": False, "frames": set(), "turns": [], "close": ""}
    turn = {"heard": "", "spoken": "", "tools": [], "audio": 0}
    pending = asyncio.Queue()
    state = {"speaking": False, "done": asyncio.Event(), "ready": asyncio.Event()}

    def tell(line):
        process.stdin.write((line + "\n").encode())

    try:
        async with websockets.connect(f"{endpoint}?key={key}", max_size=None, open_timeout=20) as socket:

            async def from_helper():
                while True:
                    raw = await process.stdout.readline()
                    if not raw:
                        return
                    line = raw.decode().rstrip("\n")
                    if line.startswith("SEND "):
                        message = line[5:]
                        if message.startswith(AUDIO_PREFIX):
                            await pending.put(message)
                        else:
                            await socket.send(message)
                    elif line == "EVENT ready":
                        result["ready"] = True
                        state["ready"].set()
                    elif line.startswith("EVENT audio "):
                        turn["audio"] += int(line.split()[3])
                        if not state["speaking"]:
                            state["speaking"] = True
                            while not pending.empty():
                                pending.get_nowait()
                            tell("END")
                    elif line.startswith("EVENT heard "):
                        turn["heard"] += line[12:]
                    elif line.startswith("EVENT spoken "):
                        turn["spoken"] += line[13:]
                    elif line.startswith("EVENT tool "):
                        turn["tools"].append(line[11:])
                    elif line == "EVENT turn":
                        if turn["audio"] or turn["spoken"]:
                            state["done"].set()

            async def from_server():
                async for message in socket:
                    result["frames"].add("binary" if isinstance(message, bytes) else "text")
                    data = message if isinstance(message, bytes) else message.encode()
                    process.stdin.write(b"RECV " + data.replace(b"\n", b" ").replace(b"\r", b" ") + b"\n")

            async def pace():
                while True:
                    message = await pending.get()
                    if not state["speaking"]:
                        await socket.send(message)
                        await asyncio.sleep(CHUNK_SECONDS)

            tasks = [asyncio.create_task(job()) for job in (from_helper, from_server, pace)]
            try:
                await asyncio.wait_for(state["ready"].wait(), 20)
                for index, text in enumerate(utterances):
                    turn.update({"heard": "", "spoken": "", "tools": [], "audio": 0})
                    state["speaking"] = False
                    state["done"].clear()
                    tell(f"AUDIO {speech(f'{label}_{index}', text, 'Samantha')}")
                    try:
                        await asyncio.wait_for(state["done"].wait(), 45)
                    except asyncio.TimeoutError:
                        turn["timeout"] = True
                    await asyncio.sleep(0.5)
                    result["turns"].append({"said": text, **turn, "tools": list(turn["tools"])})
            except asyncio.TimeoutError:
                result["close"] = result["close"] or "setup was not acknowledged within 20 s"
            finally:
                for task in tasks:
                    task.cancel()
    except websockets.ConnectionClosed as error:
        result["close"] = f"{error.rcvd.code} {error.rcvd.reason}" if error.rcvd else str(error)
    except Exception as error:
        result["close"] = f"{type(error).__name__}: {error}"
    finally:
        try:
            tell("QUIT")
            await process.stdin.drain()
        except Exception:
            pass
        await asyncio.wait_for(process.wait(), 5)
    if reply.exists() and reply.stat().st_size:
        with wave.open(str(reply.with_suffix(".wav")), "wb") as target:
            target.setnchannels(1)
            target.setsampwidth(2)
            target.setframerate(16000)
            target.writeframes(reply.read_bytes())
    return result


def report(result):
    print(f"\n== {result['label']}")
    print(f"   setup accepted: {result['ready']} | frames from server: {sorted(result['frames']) or '-'}")
    if result["close"]:
        print(f"   connection ended: {result['close']}")
    passed = result["ready"]
    for turn in result["turns"]:
        seconds = turn["audio"] / 16000
        print(f"   said   : {turn['said']}")
        print(f"   heard  : {turn['heard'].strip()}")
        print(f"   tools  : {turn['tools'] or '-'}")
        print(f"   spoken : {turn['spoken'].strip()}")
        print(f"   audio  : {seconds:.1f} s" + ("  (timed out)" if turn.get("timeout") else ""))
        passed = passed and seconds > 0.3 and not turn.get("timeout")
    return passed


async def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--settings", default=str(ROOT / "nastaveni.json"))
    parser.add_argument("--model", default="gemini-3.8-live")
    parser.add_argument("--voice", default="Charon")
    parser.add_argument("--endpoint", default=ENDPOINT)
    options = parser.parse_args()
    key = os.environ.get("GEMINI_API_KEY", "")
    if not key and Path(options.settings).exists():
        key = json.loads(Path(options.settings).read_text(encoding="utf-8")).get("key", "")
    if not key:
        sys.exit(f"no API key: set GEMINI_API_KEY or put {{\"key\": ...}} into {options.settings}")

    helper = build_helper()
    persona = c_string("PERSONA").replace("%s", "Praha")
    home = c_string("HOME")
    functions = c_string("FUNCTIONS")
    plain = persona
    with_home = f"{persona}\n\n{home}\n\n{DEVICES}"

    results = [
        await converse(options.endpoint, helper, key, options.model, options.voice, plain, None, False,
                       ["What is two plus two?"], "plain"),
        await converse(options.endpoint, helper, key, options.model, options.voice, with_home, functions, True,
                       ["Turn off the lamp in the living room.", "Is the lamp on right now?"], "home_search"),
    ]
    if not results[1]["ready"]:
        results.append(await converse(options.endpoint, helper, key, options.model, options.voice, with_home, functions, False,
                                      ["Turn off the lamp in the living room."], "home_only"))
    passed = all([report(result) for result in results][:2])
    tools_seen = any(turn["tools"] for result in results for turn in result["turns"])
    print(f"\nreply audio saved in {WORK}")
    print("RESULT:", "PASS" if passed and tools_seen else "FAIL")
    sys.exit(0 if passed and tools_seen else 1)


if __name__ == "__main__":
    asyncio.run(main())
