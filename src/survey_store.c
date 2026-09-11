#include "survey_store.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "band_plan.h"
#include "survey_suspect.h"

/* Megahertz as the ingest script spells it: no trailing zeros, an M after. */
static int mhz_text(double hz, char *out, size_t size) {
    double value = hz / 1e6;
    int written;
    if (fabs(value - (double)(long)value) < 1e-9)
        written = snprintf(out, size, "%ldM", (long)value);
    else
        written = snprintf(out, size, "%gM", value);
    return (written < 0 || (size_t)written >= size) ? -1 : written;
}

int survey_store_filename(double lower_hz, double upper_hz,
                          const struct tm *when, char *out, size_t size) {
    char low[24], high[24];
    int written;

    if (!out || !when)
        return -1;
    if (mhz_text(lower_hz, low, sizeof(low)) < 0 ||
        mhz_text(upper_hz, high, sizeof(high)) < 0)
        return -1;
    written = snprintf(out, size, "%04d-%02d-%02d-%02d%02d%02d-%s-%s.json",
                       when->tm_year + 1900, when->tm_mon + 1, when->tm_mday,
                       when->tm_hour, when->tm_min, when->tm_sec, low, high);
    return (written < 0 || (size_t)written >= size) ? -1 : written;
}

int survey_json_escape(const char *in, char *out, size_t size) {
    size_t used = 0;

    if (!out || size == 0)
        return -1;
    out[0] = '\0';
    if (!in)
        return 0;
    for (; *in; in++) {
        const char *replacement = NULL;
        char escaped[8];
        size_t length;

        switch (*in) {
        case '"':  replacement = "\\\""; break;
        case '\\': replacement = "\\\\"; break;
        case '\n': replacement = "\\n"; break;
        case '\r': replacement = "\\r"; break;
        case '\t': replacement = "\\t"; break;
        default:
            if ((unsigned char)*in < 0x20) {
                snprintf(escaped, sizeof(escaped), "\\u%04x",
                         (unsigned char)*in);
                replacement = escaped;
            }
            break;
        }
        length = replacement ? strlen(replacement) : 1;
        if (used + length >= size)
            return -1;
        if (replacement)
            memcpy(out + used, replacement, length);
        else
            out[used] = *in;
        used += length;
    }
    out[used] = '\0';
    return (int)used;
}

/* The indent is a separate argument on purpose: folding it into the key is
   how "antenna" became "  antenna", a key no reader looks for and valid JSON
   besides, so nothing complained. */
static void put_string(FILE *file, const char *indent, const char *key,
                       const char *value, const char *tail) {
    char escaped[512];
    if (survey_json_escape(value, escaped, sizeof(escaped)) < 0)
        escaped[0] = '\0';
    fprintf(file, "%s\"%s\": \"%s\"%s\n", indent, key, escaped, tail);
}

