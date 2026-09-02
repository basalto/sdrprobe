#include "site_history.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

double site_match_tolerance(double bin_hz) {
    double tolerance = 2.0 * bin_hz;
    return tolerance < SITE_MATCH_FLOOR_HZ ? SITE_MATCH_FLOOR_HZ : tolerance;
}

double site_match_tolerance_for(double bin_hz, double entry_bin_hz) {
    double coarser = bin_hz > entry_bin_hz ? bin_hz : entry_bin_hz;
    return site_match_tolerance(coarser);
}

void site_history_init(struct site_history *history, const char *site) {
    if (!history)
        return;
    memset(history, 0, sizeof(*history));
    if (site)
        snprintf(history->site, sizeof(history->site), "%s", site);
}

int site_history_parse(const char *text, struct site_history *history) {
    const char *line = text;

    if (!history)
        return 0;
    site_history_init(history, NULL);
    if (!text)
        return 0;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        char buffer[256];
        double hz, bin_hz;
        float dbfs, prominence;
        int sweeps, last, fields;

        if (length >= sizeof(buffer))
            length = sizeof(buffer) - 1;
        memcpy(buffer, line, length);
        buffer[length] = '\0';
        line = end ? end + 1 : line + strlen(line);

        if (buffer[0] == '#' || !buffer[0])
            continue;
        if (!strncmp(buffer, "site ", 5)) {
            snprintf(history->site, sizeof(history->site), "%.*s",
                     (int)sizeof(history->site) - 1, buffer + 5);
        } else if (!strncmp(buffer, "sweeps ", 7)) {
            history->sweeps = atoi(buffer + 7);
        } else if ((bin_hz = 0.0,
                    fields = sscanf(buffer, "seen %lf %f %f %d %d %lf", &hz,
                                    &dbfs, &prominence, &sweeps, &last,
                                    &bin_hz)) >= 5) {
            if (history->count < SITE_HISTORY_MAX) {
                struct site_entry *e = &history->entries[history->count++];
                e->hz = hz;
                e->dbfs = dbfs;
                e->prominence_db = prominence;
                e->sweeps = sweeps;
                e->last_sweep = last;
                /* A file written by hand, or by an older build, need not say
                   how precisely it measured. Nothing is a safer answer than a
                   guess: it falls back to the current sweep's own width. */
                e->bin_hz = fields >= 6 ? bin_hz : 0.0;
            }
        }
    }
    return history->count;
}

int site_history_format(const struct site_history *history, char *out,
                        size_t size) {
    int written, i;

    if (!history || !out)
        return -1;
    written = snprintf(out, size,
                       "# sdrprobe: what this site has heard. Derived from the\n"
                       "# saved sweeps; delete it and it rebuilds from the next.\n"
                       "site %s\nsweeps %d\n"
                       "# seen <hz> <dbfs> <prominence> <sweeps> <last_sweep> <bin_hz>\n",
                       history->site, history->sweeps);
    if (written < 0 || (size_t)written >= size)
        return -1;
    for (i = 0; i < history->count; i++) {
        const struct site_entry *e = &history->entries[i];
        int more = snprintf(out + written, size - (size_t)written,
                            "seen %.0f %.1f %.1f %d %d %.1f\n", e->hz,
                            (double)e->dbfs, (double)e->prominence_db,
                            e->sweeps, e->last_sweep, e->bin_hz);
        if (more < 0 || (size_t)(written + more) >= size)
            return -1;
        written += more;
    }
    return written;
}

const struct site_entry *site_history_find(const struct site_history *history,
                                           double hz, double bin_hz) {
    const struct site_entry *best = NULL;
    double nearest = -1.0;
    int i;

    if (!history)
        return NULL;
    for (i = 0; i < history->count; i++) {
        const struct site_entry *e = &history->entries[i];
        double tolerance = site_match_tolerance_for(bin_hz, e->bin_hz);
        double gap = fabs(e->hz - hz);
        if (gap <= tolerance && (nearest < 0.0 || gap < nearest)) {
            nearest = gap;
            best = e;
        }
    }
    return best;
}

enum site_status site_history_status(const struct site_history *history,
                                     double hz, double bin_hz) {
    if (!history || history->sweeps == 0)
        return SITE_STATUS_UNKNOWN;
    return site_history_find(history, hz, bin_hz) ? SITE_STATUS_KNOWN
                                                  : SITE_STATUS_NEW;
}

