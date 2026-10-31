#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "journal.h"
#include "textfold.h"

#define FLASH_SIZE (32 * JOURNAL_SECTOR)

static uint8_t flash[FLASH_SIZE];
static int erases;
static int fail_write_after = -1;

static bool mock_read(void *context, size_t offset, void *out, size_t length)
{
    (void) context;
    assert(offset + length <= FLASH_SIZE);
    memcpy(out, flash + offset, length);
    return true;
}

static bool mock_write(void *context, size_t offset, const void *data, size_t length)
{
    (void) context;
    assert(offset + length <= FLASH_SIZE);
    const uint8_t *bytes = data;
    for (size_t i = 0; i < length; i++) {
        if (fail_write_after == 0) {
            return false;
        }
        if (fail_write_after > 0) {
            fail_write_after--;
        }
        flash[offset + i] &= bytes[i];
    }
    return true;
}

static bool mock_erase(void *context, size_t offset, size_t length)
{
    (void) context;
    assert(offset % JOURNAL_SECTOR == 0 && length % JOURNAL_SECTOR == 0 && offset + length <= FLASH_SIZE);
    memset(flash + offset, 0xFF, length);
    erases++;
    return true;
}

static const journal_flash_t MOCK = {
    .read = mock_read,
    .write = mock_write,
    .erase = mock_erase,
    .size = FLASH_SIZE,
};

static journal_t journal;

static void reopen(void)
{
    memset(&journal, 0xAA, sizeof(journal));
    assert(journal_open(&journal, &MOCK));
}

typedef struct {
    char texts[32][JOURNAL_LINE_MAX + 1];
    int64_t when[32];
    char who[32];
    int count;
} seen_t;

static bool collect(void *context, const journal_line_t *line, size_t offset, size_t length)
{
    (void) offset;
    (void) length;
    seen_t *seen = context;
    if (seen->count == 32) {
        return false;
    }
    strcpy(seen->texts[seen->count], line->text);
    seen->when[seen->count] = line->when;
    seen->who[seen->count] = line->who;
    seen->count++;
    return true;
}

static void test_fold(void)
{
    char out[64];
    text_fold("Příliš ŽLUŤOUČKÝ kůň ÚPĚL", out, sizeof(out));
    assert(strcmp(out, "prilis zlutoucky kun upel") == 0);
    text_fold("a€b", out, sizeof(out));
    assert(strcmp(out, "a b") == 0);
    text_fold("Šš", out, 2);
    assert(strcmp(out, "s") == 0);
}

static void test_append_and_order(void)
{
    memset(flash, 0xFF, sizeof(flash));
    reopen();
    assert(journal_count(&journal) == 0);
    assert(journal_append(&journal, 1000, 'P', "Dej mi v pátek k doktorovi"));
    assert(journal_append(&journal, 1001, 'J', "Zapsal jsem,\npane.\r\n"));
    reopen();
    assert(journal_append(&journal, 1002, 'P', "Díky"));
    seen_t seen = {0};
    journal_each(&journal, collect, &seen);
    assert(seen.count == 3);
    assert(strcmp(seen.texts[0], "Díky") == 0 && seen.when[0] == 1002 && seen.who[0] == 'P');
    assert(strcmp(seen.texts[1], "Zapsal jsem, pane. ") == 0 && seen.who[1] == 'J');
    assert(strcmp(seen.texts[2], "Dej mi v pátek k doktorovi") == 0);
    assert(journal_newest(&journal, 'P') == 1002 && journal_newest(&journal, 'J') == 1001);
    assert(journal_newest(&journal, 'N') == 0);
    assert(!journal_append(&journal, 1003, 'P', ""));
}

static void test_long_text_splits_on_character_boundary(void)
{
    memset(flash, 0xFF, sizeof(flash));
    reopen();
    char text[JOURNAL_LINE_MAX * 2 + 200];
    size_t length = 0;
    while (length + 2 < sizeof(text) - 1) {
        text[length++] = (char) 0xC5;
        text[length++] = (char) 0xA1;
    }
    text[length] = 0;
    assert(journal_append(&journal, 5, 'J', text));
    seen_t seen = {0};
    journal_each(&journal, collect, &seen);
    assert(seen.count == 3);
    size_t total = 0;
    for (int i = 0; i < seen.count; i++) {
        size_t size = strlen(seen.texts[i]);
        assert(size % 2 == 0 && size <= JOURNAL_LINE_MAX);
        total += size;
    }
    assert(total == length);
}

