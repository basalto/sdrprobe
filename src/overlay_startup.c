#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "view.h"
#include "startup_layout.h"
#include "debug_log.h"
#include "device_profile.h"
#include "lte_dsp.h"

/*
 * The startup form: where this is, what it is listening with, and the
 * calibration running while the operator answers.
 *
 * ADR-0024. This file **draws and reads input**, and decides nothing: the
 * sequence is `startup_session.{c,h}`, which has never seen `struct app`, and
 * whether Continue may be pressed is `startup_session_may_commit()` rather
 * than a condition written out here. If anything in this file starts
 * computing a threshold, choosing a range or advancing a phase, that part
 * belongs in the machine with a name.
 *
 * It is modal, and that is not a preference: `start_calibration()` and this
 * machine both hold the tuner for the whole measurement, so every other
 * screen behind it would be drawing the calibration carrier rather than what
 * its own controls say.
 */

/* The three name fields, as one accessor so nothing indexes them by hand. */
struct startup_text {
    char *text;
    int *length;
    int capacity;
    const char *caption;
};

static struct startup_text startup_field_at(struct startup_view *s, int which) {
    struct startup_text f;

    memset(&f, 0, sizeof(f));
    switch (which) {
    case STARTUP_FIELD_SITE:
        f.text = s->site;
        f.length = &s->site_length;
        f.capacity = (int)sizeof(s->site);
        f.caption = "Site";
        break;
    case STARTUP_FIELD_ANTENNA:
        f.text = s->antenna;
        f.length = &s->antenna_length;
        f.capacity = (int)sizeof(s->antenna);
        f.caption = "Antenna";
        break;
    case STARTUP_FIELD_LABEL:
        f.text = s->label;
        f.length = &s->label_length;
        f.capacity = (int)sizeof(s->label);
        f.caption = "Receiver label";
        break;
    default:
        break;
    }
    return f;
}

/* Whether this receiver can be asked about GSM 900 at all. A tuner with a
   reach hole over the band would otherwise be sent to scan where it cannot
   hear and report an absence it never tested. */
static int startup_gsm_reachable(const struct app *app) {
    double lower = app->device.tune_lower_hz;
    double upper = app->device.tune_upper_hz;

    if (lower <= 0.0 || upper <= 0.0)
        return 0;
    return lower <= SCAN_BAND_LOWER_HZ && upper >= SCAN_BAND_UPPER_HZ;
}

static const struct lte_band *startup_band(const struct app *app) {
    int bands[LTE_BANDS_MAX];
    int count = view_lte_bands(app, bands);
    int at = app->startup.band;

    if (count <= 0)
        return NULL;
    if (at < 0 || at >= count)
        at = 0;
    return lte_band_for_number(bands[at]);
}

/* Start the machine and take the receiver for it. */
static void startup_begin_calibration(struct app *app) {
    struct startup_view *s = &app->startup;
    struct startup_session_event ev;

    if (!app->receiver_mode)
        return;
    if (receiver_borrow(app, &s->lease_token) < 0) {
        snprintf(s->session.status, sizeof(s->session.status),
                 "Could not take the receiver: %.100s", app->receiver_error);
        return;
    }
    if (startup_session_begin(&s->session,
                              (double)app->applied.sample_rate_hz,
                              app->applied.ppm, startup_gsm_reachable(app),
                              startup_band(app), monotonic_seconds(),
                              &ev) < 0) {
        /* Nothing to measure at all. The machine says so in words and the
           form is released at once rather than waiting for a budget. */
        receiver_lease_cancel(&app->lease, &s->lease_token);
        debug_log_write("startup", "no reference available: %.140s",
                        s->session.status);
        return;
    }
    debug_log_write("startup", "begin, %s",
                    startup_phase_name(s->session.phase));
    s->pending_hz = ev.retune_hz;
    s->pending_rate_hz = ev.retune_rate_hz;
    s->retune_pending = ev.retune_hz != 0;
}

