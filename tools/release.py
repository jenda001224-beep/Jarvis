#!/usr/bin/env python3
import hashlib
import json
import re
import shutil
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "esphome" / "jarvis.yaml"
BUILD = ROOT / "esphome" / ".esphome" / "build" / "jarvis" / "build"
DOCS = ROOT / "docs"
FIRMWARE = DOCS / "firmware"

IMAGE_MAGIC = 0xE9
CHIP_ID_ESP32S3 = 9
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_MAGIC = 0x50AA
PARTITION_TYPE_APP = 0x00
PARTITION_SUBTYPE_OTA_0 = 0x10


def fail(message):
    sys.exit(f"release: {message}")


def substitution(name):
    match = re.search(rf'^  {name}:\s*"?([^"\n]+)"?\s*$', CONFIG.read_text(encoding="utf-8"), re.MULTILINE)
    if not match:
        fail(f"substitution {name} not found in {CONFIG.name}")
    return match.group(1).strip()


def check_image(data, offset, label):
    if data[offset] != IMAGE_MAGIC:
        fail(f"{label}: bad image magic 0x{data[offset]:02x} at 0x{offset:x}")
    chip_id = struct.unpack_from("<H", data, offset + 12)[0]
    if chip_id != CHIP_ID_ESP32S3:
        fail(f"{label}: chip id {chip_id}, expected ESP32-S3")


def app_partition(data):
    offset = PARTITION_TABLE_OFFSET
    while True:
        magic, kind, subtype, start, size = struct.unpack_from("<HBBII", data, offset)
        if magic != PARTITION_MAGIC:
            fail("no ota_0 partition in the partition table")
        if kind == PARTITION_TYPE_APP and subtype == PARTITION_SUBTYPE_OTA_0:
            return start, size
        offset += 32


def main():
    factory = (BUILD / "firmware.factory.bin").read_bytes()
    ota = (BUILD / "firmware.ota.bin").read_bytes()
    version = substitution("version")
    update_url = substitution("update_url")

    check_image(factory, 0, "bootloader")
    check_image(ota, 0, "app")
    start, size = app_partition(factory)
    if len(ota) > size:
        fail(f"app is {len(ota)} bytes, partition holds {size}")
    if factory[start : start + len(ota)] != ota:
        fail("factory image does not contain the OTA image at the app offset")
    if "!secret" in CONFIG.read_text(encoding="utf-8"):
        fail(f"{CONFIG.name} uses !secret, a public image must not embed secrets")
    if version.encode() not in ota:
        fail(f"version {version} not found in the app image")

    FIRMWARE.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(BUILD / "firmware.factory.bin", FIRMWARE / "jarvis.factory.bin")
    shutil.copyfile(BUILD / "firmware.ota.bin", FIRMWARE / "jarvis.ota.bin")

    manifest = {
        "name": "Jarvis",
        "version": version,
        "home_assistant_domain": "esphome",
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 20,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "size": len(factory),
                "ota": {
                    "md5": hashlib.md5(ota).hexdigest(),
                    "path": "firmware/jarvis.ota.bin",
                    "summary": f"Jarvis {version}",
                    "release_url": update_url.rsplit("/", 1)[0] + "/",
                },
                "parts": [{"path": "firmware/jarvis.factory.bin", "offset": 0}],
            }
        ],
    }
    (DOCS / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print(f"Jarvis {version}")
    print(f"  factory {len(factory)} bytes, app at 0x{start:x}, {len(ota)} of {size} bytes")
    print(f"  manifest {DOCS / 'manifest.json'}")


if __name__ == "__main__":
    main()
