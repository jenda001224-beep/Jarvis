#!/bin/sh
set -e
cd "$(dirname "$0")"
CJSON="${IDF_PATH:-/Volumes/Jan/Z-Macu/Library/Caches/esphome/idf/frameworks/5.5.5}/components/json/cJSON"
OUT="${TMPDIR:-/tmp}/jarvis-tests"
mkdir -p "$OUT"
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I../main -o "$OUT/test_resample" test_resample.c ../main/resample.c -lm
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -DFIXTURE_SUM=-915900 -I../main -I"$CJSON" -o "$OUT/test_proto" test_proto.c ../main/proto.c "$CJSON/cJSON.c" -lm 2>/dev/null
cc -std=c11 -w -I../main -I"$CJSON" -o "$OUT/dump_wire" dump_wire.c ../main/proto.c "$CJSON/cJSON.c" -lm
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I../main -I"$CJSON" -o "$OUT/test_modules" test_modules.c ../main/bambu.c ../main/reminders.c ../main/geo.c ../main/meteo.c ../main/proto.c ../main/tavily.c ../main/todos.c ../main/alarms.c "$CJSON/cJSON.c" -lm 2>/dev/null
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address,undefined -I../main -I"$CJSON" -o "$OUT/agenda_cli" agenda_cli.c ../main/agenda.c "$CJSON/cJSON.c" -lm 2>/dev/null
"$OUT/test_resample"
"$OUT/test_proto"
"$OUT/test_modules"
"$OUT/dump_wire" > "$OUT/wire.jsonl"
PYTHON="${PYTHON:-../../.tools/venv/bin/python}"
"$PYTHON" sdk_wire.py "$OUT/wire.jsonl"
"$PYTHON" agenda_check.py "$OUT/agenda_cli"
