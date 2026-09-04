#ifndef VIEW_H
#define VIEW_H

#include <raylib.h>

#include "app.h"

/*
 * The Decode tab's screens, one file each, plus the few helpers they share
 * with the rest of the application.
 *
 * A view is state-to-pixels and clicks-to-state: it reads `struct app`, draws,
 * and handles its own input. It owns no state of its own, which is the honest
 * limit of this split -- see the note in app.h.
 */

/* Shared widgets and actions, defined in sdrprobe.c. */
int clicked(Rectangle rectangle);
void draw_button(Rectangle rectangle, const char *label, int primary);
void draw_button_enabled(Rectangle rectangle, const char *label, int enabled);
/* Stop measuring and hand the receiver back, staying on the screen. Returns
   negative when the retune failed, in which case nothing changed. */
int calibration_stop_measuring(struct app *app);
int retune_receiver(struct app *app, uint32_t frequency, int ppm);
/* The same, changing the sample rate with the tuning. Only LTE needs it. */
int retune_receiver_at_rate(struct app *app, uint32_t frequency,
                            uint32_t sample_rate, int ppm);
int process_block(struct app *app, double now);
double monotonic_seconds(void);
int stop_requested(void);

/* The band survey with no window: sweep, then print the candidates to stdout,
   one per line. src/survey_report.c. */
int survey_report_run(struct app *app);

/* Read the broadcast block that follows this SCH burst, if this is the SCH a
   block follows. Returns 1 when a System Information message came out of it.
   src/view_gsm.c. */
int gsm_read_broadcast(struct app *app, const struct gsm_sch_result *sch,
                       struct gsm_si *si);
void set_tab(struct app *app, int new_tab);
void set_decode(struct app *app, int kind);
void adjust_waterfall_scale(struct app *app, int zoom_in);
int scan_strongest_arfcn(const struct app *app);
int scan_strongest_bcch(const struct app *app);
int start_scan(struct app *app);
/* Start a timestamped capture in captures/, with the sidecar describing the
   tuning it was taken at. `basename` names the file, `technology` goes in the
   sidecar, and the GSM fields are 0 for a technology that has no channel.
   Shared because recording is not a property of either decode view. */
int start_capture_record(struct app *app, const char *basename,
                         const char *technology, int arfcn,
                         double carrier_offset_hz, double seconds);
int compare_double(const void *left, const void *right);

/* GSM band-analysis view. */
void draw_gsm(struct app *app);
void handle_gsm_input(struct app *app);
void update_gsm_sch(struct app *app, double now);
void enter_gsm(struct app *app);
void leave_gsm(struct app *app);
void view_gsm_defaults(struct app *app);
void start_record(struct app *app);
void gsm_tune_selected(struct app *app, int arfcn);
Rectangle gsm_scan_rect(void);
Rectangle gsm_waterfall_rect(void);
Rectangle gsm_burst_rect(void);

/* LTE cell-search and broadcast view. */
void draw_fm(struct app *app);
void handle_fm_input(struct app *app);
void update_fm(struct app *app, double now);
void view_fm_defaults(struct app *app);
/* Retune and start over: everything in the view belongs to one carrier. */
void fm_tune(struct app *app, double hz);
/* Whether the FM view's frequency field has focus. */
int fm_editing(const struct app *app);
/* Walking band II: a coarse sweep, then the carriers it found. */
void fm_scan_begin(struct app *app);
void fm_scan_stop(struct app *app);
void update_fm_scan(struct app *app, double now, int have_block);
int fm_scan_showing(const struct app *app);
/* Put the receiver in band II when the view is opened. */
void enter_fm(struct app *app);
/* Start or stop the sound. The device opens on the first press. */
void fm_play(struct app *app);
void update_fm_audio(struct app *app);
void fm_audio_close(struct app *app);

void draw_lte(struct app *app);
void handle_lte_input(struct app *app);
void update_lte(struct app *app, double now);
void view_lte_defaults(struct app *app);
/* The LTE view borrows the receiver: it needs 1.92 MS/s and a carrier centre,
   and gives both back on the way out. */
void enter_lte(struct app *app);
void leave_lte(struct app *app);
/* The band scan. Driven every frame, not only when a block arrives, because
   most of its time is spent waiting for the tuner. */
void update_lte_scan(struct app *app, double now, int have_block);
/* Start one, by band number rather than by button. Returns 0 when it began.
   Shared with the headless scan, which is the only way to see what a scan
   found without a window and somebody to click it (ADR-0012). */
