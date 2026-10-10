#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "agenda.h"

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: agenda_cli file.ics window_start window_end [chunk]\n");
        return 2;
    }
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    time_t from = (time_t) atoll(argv[2]);
    time_t to = (time_t) atoll(argv[3]);
    size_t chunk = argc > 4 ? (size_t) atoi(argv[4]) : 4096;
    FILE *file = fopen(argv[1], "rb");
    if (!file || chunk == 0) {
        return 2;
    }
    agenda_parser_t *parser = malloc(agenda_parser_size());
    agenda_parser_begin(parser, from, to);
    char *buffer = malloc(chunk);
    size_t got;
    while ((got = fread(buffer, 1, chunk, file)) > 0) {
        agenda_parser_feed(parser, buffer, got);
    }
    fclose(file);
    static agenda_event_t events[128];
    int count = agenda_parser_finish(parser, events, 128);
    for (int i = 0; i < count; i++) {
        printf("%lld\t%lld\t%d\t%s\t%s\n", (long long) events[i].start, (long long) events[i].end,
               events[i].all_day ? 1 : 0, events[i].title, events[i].place);
    }
    if (argc > 5) {
        char *described = agenda_describe(events, count, from, to);
        char summary[600];
        agenda_summary(events, count, from, to, summary, sizeof(summary));
        fprintf(stderr, "%s\n%s\n", described, summary);
        free(described);
    }
    free(buffer);
    free(parser);
    return 0;
}
