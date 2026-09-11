#ifndef VIEW_H
#define VIEW_H

#include <raylib.h>

#include "app.h"
#include "survey_record.h"

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

/*
 * Borrowing the receiver's tuning, in the order it was borrowed.
 *
 * These are the only way a screen should take the receiver somewhere it will
 * later come back from; `receiver_lease.h` is the rule they enforce and says
 * why. All of them are no-ops that report success in file mode, because a
 * capture holds one tuning and nothing can move it -- so a caller does not
 * have to guard each one with `app->receiver_mode`.
 */

/* Take the receiver where it stands, without moving it. For an owner that
   tunes later, or several times, or not at all. */
int receiver_borrow(struct app *app, struct receiver_lease_token *token);
/* Take it and move it in one step: on a refusal the token is cancelled, since
   retune_receiver*() has already put the hardware back. `sample_rate` of 0
   means "leave the rate alone". */
int receiver_borrow_at(struct app *app, struct receiver_lease_token *token,
                       uint32_t frequency, uint32_t sample_rate);
/* Back to where this owner started, still holding it. The survey between
   sweeps: it has finished walking the band but still owns the right to sweep
   again. */
int receiver_restore_held(struct app *app,
                          const struct receiver_lease_token *token);
/* Give it back. Restores with the *current* PPM, so a calibration applied
   while borrowed survives the return. A failed retune leaves the token live
   and retryable. */
int receiver_return(struct app *app, struct receiver_lease_token *token);
/* Give up the claim and keep the tuning: the survey's "Open waterfall". */
int receiver_commit(struct app *app, struct receiver_lease_token *token);
int process_block(struct app *app, double now);
double monotonic_seconds(void);
int stop_requested(void);

/* The band survey with no window: sweep, then print the candidates to stdout,
   one per line. src/survey_report.c. */
int survey_report_run(struct app *app);

/* Read the broadcast block that follows this SCH burst, if this is the SCH a
   block follows. Returns 1 when a System Information message came out of it.
   src/view_gsm.c. */
void set_tab(struct app *app, int new_tab);
void set_decode(struct app *app, int kind);
void adjust_waterfall_scale(struct app *app, int zoom_in);
int scan_strongest_arfcn(const struct app *app);
int scan_strongest_bcch(const struct app *app);
int start_scan(struct app *app);
/* Give the receiver back if a scan is holding it. A no-op otherwise. */
void scan_release_receiver(struct app *app);
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
Rectangle calibration_chart_rect(const struct app *app);
Rectangle lte_waterfall_rect(void);
Rectangle fm_waterfall_rect(const struct app *app);
Rectangle gsm_burst_rect(void);

/* LTE cell-search and broadcast view. */
void draw_fm(struct app *app);
void handle_fm_input(struct app *app);
void update_fm(struct app *app, double now);
/* As above, but `flush` decodes a short final chunk instead of waiting for a
   full one -- what the band scan needs, since a visit is shorter than a
   chunk. */
void update_fm_flush(struct app *app, double now, int flush);
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
/*
 * What the receiver was doing, as the survey record's facts.
 *
 * One function because three places need the same six numbers out of `app` --
 * the chart's live suspicion marks, the window's save, and the headless report
 * -- and assembling them separately is how two copies of one answer start to
 * differ. That is what `survey_session` was extracted to end, and the
 * finished-survey half of it is `.scratch/deepening/issues/12-*`.
 */
void survey_tuning_from(struct survey_record_tuning *out,
                        const struct app *app);

/*
 * This receiver's own reference error and the correction in force -- the two
 * numbers `reading_origin.h` needs, and it needs two.
 *
 * One function because both survey adapters build a `struct survey_block` and
 * the record needs the same pair, and this repository has just spent a ticket
 * on what happens when two copies of a survey fact drift apart.
 *
 * The crystal error is 0 when this receiving setup has never been calibrated,
 * which is a refusal downstream rather than a claim that the receiver is
 * perfect. It is **not** 0 merely because the correction is applied: that was
 * the first version of this and it made the whole measurement unreachable in
 * the shipping program while every unit check stayed green, since the program
 * restores and applies a stored calibration at startup and `calibrated -
 * applied` is then always zero.
 */
struct reading_clock survey_reading_clock(const struct app *app);

