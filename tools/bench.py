#!/usr/bin/env python3
import argparse
import glob
import json
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "firmware" / "build"
SETTINGS = ROOT / "nastaveni.json"
PARTITION_ENTRY = struct.Struct("<HBBII16sI")
PARTITION_MAGIC = 0x50AA
PORT_PATTERNS = ("/dev/cu.wchusbserial*", "/dev/cu.usbserial*", "/dev/cu.usbmodem*")
BAUD = 115200


def fail(message):
    sys.exit(f"bench: {message}")


def find_port(explicit):
    if explicit:
        return explicit
    ports = [port for pattern in PORT_PATTERNS for port in sorted(glob.glob(pattern))]
    if not ports:
        fail("no serial port found; plug the board in with a data cable")
    return ports[0]


def config_partition():
    table = (BUILD / "partition_table" / "partition-table.bin").read_bytes()
    for offset in range(0, len(table), PARTITION_ENTRY.size):
        magic, _, _, start, size, label, _ = PARTITION_ENTRY.unpack_from(table, offset)
        if magic != PARTITION_MAGIC:
            break
        if label.rstrip(b"\0") == b"cfg":
            return start, size
    fail("cfg partition not found")


def settings_image(path, size):
    values = json.loads(Path(path).read_text(encoding="utf-8"))
    if not values.get("ssid") or not values.get("key"):
        fail(f"{path} needs at least ssid, password and key")
    text = json.dumps(values, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(text) >= size:
        fail("settings are too long")
    return text + b"\0" + b"\xff" * (size - len(text) - 1)


def esptool(port, *arguments):
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "-b", "460800", *arguments]
    subprocess.run(command, check=True)


def flash(options):
    port = find_port(options.port)
    args = json.loads((BUILD / "flasher_args.json").read_text(encoding="utf-8"))
    files = []
    if not options.settings_only:
        for offset, name in sorted(args["flash_files"].items(), key=lambda item: int(item[0], 16)):
            files += [offset, str(BUILD / name)]
    if Path(options.settings).exists():
        start, size = config_partition()
        image = ROOT / ".tools" / "tmp" / "cfg.bin"
        image.parent.mkdir(parents=True, exist_ok=True)
        image.write_bytes(settings_image(options.settings, size))
        files += [hex(start), str(image)]
    elif options.settings_only:
        fail(f"{options.settings} does not exist")
    else:
        print(f"bench: {options.settings} not found, flashing without settings")
    if options.erase:
        esptool(port, "erase_flash")
    esptool(port, "--before", "default_reset", "--after", "hard_reset", "write_flash", *args["write_flash_args"], *files)
    if options.seconds:
        log(options)


def log(options):
    import serial

    port = find_port(options.port)
    deadline = time.time() + options.seconds
    with serial.Serial(port, BAUD, timeout=0.2) as link:
        if options.reset:
            link.dtr = False
            link.rts = True
            time.sleep(0.1)
            link.rts = False
        pending = b""
        while time.time() < deadline:
            pending += link.read(4096)
            while b"\n" in pending:
                line, pending = pending.split(b"\n", 1)
                print(line.decode("utf-8", "replace").rstrip())
                sys.stdout.flush()


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    flashing = commands.add_parser("flash")
    flashing.add_argument("--port")
    flashing.add_argument("--settings", default=str(SETTINGS))
    flashing.add_argument("--settings-only", action="store_true")
    flashing.add_argument("--erase", action="store_true")
    flashing.add_argument("--seconds", type=int, default=0)
    flashing.add_argument("--reset", action="store_true")
    flashing.set_defaults(run=flash)
    logging = commands.add_parser("log")
    logging.add_argument("--port")
    logging.add_argument("--seconds", type=int, default=30)
    logging.add_argument("--reset", action="store_true")
    logging.set_defaults(run=log)
    options = parser.parse_args()
    options.run(options)


if __name__ == "__main__":
    main()
