#!/usr/bin/env python3
import hashlib
import json
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROJECT = ROOT / "firmware"
BUILD = PROJECT / "build"
DOCS = ROOT / "docs"
FIRMWARE = DOCS / "firmware"

IMAGE_MAGIC = 0xE9
CHIP_ID_ESP32S3 = 9
PARTITION_MAGIC = 0x50AA
PARTITION_ENTRY = struct.Struct("<HBBII16sI")
TYPE_APP = 0x00
TYPE_DATA = 0x01
TYPE_CONFIG = 0x40
FLASH_SIZE = 16 * 1024 * 1024


def fail(message):
    sys.exit(f"release: {message}")


def version():
    match = re.search(r'set\(PROJECT_VER "([^"]+)"\)', (PROJECT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not match:
        fail("PROJECT_VER not found in firmware/CMakeLists.txt")
    return match.group(1)


def partitions(table):
    found = {}
    for offset in range(0, len(table), PARTITION_ENTRY.size):
        magic, kind, subtype, start, size, label, _ = PARTITION_ENTRY.unpack_from(table, offset)
        if magic != PARTITION_MAGIC:
            break
        found[label.rstrip(b"\0").decode()] = (kind, subtype, start, size)
    return found


def check_image(data, label):
    if data[0] != IMAGE_MAGIC:
        fail(f"{label}: bad image magic 0x{data[0]:02x}")
    chip_id = struct.unpack_from("<H", data, 12)[0]
    if chip_id != CHIP_ID_ESP32S3:
        fail(f"{label}: chip id {chip_id}, expected ESP32-S3")


def main():
    args = json.loads((BUILD / "flasher_args.json").read_text(encoding="utf-8"))
    files = {int(offset, 16): BUILD / path for offset, path in args["flash_files"].items()}
    table_offset = int(args["partition-table"]["offset"], 16)
    layout = partitions(files[table_offset].read_bytes())

    for name in ("factory", "model", "cfg", "nvs"):
        if name not in layout:
            fail(f"partition {name} missing")
    app_kind, _, app_start, app_size = layout["factory"]
    _, _, model_start, model_size = layout["model"]
    config_kind, _, config_start, config_size = layout["cfg"]
    if app_kind != TYPE_APP or config_kind != TYPE_CONFIG:
        fail("unexpected partition types")

    bootloader = files[0].read_bytes()
    app = files[app_start].read_bytes()
    model = files[model_start].read_bytes()
    check_image(bootloader, "bootloader")
    check_image(app, "app")
    if len(bootloader) > table_offset:
        fail("bootloader overlaps the partition table")
    if len(app) > app_size:
        fail(f"app is {len(app)} bytes, partition holds {app_size}")
    if len(model) > model_size:
        fail(f"wake word models are {len(model)} bytes, partition holds {model_size}")
    if b"wn9_jarvis_tts" not in model:
        fail("the Jarvis wake word model is not in srmodels.bin")
    if version().encode() not in app:
        fail(f"version {version()} not found in the app image")

    spans = sorted((start, start + size, name) for name, (_, _, start, size) in layout.items())
    for (_, end, first), (start, _, second) in zip(spans, spans[1:]):
        if end > start:
            fail(f"partitions {first} and {second} overlap")
    if spans[-1][1] > FLASH_SIZE:
        fail("partitions exceed the 16 MB flash")

    written = sorted((offset, offset + path.stat().st_size, path.name) for offset, path in files.items())
    protected = [layout["nvs"], layout["cfg"]]
    for start, end, name in written:
        for _, _, guarded_start, guarded_size in protected:
            if start < guarded_start + guarded_size and end > guarded_start:
                fail(f"{name} would overwrite stored settings")

    if FIRMWARE.exists():
        shutil.rmtree(FIRMWARE)
    FIRMWARE.mkdir(parents=True)
    parts = []
    total = 0
    for offset, path in sorted(files.items()):
        shutil.copyfile(path, FIRMWARE / path.name)
        data = path.read_bytes()
        total += len(data)
        parts.append({"path": f"firmware/{path.name}", "offset": offset, "md5": hashlib.md5(data).hexdigest()})

    manifest = {
        "name": "Jarvis",
        "version": version(),
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 0,
        "config": {"offset": config_start, "size": config_size},
        "builds": [{"chipFamily": "ESP32-S3", "size": total, "parts": parts}],
    }
    (DOCS / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Jarvis {version()}")
    for part in parts:
        print(f"  0x{part['offset']:06x}  {part['path']}")
    print(f"  0x{config_start:06x}  settings written by the flasher ({config_size} bytes)")


if __name__ == "__main__":
    main()
