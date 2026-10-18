import asyncio
import json
import sys

import websockets
from google import genai
from google.genai import types

FIXTURE = __file__.rsplit("/", 1)[0] + "/server_messages.jsonl"
captured = []
request_paths = []


async def handler(socket):
    request_paths.append(socket.request.path)
    first = await socket.recv()
    captured.append(json.loads(first))
    await socket.send(json.dumps({"setupComplete": {}}).encode())
    try:
        async for message in socket:
            captured.append(json.loads(message))
            if "tool_response" in captured[-1] or "toolResponse" in captured[-1]:
                for line in open(FIXTURE, encoding="utf-8"):
                    await socket.send(line.strip().encode())
    except websockets.ConnectionClosed:
        pass


def camel(key):
    head, *rest = key.split("_")
    return head + "".join(part.capitalize() for part in rest)


def keys(value, prefix=""):
    found = set()
    if isinstance(value, dict):
        for key, item in value.items():
            key = camel(key)
            found.add(prefix + key)
            found |= keys(item, prefix + key + ".")
    elif isinstance(value, list):
        for item in value:
            found |= keys(item, prefix + "[].")
    return found


async def main():
    server = await websockets.serve(handler, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    client = genai.Client(api_key="TESTKEY")
    from google.genai import live

    real_connect = live.ws_connect
    dialled = []

    def local_connect(uri, **options):
        dialled.append((uri, sorted((options.get("additional_headers") or {}).keys())))
        return real_connect(f"ws://127.0.0.1:{port}/" + uri.split("/", 3)[3], additional_headers=options.get("additional_headers"))

    live.ws_connect = local_connect
    config = types.LiveConnectConfig(
        response_modalities=["AUDIO"],
        speech_config=types.SpeechConfig(
            voice_config=types.VoiceConfig(prebuilt_voice_config=types.PrebuiltVoiceConfig(voice_name="Charon"))
        ),
        system_instruction=types.Content(parts=[types.Part(text="Jsi Jarvis.")]),
        tools=[
            types.Tool(google_search=types.GoogleSearch()),
            types.Tool(
                function_declarations=[
                    types.FunctionDeclaration(
                        name="set_devices",
                        description="x",
                        parameters=types.Schema(type="OBJECT", properties={"on": types.Schema(type="BOOLEAN")}),
                    )
                ]
            ),
        ],
        input_audio_transcription=types.AudioTranscriptionConfig(language_codes=["cs-CZ", "en-US"]),
        output_audio_transcription=types.AudioTranscriptionConfig(),
        realtime_input_config=types.RealtimeInputConfig(
            automatic_activity_detection=types.AutomaticActivityDetection(
                start_of_speech_sensitivity=types.StartSensitivity.START_SENSITIVITY_LOW,
                prefix_padding_ms=200,
                silence_duration_ms=600,
            )
        ),
    )
    async with client.aio.live.connect(model="gemini-3.8-live", config=config) as session:
        await session.send_realtime_input(audio=types.Blob(data=b"\x01\x02\x03\x04", mime_type="audio/pcm;rate=16000"))
        await session.send_realtime_input(audio_stream_end=True)
        await session.send_client_content(
            turns=[{"role": "user", "parts": [{"text": "Nastal čas připomínky."}]}], turn_complete=True
        )
        await session.send_tool_response(
            function_responses=[types.FunctionResponse(id="call-1", name="set_devices", response={"result": "ok"})]
        )
        audio = b""
        heard = spoken = ""
        calls = []
        async for message in session.receive():
            if message.data:
                audio += message.data
            content = message.server_content
            if content and content.input_transcription and content.input_transcription.text:
                heard += content.input_transcription.text
            if content and content.output_transcription and content.output_transcription.text:
                spoken += content.output_transcription.text
            if message.tool_call:
                calls += [(call.id, call.name, call.args) for call in message.tool_call.function_calls]
            if content and content.turn_complete:
                break
    server.close()
    import struct

    samples = struct.unpack(f"<{len(audio) // 2}h", audio)
    print(f"SDK read the fixture: {len(samples)} samples (sum {sum(samples)}), heard '{heard}', spoken '{spoken}', calls {calls}")
    assert len(samples) == 600 and heard == "zhasni světlo" and spoken == "Zhasnuto."
    assert calls == [("function-call-1", "set_devices", {"ids": ["abc_1"], "on": False})]

    mine = [json.loads(line) for line in open(sys.argv[1], encoding="utf-8")]
    print("SDK dials:", dialled[0][0].replace("TESTKEY", "<key>"))
    print("SDK headers:", dialled[0][1])
    failed = False
    order = ("setup", "audio", "stream end", "text prompt", "tool response")
    mine = [mine[0], mine[1], mine[2], mine[4], mine[3]]
    assert len(captured) >= len(order), captured
    for label, sdk, own in zip(order, captured, mine):
        extra = sorted(keys(own) - keys(sdk))
        missing = sorted(keys(sdk) - keys(own))
        print(f"{label}: only in firmware {extra} | only in SDK {missing}")
        failed = failed or bool(extra) or bool(missing)
    print("SDK setup:", json.dumps(captured[0], ensure_ascii=False))
    print("SDK audio:", json.dumps(captured[1]))
    print("SDK text :", json.dumps(captured[3], ensure_ascii=False))
    print("SDK tool :", json.dumps(captured[4]))
    sys.exit(1 if failed else 0)


asyncio.run(main())