void open_startup(struct app *app) {
    struct startup_view *s = &app->startup;

    startup_session_reset(&s->session);
    s->open = 1;
    s->focus = -1;
    s->site_menu_open = 0;
    s->antenna_menu_open = 0;
    s->band = 0;
    /* Pre-filled from what the last run left, which is the point: an operator
       who has not moved should not have to retype where they are, and one who
       has should see what they are changing from. */
    snprintf(s->site, sizeof(s->site), "%s", app->config.site);
    s->site_length = (int)strlen(s->site);
    snprintf(s->antenna, sizeof(s->antenna), "%s", app->config.antenna);
    s->antenna_length = (int)strlen(s->antenna);
    snprintf(s->label, sizeof(s->label), "%s",
             app->options.receiver_label ? app->options.receiver_label : "");
    s->label_length = (int)strlen(s->label);
    startup_begin_calibration(app);
}

/*
 * Commit: the fields, then the correction, then the receiver.
 *
 * The order matters and the middle step is why. A crystal does not depend on
 * where it was measured, so the scan may start while the operator is still
 * typing -- but a correction is filed by receiver **and** site (ADR-0018), so
 * filing waits for a site that has been committed, and it is filed against
 * whatever the form says now.
 */
static void startup_commit(struct app *app) {
    struct startup_view *s = &app->startup;
    int changed = 0;

    if (s->site[0] && strcmp(app->config.site, s->site)) {
        snprintf(app->config.site, sizeof(app->config.site), "%s", s->site);
        changed = 1;
    }
    if (s->antenna[0] && strcmp(app->config.antenna, s->antenna)) {
        snprintf(app->config.antenna, sizeof(app->config.antenna), "%s",
                 s->antenna);
        changed = 1;
    }
    if (app->config.site[0])
        changed |= config_remember_site(&app->config, app->config.site);
    if (app->config.antenna[0])
        changed |= config_remember_antenna(&app->config, app->config.antenna);
    installation_set_id(app->installation.site, app->config.site);
    installation_set_id(app->installation.antenna, app->config.antenna);
    /*
     * The label last, because it decides whether anything can be filed at
     * all: a receiver with no USB serial and no label has no identity, and a
     * correction that cannot say whose crystal it compensates is not
     * persisted (ADR-0018).
     */
    if (s->label[0])
        installation_identify(&app->installation, app->device.serial,
                              s->label);

    if (s->session.phase == STARTUP_LOCKED) {
        int was = app->applied.ppm;

        if (retune_receiver(app, app->applied.frequency_hz,
                            s->session.suggested_ppm) == 0) {
            app->options.ppm = s->session.suggested_ppm;
            debug_log_write("startup",
                            "apply %+d ppm (was %+d) source %s measurements %d",
                            s->session.suggested_ppm, was,
                            startup_session_source(&s->session) ==
                                    CALIBRATION_SOURCE_FCCH ? "fcch" : "lte",
                            s->session.track.measurements);
            if (installation_record_ppm(&app->installation,
                                        s->session.suggested_ppm) == 0)
                changed = 1;
            /*
             * The FCCH bookkeeping, and only for an FCCH.
             *
             * Missing this is silent for a whole session: `update_drift_check()`
             * returns immediately unless `gsm_valid`, so an LTE-backed
             * correction leaves the receiver never watching its own crystal
             * again -- and the dot stays an honest grey rather than claiming
             * a health nothing checked (ADR-0006).
             */
            if (startup_session_source(&s->session) ==
                CALIBRATION_SOURCE_FCCH) {
                app->cal.gsm_valid = 1;
                app->cal.gsm_arfcn = s->session.arfcn;
                app->cal.gsm_expected_hz = s->session.expected_hz;
                app->cal.gsm_tune_hz = s->session.expected_hz - 400000U;
                app->cal.gsm_ppm = app->applied.ppm;
                app->cal.drift_health = CAL_HEALTH_GOOD;
                app->cal.drift_ppm = 0.0;
                app->cal.drift_notice[0] = '\0';
                app->cal.drift_phase = DRIFT_IDLE;
                app->cal.drift_last_check_at = monotonic_seconds();
            } else {
                app->cal.lte_valid = 1;
                app->cal.lte_ppm = app->applied.ppm;
                app->cal.lte_earfcn = (int)s->session.earfcn;
            }
        }
    }
    if (changed)
        installation_commit(&app->installation, &app->config);
    debug_log_write("startup", "done, %s, site \"%s\", antenna \"%s\"",
                    startup_reason_name(s->session.reason), app->config.site,
                    app->config.antenna);
    s->open = 0;
    s->focus = -1;
}

