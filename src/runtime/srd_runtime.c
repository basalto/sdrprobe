/*
 * SRD's runtime: the tuning inside the 430-440 MHz allocation, entering
 * and leaving the view, one block of OOK/Manchester, and remembering what
 * the frames and the undecoded detections said.
 *
 * Out of `view_srd.c` by `.scratch/layer-boundaries/issues/02-*`. The
 * drawing and the input stayed behind. No raylib here.
 */

#include <sys/stat.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "runtime/app.h"
#include "runtime/debug_log.h"
#include "tech/srd_log.h"
#include "tech/srd_record.h"
#include "runtime/runtime.h"

int srd_tuned(const struct app *app) {
    return srd_receiver_ready(app->applied.frequency_hz,
                              app->applied.sample_rate_hz);
}

void view_srd_defaults(struct app *app) {
    if (!app)
        return;
    memset(&app->srd, 0, sizeof(app->srd));
    app->srd.polarity = SRD_MANCHESTER_THOMAS;
    app->srd.selected_log = -1;
    snprintf(app->srd.record_seconds, sizeof(app->srd.record_seconds), "%.0f",
             SRD_RECORD_SECONDS_DEFAULT);
    app->srd.record_seconds_length = (int)strlen(app->srd.record_seconds);
    srd_session_reset(&app->srd.session);
}

/*
 * Entering the SRD view retunes the receiver to 434 MHz, the same shape as
 * enter_gsm()/enter_lte(): a "Retune to 434 MHz" button asks an operator to
 * land a click on it, and the reason this view exists is to be listening at
 * the SRD band the moment somebody presses a SRD remote control, not after they notice
 * the header still reads the previous band and reach for the mouse. Only
 * when it is not already there -- arriving with the receiver already parked
 * in the band (a capture, or a previous session) must not retune it away
 * from wherever it already was set, the same guard park_in_band() makes for
 * LTE.
 *
 * The manual button stays for the cases this cannot reach: file playback,
 * where there is one tuning and nothing to move (srd_tuned() then reports
 * the mismatch as informational text instead), and a retune that fails --
 * the button offers a retry with app->receiver_error already in place.
 */
void enter_srd(struct app *app) {
    if (!app->receiver_mode)
        return;
    if (srd_tuned(app)) {
        receiver_borrow(app, &app->srd.lease_token);
        return;
    }
    receiver_borrow_at(app, &app->srd.lease_token, SRD_CENTER_HZ, 0);
}

void leave_srd(struct app *app) {
    receiver_return(app, &app->srd.lease_token);
}

static void remember_frame(struct app *app, double now, const struct srd_frame *f,
                           double carrier_hz, double chip_us) {
    struct srd_view *s = &app->srd;

    for (int i = SRD_LOG_CAPACITY - 1; i > 0; i--)
        s->log[i] = s->log[i - 1];

    s->log[0].at = now;
    s->log[0].kind = f->kind;
    s->log[0].modulation = f->modulation;
    s->log[0].byte_count = f->byte_count;
    s->log[0].bit_count = f->bit_count;
    s->log[0].carrier_hz = carrier_hz;
    s->log[0].absolute_hz = srd_log_absolute_hz(app->applied.frequency_hz,
                                                carrier_hz);
    s->log[0].chip_us = chip_us;
    s->log[0].error_count = f->error_count;
    size_t to_copy = f->byte_count;
    if (to_copy > sizeof(s->log[0].bytes))
        to_copy = sizeof(s->log[0].bytes);
    memcpy(s->log[0].bytes, f->bytes, to_copy);

    if (s->log_count < SRD_LOG_CAPACITY)
        s->log_count++;

    debug_log_write("srd", "%s %s %zu bytes, %.1f us",
                    f->modulation == SRD_MOD_FSK2 ? "2FSK" : "OOK",
                    f->kind == SRD_FRAME_FULL ? "FULL" :
                    f->kind == SRD_FRAME_REPEAT ? "REPEAT" : "GENERIC",
                    f->byte_count, chip_us);
}