/* The frequency window the Scope's spectrum and waterfall share. */
void scope_freq_sync(struct app *app);
/*
 * The keys every chart shares, read once a frame.
 *
 * + and - change the vertical scale; Up and Down zoom the frequency window in
 * and out; Left and Right pan it; 0 puts it back. The split is deliberate:
 * the two axes get two pairs of keys, so neither has to be modal and a reader
 * never has to remember which one the arrows are currently driving.
 *
 * + - and 0 are read as *characters* rather than as keys, and that is not a
 * style choice. raylib names keys after positions on a US keyboard, so
 * KEY_EQUAL and KEY_MINUS are wherever a US board prints = and -. On a
 * Portuguese layout the key printed + sits where a US board has [, so binding
 * the physical keys makes them do nothing at all -- which is exactly how the
 * survey's zoom shipped once. The keypad and the US positions stay as
 * fallbacks, since a numpad + is the same key everywhere.
 *
 * Read once a frame, in the frame loop, because GetCharPressed() drains a
 * queue: two callers asking independently means the second one gets nothing,
 * intermittently, depending on who ran first.
 */
enum chart_key {
    CHART_KEY_NONE = 0,
    CHART_KEY_SCALE_UP,     /* + */
    CHART_KEY_SCALE_DOWN,   /* - */
    CHART_KEY_RESET_ZOOM    /* 0 */
};

enum chart_key chart_key_pressed(void);

int scope_freq_input(struct app *app, Rectangle outer,
                     enum chart_key key);
void scope_freq_reset(struct app *app);
/* How far Left and Right move it, and the narrowest it may become. */
#define SCOPE_FREQ_PAN 0.20
#define SCOPE_ZOOM_STEP 1.4
#define SCOPE_FREQ_MIN_SPAN_HZ 20000.0

/*
 * Which control-row field has the keyboard. NONE is 0 deliberately: the state
 * is zero-initialised with the rest of struct app, and a "focused" value of 0
 * meant the centre field came up believing it was mid-edit, so it was never
 * filled in and the row opened with an empty box under a caption promising a
 * frequency. Said out loud rather than relied on, the way TAB_SURVEY is.
 */
#define SCOPE_FIELD_NONE 0
#define SCOPE_FIELD_CENTRE 1
#define SCOPE_FIELD_START 2
#define SCOPE_FIELD_END 3



void scope_header_sync(struct app *app);
int scope_header_input(struct app *app);
void draw_scope_header(const struct app *app);
void clear_scatter(struct app *app);
int recreate_scatter(struct app *app, Rectangle plot);
int recreate_waterfall(struct app *app, Rectangle plot, int clear_history);
void render_waterfall(struct app *app);
void update_waterfall(struct app *app);
void update_scatter(struct app *app, double now, int insert);
/*
 * The window gestures for a decode view's waterfall: sync it against the
 * current tuning, then take the drag, the zoom keys and the pan.
 *
 * `spacing_hz` is the channel spacing when the axis is channels and 0 when it
 * is linear -- it sets how far in a zoom may go, because a window narrower
 * than one channel can contain no channel centre and leaves the axis blank.
 * `allow_retune` is false where a pan may not move the receiver.
 */
void view_window_input(struct app *app, struct chart_window *win,
                       Rectangle rect, enum chart_key key, double spacing_hz,
                       int allow_retune);

void draw_waterfall_rect(const struct app *app, int calibration_mode,
                         Rectangle rect, const struct chart_window *win);
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


/* Band survey (its own tab): sweep a range, find what stands above the local
   floor, and measure whichever candidate is selected. */
void view_survey_defaults(struct app *app);
void view_survey_enter(struct app *app);
/* Point the range fields at the nth offerable band, 1-based. */
int survey_choose_band(struct app *app, int nth);
void view_survey_leave(struct app *app);
/*
 * Every frame, with `spectrum_updated` saying whether a block came with it.
 *
 * The machine has decisions on both clocks and that is why this is a
 * parameter rather than a guard around the call. A confirmation *look* counts
 * blocks: counting frames gave the pass six looks in a tenth of a second, at
 * a spectrum from before the receiver had retuned. A sweep *step* that has
 * already heard something is over on time alone, and waiting for one more
 * block to say so costs a block per step -- 39 over a 13-step sweep of band
 * II instead of 26.
 */
void update_survey(struct app *app, double now, int spectrum_updated);
/*
 * What a confirmation pass settled, printed for a pass nobody is watching.
 *
 * Shared with the headless sweep so a scripted pass and a scripted window
 * cannot spell the same verdict two ways: the two used to have their own
 * printf loops, and their `# confirm` header lines had already drifted apart
 * -- the headless one promised five fields where its rows carried seven.
 * docs/band-surveys.md is the format.
 */
void survey_print_confirm_header(void);
void survey_print_confirm_target(const struct survey_confirm_target *target);
void survey_print_confirm_summary(const struct survey_session *ss);
void handle_survey_input(struct app *app);
void draw_survey(struct app *app);

/* TETRA (Decode tab): the network's identity, and how it was read. */
void update_tetra(struct app *app, double now);
void draw_tetra(struct app *app);
void handle_tetra_input(struct app *app);
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
int set_frequency_correction(struct device_session *source, int ppm);

#endif