/* Give the receiver back, whatever happened to the measurement. */
static void startup_release(struct app *app) {
    struct startup_view *s = &app->startup;

    if (receiver_lease_token_active(&s->lease_token))
        receiver_return(app, &s->lease_token);
    s->retune_pending = 0;
}

/*
 * One frame of the machine, and the adapter work around it.
 *
 * Called every frame with `have_block` as a **parameter rather than a guard**:
 * a scan step is over on its own clock, so skipping the tick on a frame with
 * no block costs a block a step -- which is the fault a 13-step sweep that
 * folded 39 blocks instead of 26 was made of.
 */
void update_startup(struct app *app, int have_block) {
    struct startup_view *s = &app->startup;
    struct startup_block block;
    struct startup_session_event ev;
    double now = monotonic_seconds();

    if (!s->open)
        return;

    /*
     * The tuning the machine asked for, obeyed here -- it never touches the
     * receiver itself. The settle starts when the tuner has actually moved,
     * which is what `startup_session_retuned()` is told and why a retune that
     * takes a tenth of a second does not silently cost a step its settle.
     */
    if (s->retune_pending) {
        int ok;

        s->retune_pending = 0;
        if (s->pending_rate_hz)
            ok = retune_receiver_at_rate(app, s->pending_hz,
                                         s->pending_rate_hz,
                                         app->applied.ppm) == 0;
        else
            ok = retune_receiver(app, s->pending_hz, app->applied.ppm) == 0;
        if (ok)
            startup_session_retuned(&s->session, monotonic_seconds());
        else
            startup_session_retune_failed(&s->session, s->pending_hz,
                                          app->receiver_error, &ev);
    }

    memset(&block, 0, sizeof(block));
    if (have_block) {
        block.i_samples = app->frame.i_samples;
        block.q_samples = app->frame.q_samples;
        block.pair_count = app->frame.pair_count;
        block.spectrum = app->frame.spectrum_average;
        block.centre_hz = (double)app->applied.frequency_hz;
        block.sample_rate = (double)app->applied.sample_rate_hz;
    }
    startup_session_tick(&s->session, &block,
                         have_block && app->frame.spectrum_ready, now, &ev);

    if (ev.scan_finished)
        debug_log_write("startup", "scan done, %s, arfcn %d, earfcn %u",
                        startup_phase_name(s->session.phase), s->session.arfcn,
                        s->session.earfcn);
    if (ev.measured)
        debug_log_write("startup",
                        "measure %d observed_ppm %.2f centre_ppm %.2f "
                        "sem_ppm %.2f spread_ppm %.2f source %s quality %.2f",
                        s->session.track.measurements,
                        s->session.expected_hz
                            ? s->session.offset_hz /
                                  (double)s->session.expected_hz * 1e6
                            : 0.0,
                        s->session.track.recent_center,
                        s->session.track.recent_sem,
                        s->session.track.recent_spread,
                        startup_session_source(&s->session) ==
                                CALIBRATION_SOURCE_FCCH ? "fcch" : "lte",
                        (double)s->session.quality);
    if (ev.finished)
        debug_log_write("startup",
                        "result %s reason %s measurements %d sem_ppm %.2f "
                        "suggested_ppm %d",
                        startup_phase_name(s->session.phase),
                        startup_reason_name(s->session.reason),
                        s->session.track.measurements,
                        s->session.track.recent_sem,
                        s->session.suggested_ppm);

    if (ev.retune_hz) {
        s->pending_hz = ev.retune_hz;
        s->pending_rate_hz = ev.retune_rate_hz;
        s->retune_pending = 1;
    }
    if (ev.release_receiver)
        startup_release(app);
}