int survey_store_write(const struct survey_record *record, char *path_out,
                       size_t path_size) {
    char name[64], path[256];
    const struct survey_plan *plan;
    const struct survey_candidate *candidates;
    const struct survey_carrier *carriers;
    const struct survey_confirm_target *targets;
    int count, carrier_count, target_count;
    struct tm when;
    FILE *file;
    int i;

    if (!record)
        return -1;
    plan = &record->plan;
    candidates = record->candidates;
    count = record->candidate_count;
    carriers = record->carriers;
    carrier_count = record->carrier_count;
    targets = record->targets;
    target_count = record->target_count;
    when = record->recorded_at;
    if (survey_store_filename(plan->lower_hz, plan->upper_hz, &when, name,
                              sizeof(name)) < 0)
        return -1;
    if (mkdir("surveys", 0755) < 0 && errno != EEXIST) {
        fprintf(stderr, "Could not create surveys/: %s\n", strerror(errno));
        return -1;
    }
    if ((size_t)snprintf(path, sizeof(path), "surveys/%s", name) >=
        sizeof(path))
        return -1;
    /*
     * Never over one already there. Seconds make a clash unlikely and this
     * makes it impossible, which is the guarantee the directory needs: a
     * sweep costs minutes of somebody's time and there is no getting it back.
     */
    {
        int attempt = 2;
        while (attempt < 100) {
            FILE *existing = fopen(path, "rb");
            if (!existing)
                break;
            fclose(existing);
            if ((size_t)snprintf(path, sizeof(path), "surveys/%.*s-%d.json",
                                 (int)(strlen(name) - 5), name, attempt) >=
                sizeof(path))
                return -1;
            attempt++;
        }
    }
    file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "Could not write %s: %s\n", path, strerror(errno));
        return -1;
    }
    fprintf(file, "{\n");
    fprintf(file, "  \"schema\": 1,\n");
    fprintf(file, "  \"recorded_at\": \"%04d-%02d-%02dT%02d:%02d:%02d\",\n",
            when.tm_year + 1900, when.tm_mon + 1, when.tm_mday, when.tm_hour,
            when.tm_min, when.tm_sec);
    fprintf(file, "  \"range_hz\": [%.0f, %.0f],\n", plan->lower_hz,
            plan->upper_hz);
    fprintf(file, "  \"sweep\": {\"steps\": %d, \"bins\": %d, "
                  "\"bin_hz\": %.1f, \"dwell_s\": %.3f},\n",
            plan->step_count, plan->bins, plan->bin_hz,
            record->dwell_seconds);
    /*
     * The receiving setup that produced this sweep, from the installation
     * rather than from the config file's spelling of it (ADR-0018, ADR-0022).
     * `id` is new: a sweep that cannot say which receiver took it cannot be
     * matched to a calibration or to a baseline, which is the whole argument
     * of both ADRs. Written as null rather than omitted when the receiver has
     * no identity, so a reader can tell "nobody said" from "not recorded".
     */
    fprintf(file, "  \"receiver\": {\n");
    if (record->setup.receiver[0])
        put_string(file, "    ", "id", record->setup.receiver, ",");
    else
        fprintf(file, "    \"id\": null,\n");
    put_string(file, "    ", "antenna", record->setup.antenna, ",");
    if (record->setup.gain_tenths > 0)
        fprintf(file, "    \"gain_db\": %.1f\n",
                (double)record->setup.gain_tenths / 10.0);
    else
        fprintf(file, "    \"gain_db\": null\n");
    fprintf(file, "  },\n");
    /* No site is written as null rather than an empty string: a reader must be
       able to tell "nobody said" from "somebody said nothing", because two
       sweeps with an empty label would compare as the same place. */
    if (record->setup.site[0]) {
        fprintf(file, "  \"site\": {\n");
        put_string(file, "    ", "label", record->setup.site, "");
        fprintf(file, "  },\n");
    } else {
        fprintf(file, "  \"site\": {},\n");
    }
    fprintf(file, "  \"totals\": {\"candidates\": %d, \"suspicious\": %d},\n",
            count, record->suspicious);
    /*
     * Whether anybody asked again, before the lists that answer to it. An
     * empty pass is written as asked 0 rather than left out: a reader has to
     * be able to tell "nothing held up" from "nothing was checked".
     */
    fprintf(file, "  \"confirmation\": {\"asked\": %d, \"confirmed\": %d, "
                  "\"intermittent\": %d, \"refuted\": %d, \"targets\": [",
            target_count, record->confirmed, record->intermittent, record->refuted);
    /*
     * And what the pass asked and found, one entry each.
     *
     * The counts above are a summary and this is the evidence: which
     * frequencies were asked about, what was claimed of each, how many looks
     * it was up in, and the width and the suspicion flags measured at the
     * pass's own resolution -- 244 Hz, where the sweep that raised the
     * candidate may have had 212 kHz bins.
     *
     * The ingest script has recorded this from the start and this writer did
     * not, so the same sweep saved two ways carried different evidence. The
     * kind sits on the carrier in both, because that is what `diff` compares;
     * this is the rest of the pass's answer.
     */
    for (i = 0; i < target_count; i++) {
        const struct survey_confirm_target *t = &targets[i];
        char flags[SURVEY_FLAG_TEXT_MAX];
        const char *text = survey_flag_text(t->suspicion, flags,
                                            sizeof(flags));

        fprintf(file, "%s\n    {\"hz\": %.0f, \"claim\": \"%s\", "
                      "\"verdict\": \"%s\", \"prominence_db\": %.1f, "
                      "\"hits\": %d, \"looks\": %d, \"width_hz\": %.0f, "
                      "\"flags\": ",
                i ? "," : "", t->hz,
                t->claim == SURVEY_CLAIM_MISSING ? "missing" : "new",
                survey_verdict_name(t->verdict), (double)t->prominence_db,
                t->hits, t->looks, t->bandwidth_hz);
        if (strcmp(text, "-") == 0)
            fprintf(file, "null");
        else
            fprintf(file, "\"%s\"", text);
        /* And the kind here too, not only on the carrier. A target the window
           revisits because the history remembers it may have no carrier in
           this sweep at all, and its kind would otherwise have nowhere to
           go. */
        if (t->kind_measured)
            fprintf(file,
                    ", \"kind\": {\"carrier\": \"%s\", "
                    "\"over_noise_db\": %.1f, \"standing_share\": %.3f, "
                    "\"envelope\": %.3f, \"bursts\": \"%s\", "
                    "\"occupancy\": %.4f}",
                    signal_verdict_name(signal_carrier_verdict(&t->carrier)),
                    t->carrier.carrier_over_noise_db,
                    t->carrier.carrier_power_fraction,
                    t->envelope.found ? t->envelope.variation : -1.0,
                    survey_burst_name(t->bursts.verdict),
                    t->bursts.occupancy);
        fprintf(file, "}");
    }
    fprintf(file, "%s]},\n", target_count ? "\n   " : "");
    fprintf(file, "  \"candidates\": [\n");
    for (i = 0; i < count; i++) {
        const struct survey_candidate *c = &candidates[i];
        char flags[SURVEY_FLAG_TEXT_MAX], escaped[256];
        const char *text = survey_flag_text(c->suspect, flags, sizeof(flags));

        fprintf(file, "   {\"hz\": %.0f, \"dbfs\": %.1f, "
                      "\"prominence_db\": %.1f, ", c->found_hz,
                (double)c->power_dbfs, (double)c->prominence_db);
        if (c->measured)
            fprintf(file, "\"centre_hz\": %.0f, \"width_hz\": %.0f, ",
                    c->centre_hz, c->width_hz);
        else
            fprintf(file, "\"centre_hz\": null, \"width_hz\": null, ");
        /*
         * The width in the sweep's own bins, and whether that is a
         * measurement or the instrument's floor.
         *
         * This writer did not record either and the ingest script did, so a
         * file written here and a file written there described the same sweep
         * differently -- and `report` said of a file from this writer that
         * the sweep "predates the extent being recorded", which was false and
         * blamed the sweep for the writer's omission. A reader cannot tell a
         * field a writer never wrote from one the data never had.
         */
        fprintf(file, "\"extent_hz\": %.0f, \"resolved\": %s, ",
                c->extent_hz,
                survey_extent_is_floor(c->extent_hz, plan->bin_hz) ? "false"
                                                                   : "true");
        fprintf(file, "\"flags\": [");
        if (strcmp(text, "-") != 0) {
            const char *from = text;
            int first = 1;
            while (*from) {
                const char *comma = strchr(from, ',');
                size_t length = comma ? (size_t)(comma - from) : strlen(from);
                fprintf(file, "%s\"%.*s\"", first ? "" : ", ", (int)length,
                        from);
                first = 0;
                from = comma ? comma + 1 : from + length;
            }
        }
        /* The verdict the record worked out. Why a candidate takes its
           carrier's is in survey_record.h; this only prints it. */
        fprintf(file, "], \"confirmed\": \"%s\", \"allocation\": ",
                survey_verdict_name(record->candidate_verdict[i]));
        if (c->allocation) {
            if (survey_json_escape(c->allocation, escaped, sizeof(escaped)) < 0)
                escaped[0] = '\0';
            fprintf(file, "\"%s\"", escaped);
        } else {
            fprintf(file, "null");
        }
        fprintf(file, "}%s\n", i + 1 < count ? "," : "");
    }
    fprintf(file, "  ],\n");
    /*
     * And the same peaks as signals. Both, not one: the candidates are what
     * was measured and the carriers are what it was concluded to mean, and a
     * reader comparing two sweeps wants the conclusion while a reader
     * doubting one wants the measurement.
     */
    fprintf(file, "  \"carriers\": [\n");
    for (i = 0; i < carrier_count; i++) {
        const struct survey_carrier *c = &carriers[i];
        const struct band_plan_entry *entry = band_plan_lookup(c->centre_hz);
        char escaped[256];

        fprintf(file, "   {\"centre_hz\": %.0f, \"power_centre_hz\": %.0f, "
                      "\"lower_hz\": %.0f, \"upper_hz\": %.0f, "
                      "\"width_hz\": %.0f, \"dbfs\": %.1f, "
                      "\"prominence_db\": %.1f, \"maxima\": %d, "
                      "\"confirmed\": \"%s\", ",
                c->centre_hz, c->power_centre_hz, c->lower_hz, c->upper_hz,
                c->width_hz, (double)c->peak_dbfs, (double)c->prominence_db,
                c->peaks,
                survey_verdict_name(record->carrier_verdict[i]));
        /*
         * And what kind of thing the pass found here, when it caught one.
         *
         * On the carrier rather than in a confirmation block, because the
         * carrier is what `diff` compares and what the history remembers; a
         * target is a thing somebody happened to ask about. Omitted entirely
         * when there is nothing to say -- writing zeros would be worse than
         * writing nothing, since a standing fraction of 0.000 reads as
         * "heavily modulated" and a burst count of zero as "continuous".
         */
        {
            const struct survey_confirm_target *kind =
                survey_record_carrier_kind(record, i);
            if (kind)
                fprintf(file,
                        "\"kind\": {\"carrier\": \"%s\", "
                        "\"over_noise_db\": %.1f, \"standing_share\": %.3f, "
                        "\"envelope\": %.3f, \"bursts\": \"%s\", "
                        "\"occupancy\": %.4f}, ",
                        signal_verdict_name(
                            signal_carrier_verdict(&kind->carrier)),
                        kind->carrier.carrier_over_noise_db,
                        kind->carrier.carrier_power_fraction,
                        kind->envelope.found ? kind->envelope.variation : -1.0,
                        survey_burst_name(kind->bursts.verdict),
                        kind->bursts.occupancy);
        }
        fprintf(file, "\"allocation\": ");
        if (entry) {
            if (survey_json_escape(entry->name, escaped, sizeof(escaped)) < 0)
                escaped[0] = '\0';
            fprintf(file, "\"%s\"", escaped);
        } else {
            fprintf(file, "null");
        }
        fprintf(file, "}%s\n", i + 1 < carrier_count ? "," : "");
    }
    fprintf(file, "  ]\n}\n");
    fclose(file);
    if (path_out)
        snprintf(path_out, path_size, "%s", path);
    return 0;
}
