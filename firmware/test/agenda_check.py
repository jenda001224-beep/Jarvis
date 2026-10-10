import datetime
import random
import subprocess
import sys
import tempfile
from pathlib import Path
from zoneinfo import ZoneInfo

import icalendar
import recurring_ical_events

CLI = sys.argv[1] if len(sys.argv) > 1 else ""
PRAGUE = ZoneInfo("Europe/Prague")
UTC = datetime.timezone.utc
DAYS = ["MO", "TU", "WE", "TH", "FR", "SA", "SU"]

HANDMADE = """BEGIN:VCALENDAR\r
VERSION:2.0\r
PRODID:-//Apple Inc.//iCloud Calendar//EN\r
X-WR-CALNAME:Domov\r
BEGIN:VTIMEZONE\r
TZID:Europe/Prague\r
BEGIN:DAYLIGHT\r
DTSTART:19810329T020000\r
RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\r
TZNAME:CEST\r
TZOFFSETFROM:+0100\r
TZOFFSETTO:+0200\r
END:DAYLIGHT\r
BEGIN:STANDARD\r
DTSTART:19961027T030000\r
RRULE:FREQ=YEARLY;BYMONTH=10;BYDAY=-1SU\r
TZNAME:CET\r
TZOFFSETFROM:+0200\r
TZOFFSETTO:+0100\r
END:STANDARD\r
END:VTIMEZONE\r
BEGIN:VEVENT\r
UID:doctor-1\r
DTSTAMP:20261001T080000Z\r
DTSTART;TZID=Europe/Prague:20261012T110000\r
DTEND;TZID=Europe/Prague:20261012T120000\r
SUMMARY:Doktor\\, kontrola\r
LOCATION:Poliklinika Budějovická\\nAntala Staška 1670/80\\, 140 00 Praha 4\\, Č\r
 esko\r
BEGIN:VALARM\r
ACTION:DISPLAY\r
DESCRIPTION:Tohle je upozornění\\, ne název\r
TRIGGER:-PT30M\r
END:VALARM\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:birthday-1\r
DTSTART;VALUE=DATE:19900312\r
DTEND;VALUE=DATE:19900313\r
RRULE:FREQ=YEARLY\r
SUMMARY:Narozeniny Petra\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:holiday-1\r
DTSTART;VALUE=DATE:20261010\r
DTEND;VALUE=DATE:20261014\r
SUMMARY:Dovolená\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:training-1\r
DTSTART;TZID=Europe/Prague:20260105T180000\r
DTEND;TZID=Europe/Prague:20260105T193000\r
RRULE:FREQ=WEEKLY;BYDAY=MO,WE\r
EXDATE;TZID=Europe/Prague:20261014T180000\r
SUMMARY:Trénink\r
LOCATION:Hala\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:training-1\r
RECURRENCE-ID;TZID=Europe/Prague:20261019T180000\r
DTSTART;TZID=Europe/Prague:20261020T170000\r
DTEND;TZID=Europe/Prague:20261020T183000\r
SUMMARY:Trénink (přesunutý)\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:meetup-1\r
DTSTART;TZID=Europe/Prague:20260113T190000\r
DURATION:PT2H\r
RRULE:FREQ=MONTHLY;BYDAY=2TU\r
SUMMARY:Sraz\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:utc-1\r
DTSTART:20261012T063000Z\r
DTEND:20261012T070000Z\r
SUMMARY:Hovor\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:done-1\r
DTSTART;TZID=Europe/Prague:20260901T080000\r
DTEND;TZID=Europe/Prague:20260901T083000\r
RRULE:FREQ=DAILY;COUNT=10\r
SUMMARY:Skončená série\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:cancelled-1\r
DTSTART;TZID=Europe/Prague:20261012T150000\r
DTEND;TZID=Europe/Prague:20261012T160000\r
STATUS:CANCELLED\r
SUMMARY:Zrušeno\r
END:VEVENT\r
BEGIN:VEVENT\r
UID:nightly-1\r
DTSTART;TZID=Europe/Prague:20261020T220000\r
DTEND;TZID=Europe/Prague:20261021T060000\r
RRULE:FREQ=DAILY;UNTIL=20261030T210000Z\r
SUMMARY:Noční\r
END:VEVENT\r
END:VCALENDAR\r
"""