/* -------------------------------------------------------------------------
 * Input
 * ---------------------------------------------------------------------- */

/* What the last gain change said, when it failed. One line, shown under the
   stepper -- a gain that silently did not take is a calibration measured at a
   level nobody chose. */
static char s_gain_error[96];

/* Which option holds this value, or -1. The profile turns a tuner's discrete
   steps and an AD9361's continuous range into one list, so this is a search
   over that list rather than arithmetic on a decibel. */
static int startup_gain_index(const struct app *app, int tenths) {
    int count = device_gain_option_count(&app->device);
    int i;

    for (i = 0; i < count; i++)
        if (device_gain_option_value(&app->device, i) == tenths)
            return i;
    return -1;
}

static void startup_gain_step(struct app *app, int by) {
    int count = device_gain_option_count(&app->device);
    int at;

    if (count <= 0 || !app->receiver_mode)
        return;
    at = startup_gain_index(app, app->applied_gain_tenths);
    if (at < 0)
        at = 0;
    at += by;
    if (at < 0)
        at = 0;
    if (at >= count)
        at = count - 1;
    /*
     * Gain alone, without the settings panel's stop/apply/read-back/roll-back
     * transaction: nothing here changes the frequency or the rate, which is
     * what that sequence exists to unwind. A refusal leaves the previous gain
     * in force and says so rather than recording one the device did not take.
     */
    if (device_set_gain(&app->source, 1,
                        device_gain_option_value(&app->device, at)) < 0) {
        snprintf(s_gain_error, sizeof(s_gain_error), "%s",
                 "The receiver would not take that gain");
        return;
    }
    app->applied_manual_gain = 1;
    app->applied_gain_tenths = device_gain_option_value(&app->device, at);
    signal_frame_invalidate(&app->frame);
    /*
     * A band scan's cell list is a function of the gain it ran at, so a cell
     * found at one gain and measured at another is two measurements wearing
     * one label. The form says it restarted rather than doing it quietly.
     */
    if (startup_session_running(&app->startup.session)) {
        debug_log_write("startup", "gain changed, calibration restarted");
        startup_release(app);
        startup_begin_calibration(app);
    }
}