int site_history_merge(struct site_history *history, const double *hz,
                       const float *dbfs, const float *prominence, int count,
                       double bin_hz) {
    int i, added = 0;

    if (!history || !hz)
        return 0;
    history->sweeps++;
    for (i = 0; i < count; i++) {
        struct site_entry *match = NULL;
        double nearest = -1.0;
        int k;

        for (k = 0; k < history->count; k++) {
            struct site_entry *e = &history->entries[k];
            double tolerance = site_match_tolerance_for(bin_hz, e->bin_hz);
            double gap = fabs(e->hz - hz[i]);
            if (gap <= tolerance && (nearest < 0.0 || gap < nearest)) {
                nearest = gap;
                match = e;
            }
        }
        if (!match) {
            if (history->count >= SITE_HISTORY_MAX)
                continue;
            match = &history->entries[history->count++];
            memset(match, 0, sizeof(*match));
            match->hz = hz[i];
            added++;
        } else if (match->last_sweep == history->sweeps) {
            /* Two peaks of one wide carrier both matched the same entry. It
               has already been counted for this sweep; counting it twice
               would make it look more reliable than it is. */
            continue;
        }
        /* Keep the finer of the two placements: a narrow sweep knows better
           where a signal is than a wide one did. */
        if (match->bin_hz <= 0.0 || bin_hz <= match->bin_hz) {
            match->hz = hz[i];
            match->bin_hz = bin_hz;
        }
        if (dbfs)
            match->dbfs = dbfs[i];
        if (prominence)
            match->prominence_db = prominence[i];
        match->sweeps++;
        match->last_sweep = history->sweeps;
    }
    return added;
}

int site_history_missing(const struct site_history *history, const double *hz,
                         int count, double lower_hz, double upper_hz,
                         double bin_hz, const struct site_entry **out,
                         int max) {
    int i, found = 0;

    if (!history || !out)
        return 0;
    for (i = 0; i < history->count && found < max; i++) {
        const struct site_entry *e = &history->entries[i];
        int heard = 0, k;

        double tolerance = site_match_tolerance_for(bin_hz, e->bin_hz);
        if (e->hz < lower_hz || e->hz > upper_hz)
            continue;                      /* outside what this sweep looked at */
        for (k = 0; k < count; k++)
            if (fabs(hz[k] - e->hz) <= tolerance)
                heard = 1;
        if (!heard)
            out[found++] = e;
    }
    return found;
}

int site_history_path(const char *site, char *out, size_t size) {
    char safe[SITE_NAME_MAX];
    size_t i;
    int written;

    if (!site || !*site || !out)
        return -1;
    for (i = 0; i + 1 < sizeof(safe) && site[i]; i++) {
        char c = site[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_';
        safe[i] = ok ? c : '-';
    }
    safe[i] = '\0';
    if (!safe[0])
        return -1;
    written = snprintf(out, size, "surveys/history-%s.txt", safe);
    return (written < 0 || (size_t)written >= size) ? -1 : 0;
}

int site_history_load(const char *site, struct site_history *history) {
    char path[256], text[65536];
    FILE *file;
    size_t got;

    site_history_init(history, site);
    if (site_history_path(site, path, sizeof(path)) < 0)
        return -1;
    file = fopen(path, "rb");
    if (!file)
        return 1;
    got = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[got] = '\0';
    site_history_parse(text, history);
    /* The file's own site line is authoritative for what it holds, but the
       caller asked about this one; keep the caller's spelling. */
    snprintf(history->site, sizeof(history->site), "%s", site);
    return 0;
}

int site_history_save(const struct site_history *history) {
    char path[256];
    char *text;
    FILE *file;
    int length;

    if (!history || site_history_path(history->site, path, sizeof(path)) < 0)
        return -1;
    if (mkdir("surveys", 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "Could not create surveys/: %s\n", strerror(errno));
        return -1;
    }
    text = malloc(65536);
    if (!text)
        return -1;
    length = site_history_format(history, text, 65536);
    if (length < 0) {
        free(text);
        return -1;
    }
    file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "Could not write %s: %s\n", path, strerror(errno));
        free(text);
        return -1;
    }
    fwrite(text, 1, (size_t)length, file);
    fclose(file);
    free(text);
    return 0;
}