static void test_wraps_and_drops_oldest(void)
{
    memset(flash, 0xFF, sizeof(flash));
    reopen();
    char text[64];
    int written = 40000;
    for (int i = 0; i < written; i++) {
        snprintf(text, sizeof(text), "zaznam cislo %05d ktery je trochu delsi", i);
        assert(journal_append(&journal, 100000 + i, i % 2 ? 'J' : 'P', text));
    }
    reopen();
    seen_t seen = {0};
    journal_each(&journal, collect, &seen);
    snprintf(text, sizeof(text), "zaznam cislo %05d ktery je trochu delsi", written - 1);
    assert(seen.count == 32 && strcmp(seen.texts[0], text) == 0);
    int total = journal_count(&journal);
    assert(total > 1000 && total < written);
    assert(journal_newest(&journal, 0) == 100000 + written - 1);
    char *out = malloc(8192);
    size_t recent = journal_recent(&journal, 300, out, 8192);
    assert(recent > 0 && recent < 300 && out[recent] == 0 && out[recent - 1] == '\n');
    assert(strstr(out, text));
    size_t since = journal_since(&journal, 100000 + written - 3, out, 8192);
    assert(since > 0 && strstr(out, text) && !strstr(out, "39996 ktery"));
    assert(journal_since(&journal, 100000 + written - 1, out, 8192) == 0 && out[0] == 0);
    free(out);
}

static void test_search(void)
{
    memset(flash, 0xFF, sizeof(flash));
    reopen();
    assert(journal_append(&journal, 2000, 'P', "Sestra Jana má narozeniny patnáctého března"));
    assert(journal_append(&journal, 2001, 'J', "Zapamatuji si to, pane."));
    assert(journal_append(&journal, 2002, 'P', "Objednej mě k doktorovi Novákovi"));
    assert(journal_append(&journal, 2003, 'P', "Doktor řekl, že mám brát vitamín"));
    assert(journal_append(&journal, 2004, 'P', "Bazén Step zavírá v deset"));
    seen_t seen = {0};
    assert(journal_search(&journal, "doktora", collect, &seen, 8) == 2);
    assert(seen.when[0] == 2003 && seen.when[1] == 2002);
    memset(&seen, 0, sizeof(seen));
    assert(journal_search(&journal, "narozeniny Jany", collect, &seen, 8) == 1 && seen.when[0] == 2000);
    memset(&seen, 0, sizeof(seen));
    assert(journal_search(&journal, "BAZÉN", collect, &seen, 8) == 1 && seen.when[0] == 2004);
    memset(&seen, 0, sizeof(seen));
    assert(journal_search(&journal, "krtek", collect, &seen, 8) == 0);
    assert(journal_search(&journal, "a v", collect, &seen, 8) == 0);
    memset(&seen, 0, sizeof(seen));
    assert(journal_search(&journal, "doktora", collect, &seen, 1) == 1);
}

static void test_forget(void)
{
    assert(journal_forget(&journal, "doktor") == 2);
    seen_t seen = {0};
    assert(journal_search(&journal, "doktor", collect, &seen, 8) == 0);
    assert(journal_count(&journal) == 3);
    reopen();
    assert(journal_count(&journal) == 3);
    assert(journal_forget(&journal, "a v") == 0);
    assert(journal_append(&journal, 2010, 'P', "Po zapomenutí"));
    assert(journal_newest(&journal, 'P') == 2010);
    assert(journal_count(&journal) == 4);
}