void handle_startup_input(struct app *app) {
    struct startup_view *s = &app->startup;
    struct startup_layout l = startup_layout_now();
    int character;
    int i;

    /* A list that is down is the nearest thing to the surface, so Escape
       closes it before it does anything else. */
    if (IsKeyPressed(KEY_ESCAPE) &&
        (s->site_menu_open || s->antenna_menu_open)) {
        s->site_menu_open = 0;
        s->antenna_menu_open = 0;
        return;
    }
    if (IsKeyPressed(KEY_ESCAPE) && s->focus >= 0) {
        s->focus = -1;
        return;
    }

    for (i = 0; i < STARTUP_FIELD_COUNT; i++) {
        if (clicked(l.field[i])) {
            s->focus = i;
            s->site_menu_open = 0;
            s->antenna_menu_open = 0;
        }
    }
    if (clicked(l.field_menu[STARTUP_FIELD_SITE])) {
        s->site_menu_open = !s->site_menu_open && app->config.site_count > 0;
        s->antenna_menu_open = 0;
    }
    if (clicked(l.field_menu[STARTUP_FIELD_ANTENNA])) {
        s->antenna_menu_open = !s->antenna_menu_open &&
                               app->config.antenna_count > 0;
        s->site_menu_open = 0;
    }
    if (s->site_menu_open || s->antenna_menu_open) {
        int which = s->site_menu_open ? STARTUP_FIELD_SITE
                                      : STARTUP_FIELD_ANTENNA;
        int count = s->site_menu_open ? app->config.site_count
                                      : app->config.antenna_count;
        Rectangle field = l.field[which];
        int row;

        for (row = 0; row < count; row++) {
            Rectangle r = { field.x, field.y + field.height +
                                         (float)row * field.height,
                            field.width, field.height };
            if (clicked(r)) {
                struct startup_text f = startup_field_at(s, which);
                const char *pick = s->site_menu_open
                                       ? app->config.sites[row].label
                                       : app->config.antennas[row];
                snprintf(f.text, (size_t)f.capacity, "%s", pick);
                *f.length = (int)strlen(f.text);
                s->site_menu_open = 0;
                s->antenna_menu_open = 0;
                return;
            }
        }
    }

    if (clicked(l.gain_down))
        startup_gain_step(app, -1);
    if (clicked(l.gain_up))
        startup_gain_step(app, 1);
    {
        int bands[LTE_BANDS_MAX];
        int count = view_lte_bands(app, bands);

        if (count > 0 && clicked(l.band_down) && s->band > 0)
            s->band--;
        if (count > 0 && clicked(l.band_up) && s->band + 1 < count)
            s->band++;
    }

    while (s->focus >= 0 && (character = GetCharPressed()) != 0) {
        struct startup_text f = startup_field_at(s, s->focus);

        if (f.text && character >= ' ' && character < 127 &&
            *f.length < f.capacity - 1) {
            f.text[(*f.length)++] = (char)character;
            f.text[*f.length] = '\0';
        }
    }
    if (s->focus >= 0 && IsKeyPressed(KEY_BACKSPACE)) {
        struct startup_text f = startup_field_at(s, s->focus);

        if (f.text && *f.length > 0)
            f.text[--(*f.length)] = '\0';
    }

    if (clicked(l.skip) && startup_session_running(&s->session)) {
        struct startup_session_event ev;

        startup_session_skip(&s->session, &ev);
        debug_log_write("startup", "skipped by the operator");
        startup_release(app);
    }
    /*
     * Continue is the machine's decision, not this file's -- and it includes
     * a session that never began, which every skip condition leaves it in.
     */
    if (clicked(l.cont) && startup_session_may_commit(&s->session)) {
        startup_release(app);
        startup_commit(app);
    }
}

/* -------------------------------------------------------------------------
 * Drawing
 * ---------------------------------------------------------------------- */

static void startup_report_row(const struct startup_layout *l, int *row,
                               const char *label, const char *value) {
    float y;

    /* Past the capacity a row is not drawn at all: off the bottom edge is
       worse than absent, so a caller orders its rows and the ones that fit
       are the ones that matter (panel_rows.h). */
    if (*row >= l->report_rows.capacity)
        return;
    y = panel_row_y(&l->report_rows, *row);
    DrawText(label, (int)l->report_rows.label_x, (int)y, 15,
             (Color){ 150, 170, 184, 255 });
    DrawText(value, (int)l->report_rows.value_x, (int)y, 15,
             (Color){ 214, 226, 236, 255 });
    (*row)++;
}