static void remember_undecoded_detection(struct app *app, double now,
                                         double carrier_hz, double chip_us,
                                         size_t chip_count, const uint8_t *raw_chips,
                                         enum srd_modulation mod,
                                         enum srd_frame_kind kind) {
    struct srd_view *s = &app->srd;

    for (int i = SRD_LOG_CAPACITY - 1; i > 0; i--)
        s->log[i] = s->log[i - 1];

    s->log[0].at = now;
    /* The session decided this, so the headless report cannot disagree. */
    s->log[0].kind = kind;
    s->log[0].modulation = mod;
    s->log[0].byte_count = 0;
    s->log[0].bit_count = chip_count;
    s->log[0].carrier_hz = carrier_hz;
    s->log[0].absolute_hz = srd_log_absolute_hz(app->applied.frequency_hz,
                                                carrier_hz);
    s->log[0].chip_us = chip_us;
    s->log[0].error_count = 0;
    memset(s->log[0].bytes, 0, sizeof(s->log[0].bytes));

    if (raw_chips && chip_count > 0) {
        size_t max_chips = chip_count > 256 ? 256 : chip_count;
        s->log[0].byte_count = srd_pack_bits(raw_chips, max_chips,
                                            s->log[0].bytes,
                                            sizeof(s->log[0].bytes));
    }

    if (s->log_count < SRD_LOG_CAPACITY)
        s->log_count++;

    debug_log_write("srd-undecoded", "%s burst %zu chips (%.1f us)",
                    mod == SRD_MOD_FSK2 ? "2FSK" : "OOK", chip_count, chip_us);
}

void update_srd(struct app *app, double now) {
    struct srd_view *s = &app->srd;
    struct srd_session_event event;
    double sample_rate = (double)app->applied.sample_rate_hz;
    float full_scale = app->device.full_scale > 0.0f ? app->device.full_scale : 127.5f;

    srd_session_feed(&s->session, app->frame.i_samples, app->frame.q_samples,
                     app->frame.pair_count, sample_rate, full_scale,
                     s->polarity, now, &event);

    for (int i = 0; i < event.frame_count; i++) {
        struct srd_session_frame_event *fe = &event.frames[i];
        remember_frame(app, fe->at, &fe->frame, fe->carrier_hz, fe->chip_us);
    }

    for (int i = 0; i < event.undecoded_count; i++) {
        struct srd_session_undecoded_event *ue = &event.undecoded[i];
        remember_undecoded_detection(app, ue->at, ue->carrier_hz, ue->chip_us,
                                     ue->chip_count, ue->raw_chips,
                                     ue->modulation, ue->kind);
        if (s->auto_save_staging && (now - s->last_auto_save_time > 1.0)) {
            mkdir("captures", 0755);
            mkdir("captures/staging", 0755);
            time_t raw_t = time(NULL);
            struct tm local_t;
            localtime_r(&raw_t, &local_t);
            char stamp[32];
            strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local_t);
            char staging_path[256];
            snprintf(staging_path, sizeof(staging_path),
                     "captures/staging/undecoded_%s.bin", stamp);
            double age = (double)(app->frame.pair_count - ue->offset_pairs) / sample_rate;
            if (age < 0.0)
                age = 0.0;
            double dur = (double)ue->pair_count / sample_rate + 0.100;
            iq_ring_save_slice(&app->acq.ring, age, dur, staging_path, "srd_undecoded");
            s->last_auto_save_time = now;
        }
    }
}

void srd_freq_show(struct app *app) {
    struct srd_view *s = &app->srd;

    if (s->freq_typing)
        return;
    snprintf(s->freq_text, sizeof(s->freq_text), "%.4f",
             (double)app->applied.frequency_hz / 1e6);
}

/* Returns 1 when the receiver moved. */
int srd_freq_commit(struct app *app) {
    struct srd_view *s = &app->srd;
    double mhz = atof(s->freq_text);
    int moved = 0;

    s->freq_typing = 0;
    if (mhz > 0.0 && app->receiver_mode) {
        double hz = mhz * 1e6;

        /*
         * Typed, so it is not clamped to the allocation the way an arrow
         * press is -- a reader who types 868.2 has said something
         * deliberate, and the view's own status line already says when the
         * receiver is somewhere this decoder cannot help with. What it is
         * clamped to is what the tuner can reach, which retune_receiver()
         * reports on rather than guessing about.
         */
        if (hz > 0.0 && hz < 4e9 &&
            retune_receiver(app, (uint32_t)llround(hz), app->applied.ppm) == 0)
            moved = 1;
    }
    srd_freq_show(app);
    return moved;
}

void srd_tune_arrow(struct app *app, int direction) {
    uint32_t target;

    if (!app->receiver_mode)
        return;
    target = srd_tune_step(app->applied.frequency_hz,
                           app->applied.sample_rate_hz, direction);
    if (target != app->applied.frequency_hz)
        retune_receiver(app, target, app->applied.ppm);
    srd_freq_show(app);
}