static void test_notes(void)
{
    memset(flash, 0xFF, sizeof(flash));
    reopen();
    char out[JOURNAL_NOTES_MAX + 1];
    assert(journal_notes_read(&journal, out, sizeof(out)) == 0 && out[0] == 0);
    assert(journal_notes_write(&journal, "- Má psa Rexe\n"));
    assert(journal_notes_read(&journal, out, sizeof(out)) == 15 && strcmp(out, "- Má psa Rexe\n") == 0);
    reopen();
    assert(journal_notes_read(&journal, out, sizeof(out)) == 15);
    assert(journal_notes_add(&journal, "Sestra se\njmenuje Jana"));
    journal_notes_read(&journal, out, sizeof(out));
    assert(strcmp(out, "- Má psa Rexe\n- Sestra se jmenuje Jana\n") == 0);
    assert(journal_notes_forget(&journal, "krtek") == 0);
    assert(journal_notes_forget(&journal, "sestra") == 1);
    journal_notes_read(&journal, out, sizeof(out));
    assert(strcmp(out, "- Má psa Rexe\n") == 0);

    fail_write_after = 3;
    assert(!journal_notes_write(&journal, "- Rozbité\n"));
    fail_write_after = -1;
    reopen();
    journal_notes_read(&journal, out, sizeof(out));
    assert(strcmp(out, "- Má psa Rexe\n") == 0);

    size_t size = JOURNAL_NOTES_MAX + 600;
    char *big = malloc(size + 1);
    size_t length = 0;
    int number = 0;
    while (length + 40 < size) {
        length += (size_t) snprintf(big + length, size + 1 - length, "- Fakt číslo %d je důležitý\n", number++);
    }
    assert(journal_notes_write(&journal, big));
    size_t read = journal_notes_read(&journal, out, sizeof(out));
    assert(read <= JOURNAL_NOTES_MAX && read > JOURNAL_NOTES_MAX - 60 && out[read - 1] == '\n');
    assert(strncmp(big, out, read) == 0);
    free(big);
    assert(journal_notes_write(&journal, ""));
    assert(journal_notes_read(&journal, out, sizeof(out)) == 0);
}

static void test_torn_writes(void)
{
    memset(flash, 0xFF, sizeof(flash));
    reopen();
    assert(journal_append(&journal, 3000, 'P', "Celý řádek"));
    fail_write_after = 9;
    assert(!journal_append(&journal, 3001, 'P', "Tenhle se nedopíše celý"));
    fail_write_after = -1;
    reopen();
    assert(journal_append(&journal, 3002, 'P', "Po výpadku"));
    seen_t seen = {0};
    journal_each(&journal, collect, &seen);
    assert(seen.count == 3 && seen.when[0] == 3002 && seen.when[2] == 3000);
    assert(seen.when[1] == 3001 && strcmp(seen.texts[1], "Te") == 0);

    memset(flash + 3 * JOURNAL_SECTOR, 0x5A, JOURNAL_SECTOR);
    reopen();
    assert(journal_newest(&journal, 'P') == 3002);
    assert(journal_append(&journal, 3003, 'P', "Další"));
    assert(journal_newest(&journal, 0) == 3003);

    memset(flash, 0xFF, sizeof(flash));
    reopen();
    assert(journal_append(&journal, 4000, 'P', "x"));
    size_t tail = 2 * JOURNAL_SECTOR + 8 + strlen("4000 P x\n");
    memcpy(flash + tail, "4001 P bez konce", 16);
    reopen();
    assert(journal_append(&journal, 4002, 'P', "dalsi"));
    memset(&seen, 0, sizeof(seen));
    journal_each(&journal, collect, &seen);
    assert(seen.count == 3 && seen.when[0] == 4002 && seen.when[1] == 4001 && seen.when[2] == 4000);
}

static void test_wipe(void)
{
    assert(journal_notes_write(&journal, "- tajné\n"));
    assert(journal_wipe(&journal));
    char out[64];
    assert(journal_notes_read(&journal, out, sizeof(out)) == 0);
    assert(journal_count(&journal) == 0);
    reopen();
    assert(journal_notes_read(&journal, out, sizeof(out)) == 0);
    assert(journal_count(&journal) == 0);
    for (size_t i = 0; i < FLASH_SIZE; i++) {
        assert(flash[i] == 0xFF || (i >= 2 * JOURNAL_SECTOR && i < 2 * JOURNAL_SECTOR + 8));
    }
    assert(journal_append(&journal, 5000, 'P', "znovu"));
    assert(journal_newest(&journal, 'P') == 5000);
}

int main(void)
{
    test_fold();
    test_append_and_order();
    test_long_text_splits_on_character_boundary();
    test_wraps_and_drops_oldest();
    test_search();
    test_forget();
    test_notes();
    test_torn_writes();
    test_wipe();
    puts("journal: order, wrap, search, forget, notes, torn writes and wipe pass");
    return 0;
}