void draw_startup(struct app *app) {
    struct startup_view *s = &app->startup;
    struct startup_layout l = startup_layout_now();
    char text[256];
    int row = 0;
    int i;

    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  (Color){ 8, 12, 18, 235 });
    DrawRectangleRec(l.panel, (Color){ 16, 23, 32, 255 });
    DrawRectangleLinesEx(l.panel, 1.0f, (Color){ 52, 68, 84, 255 });

    DrawText("Where is this receiver?", (int)l.panel.x + 24,
             (int)l.panel.y + 24, 22, (Color){ 226, 236, 244, 255 });
    DrawText("A survey's levels mean nothing without it, and a correction is "
             "filed by receiver and site.",
             (int)l.panel.x + 24, (int)l.panel.y + 52, 15,
             (Color){ 140, 158, 174, 255 });

    for (i = 0; i < STARTUP_FIELD_COUNT; i++) {
        struct startup_text f = startup_field_at(s, i);
        Rectangle box = l.field[i];

        DrawText(f.caption, (int)l.panel.x + 24,
                 (int)box.y + 8, 16, (Color){ 150, 170, 184, 255 });
        DrawRectangleRec(box, (Color){ 24, 33, 44, 255 });
        DrawRectangleLinesEx(box, 1.0f,
                             s->focus == i ? (Color){ 99, 228, 170, 255 }
                                           : (Color){ 60, 76, 92, 255 });
        DrawText(f.text, (int)box.x + 8, (int)box.y + 8, 16,
                 (Color){ 226, 236, 244, 255 });
        if (i == STARTUP_FIELD_SITE || i == STARTUP_FIELD_ANTENNA)
            draw_button(l.field_menu[i], "v", 0);
    }
    /* A receiver with no identity is told so rather than offered a claim it
       cannot make (ADR-0018). */
    if (!app->device.serial[0] && !s->label[0])
        DrawText("no USB serial: without a label, a correction cannot be "
                 "filed for this receiver",
                 (int)l.field[STARTUP_FIELD_LABEL].x,
                 (int)l.field[STARTUP_FIELD_LABEL].y +
                     (int)l.field[STARTUP_FIELD_LABEL].height + 2,
                 13, (Color){ 250, 190, 74, 255 });

    {
        char gain[32];

        device_gain_format(&app->device, app->applied_gain_tenths, gain,
                           sizeof(gain));
        DrawText("Gain", (int)l.panel.x + 24, (int)l.gain_value.y + 8, 16,
                 (Color){ 150, 170, 184, 255 });
        draw_button(l.gain_down, "-", 0);
        DrawRectangleRec(l.gain_value, (Color){ 24, 33, 44, 255 });
        DrawText(app->applied_manual_gain ? gain : "auto",
                 (int)l.gain_value.x + 8, (int)l.gain_value.y + 8, 16,
                 (Color){ 226, 236, 244, 255 });
        draw_button(l.gain_up, "+", 0);
    }
    {
        int bands[LTE_BANDS_MAX];
        int count = view_lte_bands(app, bands);

        DrawText("4G fall-back band", (int)l.panel.x + 24,
                 (int)l.band_value.y + 8, 16, (Color){ 150, 170, 184, 255 });
        if (count > 0) {
            draw_button(l.band_down, "-", 0);
            DrawRectangleRec(l.band_value, (Color){ 24, 33, 44, 255 });
            snprintf(text, sizeof(text), "band %d",
                     bands[s->band < count ? s->band : 0]);
            DrawText(text, (int)l.band_value.x + 8, (int)l.band_value.y + 8,
                     16, (Color){ 226, 236, 244, 255 });
            draw_button(l.band_up, "+", 0);
        } else {
            DrawText("none this tuner can reach", (int)l.band_value.x,
                     (int)l.band_value.y + 8, 16,
                     (Color){ 140, 158, 174, 255 });
        }
    }
    snprintf(text, sizeof(text), "%.3f MHz", app->applied.frequency_hz / 1e6);
    DrawText("Tuned to", (int)l.panel.x + 24, (int)l.frequency.y + 8, 16,
             (Color){ 150, 170, 184, 255 });
    DrawText(text, (int)l.frequency.x, (int)l.frequency.y + 8, 16,
             (Color){ 226, 236, 244, 255 });

    /*
     * The calibration's own panel. This is where it reports while the form is
     * up, because the form is modal and there is a whole screen to report
     * into; afterwards the verdict goes to the health indicator, where it
     * stands rather than expires (ADR-0024).
     */
    DrawRectangleRec(l.report, (Color){ 20, 28, 38, 255 });
    DrawRectangleLinesEx(l.report, 1.0f, (Color){ 52, 68, 84, 255 });
    DrawText("Calibration", (int)l.report.x + 12, (int)l.report.y + 6, 16,
             (Color){ 150, 170, 184, 255 });

    startup_report_row(&l, &row, "Stage",
                       startup_phase_name(s->session.phase));
    if (s->session.arfcn > 0) {
        /*
         * The BSIC is the part worth showing, not the tone's confidence: a
         * coherent line that is not an FCCH reads 0.99 just the same, while a
         * parity-valid synchronisation burst is something only a base station
         * could have said.
         */
        if (s->session.verified)
            snprintf(text, sizeof(text), "ARFCN %d, BSIC %d, FCCH %.2f",
                     s->session.arfcn, s->session.bsic,
                     (double)s->session.arfcn_confidence);
        else
            snprintf(text, sizeof(text), "ARFCN %d, FCCH %.2f, unverified",
                     s->session.arfcn, (double)s->session.arfcn_confidence);
        startup_report_row(&l, &row, "GSM reference", text);
    }
    if (s->session.earfcn > 0) {
        snprintf(text, sizeof(text), "EARFCN %u, cell %d, PSS %.2f",
                 s->session.earfcn, s->session.pci, (double)s->session.pss);
        startup_report_row(&l, &row, "4G reference", text);
    }
    if (s->session.track.measurements > 0) {
        snprintf(text, sizeof(text), "%d", s->session.track.measurements);
        startup_report_row(&l, &row, "Measurements", text);
        snprintf(text, sizeof(text), "%+.2f ppm, +/- %.2f",
                 s->session.track.recent_center, s->session.track.recent_sem);
        startup_report_row(&l, &row, "Residual", text);
        snprintf(text, sizeof(text), "%+d ppm", s->session.suggested_ppm);
        startup_report_row(&l, &row, "Correction", text);
    }
    if (s->session.rejected_arfcn > 0)
        startup_report_row(&l, &row, "Passed over", s->session.rejected_why);
    if (s->session.tone_moved_hz != 0.0) {
        snprintf(text, sizeof(text), "%+.0f Hz over %d kHz of tuning",
                 s->session.tone_moved_hz, STARTUP_TONE_SHIFT_HZ / 1000);
        startup_report_row(&l, &row, "Tone holds still", text);
    }
    if (s->session.references >= 2) {
        /* Both numbers, always -- on agreement it is the evidence the
           correction rests on, and on a refusal it is the whole finding. */
        snprintf(text, sizeof(text), "ARFCN %d %+d ppm, ARFCN %d %+d ppm",
                 s->session.first_arfcn, s->session.first_ppm,
                 s->session.second_arfcn, s->session.second_ppm);
        startup_report_row(&l, &row, "Cross-check", text);
    }
    if (s->session.reason != STARTUP_REASON_NONE)
        startup_report_row(&l, &row, "Verdict",
                           startup_reason_name(s->session.reason));

    DrawText(s->session.status, (int)l.status.x, (int)l.status.y, 15,
             (Color){ 190, 206, 220, 255 });

    draw_button_enabled(l.skip, "Skip calibration",
                        startup_session_running(&s->session));
    draw_button_enabled(l.cont, "Continue",
                        startup_session_may_commit(&s->session));

    /* The lists last, so they are drawn over the fields they drop from. */
    if (s->site_menu_open || s->antenna_menu_open) {
        int which = s->site_menu_open ? STARTUP_FIELD_SITE
                                      : STARTUP_FIELD_ANTENNA;
        int count = s->site_menu_open ? app->config.site_count
                                      : app->config.antenna_count;
        Rectangle field = l.field[which];
        int r;

        for (r = 0; r < count; r++) {
            Rectangle box = { field.x, field.y + field.height +
                                           (float)r * field.height,
                              field.width, field.height };
            const char *label = s->site_menu_open
                                    ? app->config.sites[r].label
                                    : app->config.antennas[r];

            DrawRectangleRec(box, (Color){ 28, 38, 50, 255 });
            DrawRectangleLinesEx(box, 1.0f, (Color){ 60, 76, 92, 255 });
            DrawText(label, (int)box.x + 8, (int)box.y + 8, 16,
                     (Color){ 214, 226, 236, 255 });
        }
    }
}
