#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "textfold.h"

#define FIXED_ONE 65536U
#define NOISE_LEVEL 11000.0f

typedef struct {
    const char *name;
    const char *key;
    const char *url;
} station_t;

static const station_t STATIONS[] = {
    {"Radiožurnál", "radiozurnal", "https://rozhlas.stream/radiozurnal.mp3"},
    {"Dvojka", "dvojka", "https://rozhlas.stream/dvojka.mp3"},
    {"Vltava", "vltava", "https://rozhlas.stream/vltava.mp3"},
    {"Plus", "plus", "https://rozhlas.stream/plus.mp3"},
    {"Radio Wave", "wave", "https://rozhlas.stream/radio_wave.mp3"},
    {"Jazz", "jazz", "https://rozhlas.stream/jazz.mp3"},
    {"Pohoda", "pohoda", "https://rozhlas.stream/pohoda.mp3"},
    {"Evropa 2", "evrop", "https://ice.actve.net/fm-evropa2-128"},
    {"Frekvence 1", "frekvenc", "https://ice.actve.net/fm-frekvence1-128"},
    {"Kiss", "kiss", "https://icecast7.play.cz/kiss128.mp3"},
};

void rate_init(rate_t *rate, uint32_t input_rate, uint32_t output_rate)
{
    memset(rate, 0, sizeof(*rate));
    rate->step = (uint32_t) (((uint64_t) input_rate * FIXED_ONE) / output_rate);
    rate->smooth = input_rate * 2 > output_rate * 3;
}

size_t rate_capacity(const rate_t *rate, size_t input_samples)
{
    return (size_t) (((uint64_t) input_samples * FIXED_ONE) / rate->step) + 2;
}

size_t rate_process(rate_t *rate, const int16_t *in, size_t count, int16_t *out, size_t capacity)
{
    size_t produced = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = in[i];
        if (rate->smooth) {
            int32_t filtered = (rate->history[1] + 2 * rate->history[0] + sample) / 4;
            rate->history[1] = rate->history[0];
            rate->history[0] = sample;
            sample = filtered;
        }
        while (rate->phase < FIXED_ONE && produced < capacity) {
            int32_t value = rate->previous + (int32_t) (((int64_t) (sample - rate->previous) * rate->phase) / FIXED_ONE);
            out[produced++] = (int16_t) value;
            rate->phase += rate->step;
        }
        rate->phase = rate->phase >= FIXED_ONE ? rate->phase - FIXED_ONE : 0;
        rate->previous = sample;
    }
    return produced;
}

void sound_to_mono(int16_t *pcm, size_t frames, int channels)
{
    if (channels < 2) {
        return;
    }
    for (size_t i = 0; i < frames; i++) {
        int32_t sum = 0;
        for (int c = 0; c < channels; c++) {
            sum += pcm[i * (size_t) channels + (size_t) c];
        }
        pcm[i] = (int16_t) (sum / channels);
    }
}

void noise_init(noise_t *noise, uint32_t seed)
{
    memset(noise, 0, sizeof(*noise));
    noise->seed = seed ? seed : 0x9E3779B9U;
}

static float next_white(noise_t *noise)
{
    uint32_t x = noise->seed;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    noise->seed = x;
    return (float) (int32_t) x / 2147483648.0f;
}

void noise_fill(noise_t *noise, noise_kind_t kind, int16_t *out, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        float white = next_white(noise);
        float value = white;
        if (kind == NOISE_PINK) {
            float *b = noise->pink;
            b[0] = 0.99886f * b[0] + white * 0.0555179f;
            b[1] = 0.99332f * b[1] + white * 0.0750759f;
            b[2] = 0.96900f * b[2] + white * 0.1538520f;
            b[3] = 0.86650f * b[3] + white * 0.3104856f;
            b[4] = 0.55000f * b[4] + white * 0.5329522f;
            b[5] = -0.7616f * b[5] - white * 0.0168980f;
            value = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + white * 0.5362f) * 0.11f;
            b[6] = white * 0.115926f;
        } else if (kind == NOISE_BROWN) {
            noise->brown = (noise->brown + 0.02f * white) / 1.02f;
            value = noise->brown * 3.5f;
        }
        float scaled = value * NOISE_LEVEL;
        scaled = scaled > 32000.0f ? 32000.0f : scaled < -32000.0f ? -32000.0f : scaled;
        out[i] = (int16_t) scaled;
    }
}

sound_format_t sound_sniff(const uint8_t *data, size_t length)
{
    if (length >= 3 && memcmp(data, "ID3", 3) == 0) {
        return SOUND_MP3;
    }
    for (size_t i = 0; i + 1 < length; i++) {
        if (data[i] != 0xFF) {
            continue;
        }
        if ((data[i + 1] & 0xF6) == 0xF0) {
            return SOUND_AAC;
        }
        if ((data[i + 1] & 0xE0) == 0xE0 && (data[i + 1] & 0x06) != 0) {
            return SOUND_MP3;
        }
    }
    return SOUND_UNKNOWN;
}