def run_cli(path, start, end, chunk=4096):
    output = subprocess.run(
        [CLI, str(path), str(int(start.timestamp())), str(int(end.timestamp())), str(chunk)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    rows = []
    for line in output.splitlines():
        first, last, all_day, title, _place = line.split("\t")
        rows.append((int(first), int(last), all_day == "1", title))
    return sorted(rows)


def moment(value):
    if isinstance(value, datetime.datetime):
        if value.tzinfo is None:
            value = value.replace(tzinfo=PRAGUE)
        return int(value.timestamp()), False
    return int(datetime.datetime(value.year, value.month, value.day, tzinfo=PRAGUE).timestamp()), True


def reference(text, start, end):
    calendar = icalendar.Calendar.from_ical(text)
    exact = {
        str(event["UID"]): int(event["DURATION"].dt.total_seconds())
        for event in calendar.walk("VEVENT")
        if "DURATION" in event and "RECURRENCE-ID" not in event
    }
    rows = []
    for event in recurring_ical_events.of(calendar).between(start, end):
        if str(event.get("STATUS", "")).upper() == "CANCELLED":
            continue
        first, all_day = moment(event["DTSTART"].dt)
        if str(event["UID"]) in exact and not all_day:
            last = first + exact[str(event["UID"])]
        elif "DTEND" in event:
            last, _ = moment(event["DTEND"].dt)
        elif "DURATION" in event:
            last = first + int(event["DURATION"].dt.total_seconds())
        else:
            last = first + 86400 if all_day else first
        overlaps = (first < end.timestamp() and last > start.timestamp()) if last > first else (
            start.timestamp() <= first < end.timestamp()
        )
        if overlaps:
            rows.append((first, last, all_day, str(event.get("SUMMARY", ""))))
    return sorted(rows)


def compare(label, text, windows, chunks=(4096,)):
    failures = 0
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "calendar.ics"
        path.write_bytes(text.encode("utf-8"))
        for start, end in windows:
            expected = reference(text, start, end)
            for chunk in chunks:
                mine = run_cli(path, start, end, chunk)
                if mine != expected:
                    failures += 1
                    print(f"MISMATCH {label} window {start} .. {end} chunk {chunk}")
                    for row in sorted(set(expected) - set(mine)):
                        print("  missing ", datetime.datetime.fromtimestamp(row[0], PRAGUE), row)
                    for row in sorted(set(mine) - set(expected)):
                        print("  extra   ", datetime.datetime.fromtimestamp(row[0], PRAGUE), row)
    return failures


def window(year, month, day, days):
    start = datetime.datetime(year, month, day, tzinfo=PRAGUE)
    return start, (start + datetime.timedelta(days=days)).astimezone(PRAGUE).replace(hour=0)


def random_rule(rng, start):
    freq = rng.choice(["DAILY", "WEEKLY", "WEEKLY", "MONTHLY", "MONTHLY", "YEARLY"])
    parts = [f"FREQ={freq}"]
    if rng.random() < 0.4:
        parts.append(f"INTERVAL={rng.choice([2, 3, 4])}")
    if freq == "WEEKLY" and rng.random() < 0.7:
        chosen = sorted(rng.sample(range(7), rng.randint(1, 4)))
        weekday = start.weekday()
        if weekday not in chosen:
            chosen = sorted(chosen + [weekday])
        parts.append("BYDAY=" + ",".join(DAYS[i] for i in chosen))
    if freq == "MONTHLY":
        style = rng.random()
        if style < 0.35:
            ordinal = (start.day - 1) // 7 + 1
            if ordinal <= 4:
                parts.append(f"BYDAY={ordinal}{DAYS[start.weekday()]}")
        elif style < 0.5:
            parts.append(f"BYMONTHDAY={start.day}")
        elif style < 0.65 and (start + datetime.timedelta(days=7)).month != start.month:
            parts.append(f"BYDAY=-1{DAYS[start.weekday()]}")
    ending = rng.random()
    if ending < 0.25:
        parts.append(f"COUNT={rng.randint(1, 60)}")
    elif ending < 0.5:
        until = start + datetime.timedelta(days=rng.randint(1, 900))
        parts.append("UNTIL=" + until.astimezone(UTC).strftime("%Y%m%dT%H%M%SZ"))
    return ";".join(parts)


def random_calendar(rng):
    lines = ["BEGIN:VCALENDAR", "VERSION:2.0", "PRODID:-//test//EN"]
    for index in range(40):
        day = datetime.date(2024, 1, 1) + datetime.timedelta(days=rng.randint(0, 1100))
        lines += ["BEGIN:VEVENT", f"UID:event-{index}", f"SUMMARY:Událost {index}"]
        all_day = rng.random() < 0.25
        if all_day:
            span = rng.choice([1, 1, 1, 2, 3])
            lines.append(f"DTSTART;VALUE=DATE:{day:%Y%m%d}")
            lines.append(f"DTEND;VALUE=DATE:{day + datetime.timedelta(days=span):%Y%m%d}")
            start = datetime.datetime(day.year, day.month, day.day, tzinfo=PRAGUE)
        else:
            hour = rng.randint(0, 23)
            minute = rng.choice([0, 15, 30, 45])
            start = datetime.datetime(day.year, day.month, day.day, hour, minute, tzinfo=PRAGUE)
            minutes = rng.choice([0, 30, 60, 90, 600, 1500])
            lines.append(f"DTSTART;TZID=Europe/Prague:{start:%Y%m%dT%H%M%S}")
            if minutes and rng.random() < 0.5:
                lines.append(f"DURATION:PT{minutes}M")
            elif minutes:
                lines.append(f"DTEND;TZID=Europe/Prague:{start + datetime.timedelta(minutes=minutes):%Y%m%dT%H%M%S}")
        if rng.random() < 0.75:
            lines.append("RRULE:" + random_rule(rng, start))
            if not all_day and rng.random() < 0.4:
                skipped = [start + datetime.timedelta(days=rng.randint(1, 1100)) for _ in range(40)]
                lines.append("EXDATE;TZID=Europe/Prague:" + ",".join(f"{day:%Y%m%dT%H%M%S}" for day in skipped))
        lines.append("END:VEVENT")
    lines.append("END:VCALENDAR")
    return "\r\n".join(lines) + "\r\n"


def main():
    failures = compare(
        "handmade",
        HANDMADE,
        [window(2026, 10, 12, 1), window(2026, 10, 12, 14), window(2026, 3, 9, 7), window(2026, 10, 24, 3),
         window(2027, 3, 12, 1), window(2026, 9, 9, 3)],
        chunks=(4096, 1, 2, 3, 7, 61),
    )
    monday = run_cli_text(HANDMADE, window(2026, 10, 12, 1))
    titles = [row[3] for row in monday]
    assert titles == ["Dovolená", "Hovor", "Doktor, kontrola", "Trénink"], titles
    week = [row[3] for row in run_cli_text(HANDMADE, window(2026, 10, 12, 14))]
    assert week.count("Trénink") == 2 and "Trénink (přesunutý)" in week and "Zrušeno" not in week, week
    assert "Skončená série" not in week and "Sraz" in week, week
    rng = random.Random(20261010)
    windows = [window(2026, 10, 12, 14), window(2026, 3, 23, 14), window(2026, 10, 19, 14), window(2025, 2, 24, 7),
               window(2027, 1, 1, 10)]
    for index in range(60):
        failures += compare(f"random {index}", random_calendar(rng), windows)
    print("agenda: handmade calendar and 60 random calendars compared with recurring_ical_events,", failures,
          "mismatches")
    sys.exit(1 if failures else 0)


def run_cli_text(text, bounds):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder) / "calendar.ics"
        path.write_bytes(text.encode("utf-8"))
        return sorted(run_cli(path, *bounds), key=lambda row: (row[0], row[3]))


if __name__ == "__main__":
    main()
