#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "fm_view_model.h"
#include "app.h"

/*
 * The funnel in words, which is the one thing in this file that was a
 * *decision* taken inside a drawing rather than a field copied out of one.
 *
 * `draw_funnel_panel()` chose between these five sentences -- and between the
 * three emphases they are painted in -- from the counts beside them, which is
 * what ADR-0012 refuses: a function that draws may not also decide. It cost
 * nothing while the window was the only reader. It costs a contradiction the
 * moment a browser reads the same five counts and reaches its own conclusion
 * about them, which is the case this file exists for.
 *
 * The order is the diagnosis and is not free: each clause is only reached
 * once the one above it has been satisfied, so "blocks but no groups" cannot
 * be reported about a station that has no pilot to have produced blocks in
 * the first place.
 */
static void fm_view_model_reading(struct fm_view_model *out, int ps_valid) {
    const char *text;
    enum fm_reading_tone tone;

    if (!out->pilot_locked) {
        text = "no pilot: not an FM station, or not tuned to one";
        tone = FM_READING_WEAK;
    } else if (out->blocks_matched == 0) {
        text = "a pilot but no blocks: this station carries no RDS";
        tone = FM_READING_WEAK;
    } else if (out->groups == 0) {
        text = "blocks but no groups: too weak to hold sync";
        tone = FM_READING_WEAK;
    } else if (!ps_valid) {
        text = "groups arriving; the name needs all four segments";
        tone = FM_READING_NEUTRAL;
    } else {
        text = "reading the station";
        tone = FM_READING_GOOD;
    }
    snprintf(out->reading, sizeof(out->reading), "%s", text);
    out->reading_tone = tone;
}

void fm_view_model_build(const struct app *app, struct fm_view_model *out) {
    const struct fm_view *fm = &app->fm;
    const struct rds_station *s = &fm->session.station;
    const char *pty_name;

    memset(out, 0, sizeof(*out));

    out->pilot_locked = fm_pilot_locked(&fm->session.front.pilot);
    out->pilot_coherence = fm_pilot_coherence(&fm->session.front.pilot);
    /* Left at zero without a lock rather than reported: an unlocked loop
       still returns a frequency and a ppm, and both are about whatever scrap
       of noise it settled on. The window only draws these two rows when it
       is locked, for the same reason. */
    if (out->pilot_locked) {
        out->pilot_hz = fm_pilot_hz(&fm->session.front.pilot);
        out->pilot_ppm = fm_pilot_ppm(&fm->session.front.pilot);
        out->timing_offset = fm->session.timing_offset;
        out->timing_samples_per_symbol = FM_RDS_SAMPLES_PER_SYMBOL;
        out->axis_radians = fm->session.axis_radians;
    }
    out->broadcast_stereo = fm_audio_is_stereo(&fm->audio);

    out->playing = fm->playing;
    if (fm->playing)
        out->audio_rate_hz = fm_audio_rate(&fm->audio);
    snprintf(out->audio_error, sizeof(out->audio_error), "%s", fm->audio_error);

    out->pi_valid = s->pi_valid;
    out->pi = s->pi;
    out->pi_repeats = s->pi_repeats;

    out->ps_valid = s->ps_valid;
    snprintf(out->ps, sizeof(out->ps), "%s", s->ps);
    /* How many of the four have arrived, not which -- the count is what both
       readers show, and the mask is the decoder's own bookkeeping. */
    out->ps_segments = __builtin_popcount((unsigned)s->ps_segments);

    out->pty_valid = s->pty_valid;
    if (s->pty_valid) {
        out->pty = s->pty;
        pty_name = rds_pty_name(s->pty);
        snprintf(out->pty_name, sizeof(out->pty_name), "%s",
                 pty_name ? pty_name : "?");
        out->tp = s->tp;
        out->ta = s->ta;
        snprintf(out->traffic, sizeof(out->traffic), "%s",
                 rds_traffic_name(s->tp, s->ta));
    }

    out->rt_valid = s->rt_valid;
    if (s->rt_valid)
        snprintf(out->rt, sizeof(out->rt), "%s", s->rt);

    out->bits = s->funnel.bits;
    out->blocks_matched = s->funnel.blocks_matched;
    out->groups = s->funnel.groups;
    out->identified = s->funnel.identified;
    out->named = s->funnel.named;
    fm_view_model_reading(out, s->ps_valid);

    out->spectrum_bins = (int)fm->spectrum_bins;
    if (out->spectrum_bins > FM_VIEW_MODEL_MAX_BINS)
        out->spectrum_bins = FM_VIEW_MODEL_MAX_BINS;
    if (out->spectrum_bins > 0) {
        out->spectrum_bin_hz = fm->spectrum_bin_hz;
        memcpy(out->spectrum, fm->spectrum,
               (size_t)out->spectrum_bins * sizeof(*out->spectrum));
    }
}