static const char *find_in(const char *from, const char *end, const char *needle)
{
    size_t size = strlen(needle);
    for (const char *c = from; c + size <= end; c++) {
        if (memcmp(c, needle, size) == 0) {
            return c;
        }
    }
    return NULL;
}

static void copy_decoded(const char *from, const char *to, char *out, size_t capacity)
{
    static const char CDATA[] = "<![CDATA[";
    if ((size_t) (to - from) >= sizeof(CDATA) - 1 && memcmp(from, CDATA, sizeof(CDATA) - 1) == 0) {
        from += sizeof(CDATA) - 1;
        const char *close = find_in(from, to, "]]>");
        to = close ? close : to;
    }
    while (from < to && (*from == ' ' || *from == '\n' || *from == '\r' || *from == '\t')) {
        from++;
    }
    size_t used = 0;
    while (from < to && used + 5 < capacity) {
        const char *stop = *from == '&' ? memchr(from, ';', (size_t) (to - from)) : NULL;
        if (!stop || stop - from > 8) {
            out[used++] = *from++;
            continue;
        }
        size_t size = (size_t) (stop - from) + 1;
        long code = -1;
        if (from[1] == '#') {
            code = from[2] == 'x' || from[2] == 'X' ? strtol(from + 3, NULL, 16) : strtol(from + 2, NULL, 10);
        } else if (size == 5 && memcmp(from, "&amp;", 5) == 0) {
            code = '&';
        } else if (size == 4 && memcmp(from, "&lt;", 4) == 0) {
            code = '<';
        } else if (size == 4 && memcmp(from, "&gt;", 4) == 0) {
            code = '>';
        } else if (size == 6 && memcmp(from, "&quot;", 6) == 0) {
            code = '"';
        } else if (size == 6 && memcmp(from, "&apos;", 6) == 0) {
            code = '\'';
        }
        if (code <= 0 || code >= 0x10000) {
            out[used++] = *from++;
            continue;
        }
        if (code < 0x80) {
            out[used++] = (char) code;
        } else if (code < 0x800) {
            out[used++] = (char) (0xC0 | (code >> 6));
            out[used++] = (char) (0x80 | (code & 0x3F));
        } else {
            out[used++] = (char) (0xE0 | (code >> 12));
            out[used++] = (char) (0x80 | ((code >> 6) & 0x3F));
            out[used++] = (char) (0x80 | (code & 0x3F));
        }
        from += size;
    }
    while (used > 0 && (out[used - 1] == ' ' || out[used - 1] == '\n' || out[used - 1] == '\r' || out[used - 1] == '\t')) {
        used--;
    }
    while (used > 0 && ((unsigned char) out[used - 1] & 0xC0) == 0x80 && used + 1 == capacity) {
        used--;
    }
    out[used] = 0;
}

bool rss_first_item(const char *xml, size_t length, char *title, size_t title_capacity, char *url, size_t url_capacity)
{
    const char *end = xml + length;
    const char *item = find_in(xml, end, "<item");
    if (!item) {
        return false;
    }
    const char *close = find_in(item, end, "</item>");
    if (close) {
        end = close;
    }
    title[0] = 0;
    const char *name = find_in(item, end, "<title");
    if (name) {
        const char *open = memchr(name, '>', (size_t) (end - name));
        const char *stop = open ? find_in(open, end, "</title>") : NULL;
        if (stop) {
            copy_decoded(open + 1, stop, title, title_capacity);
        }
    }
    const char *enclosure = find_in(item, end, "<enclosure");
    if (!enclosure) {
        return false;
    }
    const char *tag_end = memchr(enclosure, '>', (size_t) (end - enclosure));
    const char *address = tag_end ? find_in(enclosure, tag_end, "url=") : NULL;
    if (!address || address + 5 >= tag_end) {
        return false;
    }
    char quote = address[4];
    const char *from = address + 5;
    const char *to = memchr(from, quote, (size_t) (tag_end - from));
    if (!to || (quote != '"' && quote != '\'')) {
        return false;
    }
    copy_decoded(from, to, url, url_capacity);
    return url[0] != 0;
}

int station_count(void)
{
    return (int) (sizeof(STATIONS) / sizeof(STATIONS[0]));
}

const char *station_name(int index)
{
    return STATIONS[index].name;
}

const char *station_url(int index)
{
    return STATIONS[index].url;
}

int station_match(const char *wanted)
{
    char folded[96];
    text_fold(wanted, folded, sizeof(folded));
    for (int i = 0; i < station_count(); i++) {
        if (strstr(folded, STATIONS[i].key)) {
            return i;
        }
    }
    return -1;
}
