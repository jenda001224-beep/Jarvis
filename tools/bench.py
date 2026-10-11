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
        return None
    text = json.dumps(values, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(text) >= size:
        fail("settings are too long")
    return text + b"\0" + b"\xff" * (size - len(text) - 1)


def esptool(port, *arguments, quiet=False):
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "-p", port, "-b", "460800", *arguments]
    subprocess.run(command, check=True, capture_output=quiet)


def stored_settings(image):
    end = next((index for index, byte in enumerate(image) if byte in (0x00, 0xFF)), len(image))
    return json.loads(image[:end].decode("utf-8")) if end else {}


def change(options):
    port = find_port(options.port)
    start, size = config_partition()
    folder = ROOT / ".tools" / "tmp"
    folder.mkdir(parents=True, exist_ok=True)
    before = folder / "cfg-before.bin"
    after = folder / "cfg-after.bin"
    try:
        esptool(port, "--after", "no_reset", "read_flash", hex(start), str(size), str(before), quiet=True)
        values = stored_settings(before.read_bytes())
        for pair in options.pairs:
            name, _, text = pair.partition("=")
            if not name or not _:
                fail(f"expected name=value, got {pair}")
            try:
                values[name] = json.loads(text)
            except ValueError:
                values[name] = text
        for name in options.clipboard or []:
            values[name] = subprocess.run(["pbpaste"], capture_output=True, text=True, check=True).stdout.strip()
        for name in options.remove or []:
            values.pop(name, None)
        text = json.dumps(values, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if len(text) >= size:
            fail("settings are too long")
        after.write_bytes(text + b"\0" + b"\xff" * (size - len(text) - 1))
        esptool(port, "--after", "no_reset" if options.stay else "hard_reset", "write_flash", hex(start), str(after),
                quiet=True)
        print("bench: the board now stores", ", ".join(sorted(values)))
    finally:
        before.unlink(missing_ok=True)
        after.unlink(missing_ok=True)


def flash(options):
    port = find_port(options.port)
    args = json.loads((BUILD / "flasher_args.json").read_text(encoding="utf-8"))
    files = []
    if options.app_only:
        files += [args["app"]["offset"], str(BUILD / args["app"]["file"])]
    elif not options.settings_only:
        for offset, name in sorted(args["flash_files"].items(), key=lambda item: int(item[0], 16)):
            files += [offset, str(BUILD / name)]
    start, size = config_partition()
    data = settings_image(options.settings, size) if Path(options.settings).exists() else None
    if data and not options.app_only:
        image = ROOT / ".tools" / "tmp" / "cfg.bin"
        image.parent.mkdir(parents=True, exist_ok=True)
        image.write_bytes(data)
        files += [hex(start), str(image)]
    elif options.settings_only:
        fail(f"{options.settings} has no ssid and key")
    else:
        print("bench: keeping the settings already stored in the board")
    if options.erase:
        esptool(port, "erase_flash")
    esptool(port, "--before", "default_reset", "--after", "hard_reset", "write_flash", *args["write_flash_args"], *files)
    if options.seconds:
        log(options)


def open_link(port):
    import serial

    link = serial.Serial()
    link.port = port
    link.baudrate = BAUD
    link.timeout = 0.2
    link.dtr = False
    link.rts = False
    link.open()
    return link


def log(options):
    import serial

    port = find_port(options.port)
    if options.reset:
        esptool(port, "--before", "default_reset", "--after", "hard_reset", "chip_id")
    started = time.time()
    deadline = started + options.seconds
    planned = []
    for item in options.send or []:
        moment, _, text = item.partition(":")
        planned.append((float(moment), text))
    planned.sort()
    pending = b""
    link = None
    while time.time() < deadline:
        try:
            if link is None:
                link = open_link(find_port(options.port))
            while planned and time.time() - started >= planned[0][0]:
                link.write(planned.pop(0)[1].encode("utf-8") + b"\n")
                link.flush()
            pending += link.read(4096)
        except (serial.SerialException, OSError, SystemExit):
            if link is not None:
                try:
                    link.close()
                except Exception:
                    pass
                link = None
            time.sleep(0.3)
            continue
        while b"\n" in pending:
            line, pending = pending.split(b"\n", 1)
            print(line.decode("utf-8", "replace").rstrip())
            sys.stdout.flush()
    if link is not None:
        link.close()


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    flashing = commands.add_parser("flash")
    flashing.add_argument("--port")
    flashing.add_argument("--settings", default=str(SETTINGS))
    flashing.add_argument("--settings-only", action="store_true")
    flashing.add_argument("--app-only", action="store_true")
    flashing.add_argument("--erase", action="store_true")
    flashing.add_argument("--seconds", type=int, default=0)
    flashing.add_argument("--reset", action="store_true")
    flashing.add_argument("--send", action="append")
    flashing.set_defaults(run=flash)
    changing = commands.add_parser("set")
    changing.add_argument("pairs", nargs="*")
    changing.add_argument("--port")
    changing.add_argument("--clipboard", action="append")
    changing.add_argument("--remove", action="append")
    changing.add_argument("--stay", action="store_true")
    changing.set_defaults(run=change)
    logging = commands.add_parser("log")
    logging.add_argument("--port")
    logging.add_argument("--seconds", type=int, default=30)
    logging.add_argument("--reset", action="store_true")
    logging.add_argument("--send", action="append")
    logging.set_defaults(run=log)
    options = parser.parse_args()
    options.run(options)


if __name__ == "__main__":
    main()