int lte_scan_begin(struct app *app, int band_number, double now);
int lte_scan_running(const struct app *app);
/* Whether the receiver is on LTE's 1.92 MS/s grid, which is the one thing
   that has to be true before any of it works (ADR-0014). */
int lte_on_grid(const struct app *app);

/* Mode S / ADS-B view. */
void draw_adsb(struct app *app);
void handle_adsb_input(struct app *app);
void update_adsb(struct app *app, double now);
int adsb_tuned(const struct app *app);


/* Scope tab: the four signal views, and the GPU resources two of them keep
   between frames. render_waterfall and update_scatter are here because the
   frame loop drives them; waterfall_color and view_name stay private. */
Rectangle calculate_plot(void);
/* The frequency window the Scope's spectrum and waterfall share. */
void scope_freq_sync(struct app *app);
void scope_freq_range(const struct app *app, double *lower, double *upper);
int scope_freq_input(struct app *app, Rectangle plot);
void scope_freq_reset(struct app *app);
/* How far Left and Right move it, and the narrowest it may become. */
#define SCOPE_FREQ_PAN 0.20
#define SCOPE_FREQ_MIN_SPAN_HZ 20000.0
void clear_scatter(struct app *app);
int recreate_scatter(struct app *app, Rectangle plot);
int recreate_waterfall(struct app *app, Rectangle plot, int clear_history);
void render_waterfall(struct app *app);
void update_waterfall(struct app *app);
void update_scatter(struct app *app, double now, int insert);
void draw_waterfall_rect(const struct app *app, int calibration_mode,
                         Rectangle rect, double zoom_center_hz);
void draw_waterfall(const struct app *app);
void draw_base_hud(const struct app *app, const struct slot_snapshot *snapshot);
void draw_magnitude(const struct app *app);
void draw_spectrum(const struct app *app);
void draw_scatter(const struct app *app);
void view_scope_defaults(struct app *app);
int view_scope_resize_if_needed(struct app *app, Rectangle plot);
void view_scope_release(struct app *app);
void recompute_magnitude_bins(struct app *app);
void decay_spectrum_peak(struct app *app, double now);
void adjust_active_scale(struct app *app, int zoom_in);


/* Band survey (Scope view 5): sweep a range, find what stands above the local
   floor, and measure whichever candidate is selected. */
void view_survey_defaults(struct app *app);
void view_survey_enter(struct app *app);
/* Point the range fields at the nth offerable band, 1-based. */
int survey_choose_band(struct app *app, int nth);
void view_survey_leave(struct app *app);
/* `spectrum_updated` says a fresh averaged spectrum arrived this frame.
   The confirmation pass counts blocks, not frames: counting frames gave it
   six looks in a tenth of a second and it measured a spectrum from before
   the receiver had retuned. */
void update_survey(struct app *app, double now, int spectrum_updated);
void handle_survey_input(struct app *app);
void draw_survey(struct app *app);
/* True while a range field is taking typed input, so the frame loop leaves the
   number keys and Esc to the field rather than switching views or quitting. */
int survey_editing(const struct app *app);

/* Help overlay: what each chart plots and how to read it. Orthogonal to the
   tabs like calibration is, reachable with `h` from every view. */
void open_help(struct app *app);
void close_help(struct app *app);
void handle_help_input(struct app *app);
void draw_help(const struct app *app);

/* Calibration overlay: the GSM 900 channel calibration, its band scan, and the
   periodic drift re-check. Drawn over whichever tab is active. */
void open_calibration(struct app *app);
/* Choose 2G, 4G or 5G, with the channel default and the instruction that
   goes with it. Shared by the buttons and by opening already on one. */
void calibration_select_technology(struct app *app, int technology);
void close_calibration(struct app *app);
void update_calibration_measurement(struct app *app);
void handle_calibration_input(struct app *app);
void draw_calibration(struct app *app);
void update_scan(struct app *app);
void draw_scan(struct app *app);
void calibration_select_channel(struct app *app, int arfcn);
int start_calibration(struct app *app);

void handle_scan_input(struct app *app);
void update_drift_check(struct app *app, int have_block);
void draw_health_indicator(const struct app *app);


/* Settings panel, and the two buttons that open it and the calibration
   overlay. */
void open_settings(struct app *app);
int apply_settings(struct app *app);
void handle_settings_input(struct app *app);
void draw_settings(const struct app *app);
Rectangle settings_button(void);
Rectangle calibration_button(void);


/* Acquisition lifecycle, in sdrprobe.c: applying settings can retune or
   restart the receiver. */
int stop_acquisition(struct app *app);
int start_acquisition(struct app *app);
int set_frequency_correction(rtlsdr_dev_t *dev, int ppm);

#endif
