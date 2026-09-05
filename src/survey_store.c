#include "survey_store.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app.h"
#include "band_plan.h"
#include "survey_suspect.h"

#define SURVEY_BANDWIDTH_DB 20.0f

const char *survey_flag_text(unsigned int flags, char *buffer, size_t size) {
    size_t used = 0;

    buffer[0] = '\0';
    if (flags & SURVEY_SUSPECT_REFERENCE)
        used += (size_t)snprintf(buffer + used, size - used, "reference");
    if (flags & SURVEY_SUSPECT_STEP_CENTRE)
        used += (size_t)snprintf(buffer + used, size - used, "%sstep-centre",
                                 used ? "," : "");
    if (flags & SURVEY_SUSPECT_UNRESOLVED)
        used += (size_t)snprintf(buffer + used, size - used, "%sunresolved",
                                 used ? "," : "");
    return used ? buffer : "-";
}

int survey_candidates_from(struct app *app, const struct survey_plan *plan,
                           const struct sdr_peak *peaks, int count,
                           const float *spectrum,
                           struct survey_candidate *out, int max) {
    int i, filled = 0;

    for (i = 0; i < count && filled < max; i++) {
        struct survey_candidate *c = &out[filled];
        struct sdr_carrier_report report;
        const struct band_plan_entry *entry;

        memset(c, 0, sizeof(*c));
        c->found_hz = survey_plan_bin_centre(plan, peaks[i].index);
        c->power_dbfs = peaks[i].power_dbfs;
        c->prominence_db = peaks[i].prominence_db;

        if (spectrum &&
            sdr_dsp_characterise_carrier(
                spectrum, SDR_DSP_FFT_SIZE, (double)app->applied_frequency,
                (double)app->applied_sample_rate, c->found_hz, 200000.0,
                SURVEY_BANDWIDTH_DB, app->magnitude_sorted, &report)) {
            c->measured = 1;
            c->centre_hz = report.centre_hz;
            c->width_hz = report.bandwidth_hz;
        }
        /*
         * The measurement is the better frequency, so the comb test is applied
         * to it -- but the candidate is still reported where the survey found
         * it. Several peaks inside one wide carrier all measure to the same
         * centre, and reporting that centre in place of each would hide the
         * fact that the peak finder returned several.
         */
        c->suspect = survey_suspect(plan,
                                    c->measured ? c->centre_hz : c->found_hz,
                                    c->measured ? c->width_hz : 0.0,
                                    (double)app->applied_sample_rate,
                                    SDR_DSP_FFT_SIZE, app->remove_dc);
        entry = band_plan_lookup(c->measured ? c->centre_hz : c->found_hz);
        c->allocation = entry ? entry->name : NULL;
        filled++;
    }
    return filled;
}

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

int survey_store_write(const struct app *app, const struct survey_plan *plan,
                       const struct survey_candidate *candidates, int count,
                       const struct survey_carrier *carriers,
                       int carrier_count,
                       const struct survey_confirm_target *targets,
                       int target_count, char *path_out, size_t path_size) {
    char name[64], path[256];
    time_t now = time(NULL);
    struct tm when;
    FILE *file;
    int i, suspicious = 0, confirmed = 0, refuted = 0;
    /* How far a reported frequency can sit from the truth: half a bin, which
       is the quantisation the sweep put on it. */
    double match_hz = plan ? plan->bin_hz / 2.0 : 0.0;

    if (!app || !plan)
        return -1;
    localtime_r(&now, &when);
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
    for (i = 0; i < count; i++)
        if (survey_suspect_warns(candidates[i].suspect))
            suspicious++;
    for (i = 0; i < target_count; i++) {
        if (targets[i].verdict == SURVEY_VERDICT_CONFIRMED)
            confirmed++;
        else if (targets[i].verdict == SURVEY_VERDICT_REFUTED)
            refuted++;
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
            app->survey.dwell_seconds);
    fprintf(file, "  \"receiver\": {\n");
    put_string(file, "    ", "antenna", app->config.antenna, ",");
    if (app->applied_gain_tenths > 0)
        fprintf(file, "    \"gain_db\": %.1f\n",
                (double)app->applied_gain_tenths / 10.0);
    else
        fprintf(file, "    \"gain_db\": null\n");
    fprintf(file, "  },\n");
    /* No site is written as null rather than an empty string: a reader must be
       able to tell "nobody said" from "somebody said nothing", because two
       sweeps with an empty label would compare as the same place. */
    if (app->config.site[0]) {
        fprintf(file, "  \"site\": {\n");
        put_string(file, "    ", "label", app->config.site, "");
        fprintf(file, "  },\n");
    } else {
        fprintf(file, "  \"site\": {},\n");
    }
    fprintf(file, "  \"totals\": {\"candidates\": %d, \"suspicious\": %d},\n",
            count, suspicious);
    /*
     * Whether anybody asked again, before the lists that answer to it. An
     * empty pass is written as asked 0 rather than left out: a reader has to
     * be able to tell "nothing held up" from "nothing was checked".
     */
    fprintf(file, "  \"confirmation\": {\"asked\": %d, \"confirmed\": %d, "
                  "\"refuted\": %d},\n", target_count, confirmed, refuted);
    fprintf(file, "  \"candidates\": [\n");
    for (i = 0; i < count; i++) {
        const struct survey_candidate *c = &candidates[i];
        char flags[64], escaped[256];
        const char *text = survey_flag_text(c->suspect, flags, sizeof(flags));

        fprintf(file, "   {\"hz\": %.0f, \"dbfs\": %.1f, "
                      "\"prominence_db\": %.1f, ", c->found_hz,
                (double)c->power_dbfs, (double)c->prominence_db);
        if (c->measured)
            fprintf(file, "\"centre_hz\": %.0f, \"width_hz\": %.0f, ",
                    c->centre_hz, c->width_hz);
        else
            fprintf(file, "\"centre_hz\": null, \"width_hz\": null, ");
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
        /*
         * A candidate takes the verdict of the carrier it belongs to. The
         * pass asks about carriers, and a carrier's shoulders are maxima of
         * the same signal a few bins away -- reading each of those as "never
         * asked" would leave most of a confirmed station's own list marked
         * unconfirmed.
         */
        {
            int holder = survey_carrier_holding(carriers, carrier_count,
                                                c->found_hz);
            double asked_at = holder >= 0 ? carriers[holder].centre_hz
                                          : c->found_hz;

            fprintf(file, "], \"confirmed\": \"%s\", \"allocation\": ",
                    survey_verdict_name(
                        survey_confirm_verdict_at(targets, target_count,
                                                  asked_at, match_hz)));
        }
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
                      "\"confirmed\": \"%s\", \"allocation\": ",
                c->centre_hz, c->power_centre_hz, c->lower_hz, c->upper_hz,
                c->width_hz, (double)c->peak_dbfs, (double)c->prominence_db,
                c->peaks,
                survey_verdict_name(
                    survey_confirm_verdict_at(targets, target_count,
                                              c->centre_hz, match_hz)));
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
