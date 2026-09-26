#ifndef VIEW_H
#define VIEW_H

#include <raylib.h>

#include "app.h"
#include "runtime.h"
#include "gui_state.h"
#include "survey_record.h"
#include "lte_dsp.h"
#include "scope_view_model.h"

struct sdrgui_waterfall_marker;

/*
 * The two chart-window entry points that take a rectangle, and so raylib.
 * `chart_window.h` holds the state and the arithmetic and is included by
 * `app.h`, which no longer compiles against raylib; these two live here,
 * where a rectangle is already at home (ticket 01 of layer-boundaries).
 * Defined in chart_window.c, which includes this header.
 */
enum chart_key;

/* Handles the drag, the zoom keys and the pan. Returns the hertz the receiver
   must move for the pan to continue past the edge of what it is delivering,
   or 0. Retuning is the caller's, because whether it is allowed differs. */
double chart_window_input(struct chart_window *w, Rectangle plot,
                          enum chart_key key, double min_span);
int chart_window_drag_of(const struct chart_window *w, Rectangle plot,
                         double *lower_hz, double *upper_hz);


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

/* The startup form (overlay_startup.c, ADR-0024). It draws and reads input;
   `startup_session.{c,h}` is what decides. */
void open_startup(struct app *app);
void update_startup(struct app *app, int have_block);
void handle_startup_input(struct app *app);
void draw_startup(struct app *app);
void draw_button_enabled(Rectangle rectangle, const char *label, int enabled);
/* Stop measuring and hand the receiver back, staying on the screen. Returns
   negative when the retune failed, in which case nothing changed. */
int calibration_stop_measuring(struct app *app);

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
/* Take it and move it in one step: on a refusal the token is cancelled, since
   retune_receiver*() has already put the hardware back. `sample_rate` of 0
   means "leave the rate alone". */
/* Back to where this owner started, still holding it. The survey between
   sweeps: it has finished walking the band but still owns the right to sweep
   again. */
int receiver_restore_held(struct app *app,
                          const struct receiver_lease_token *token);
/* Give it back. Restores with the *current* PPM, so a calibration applied
   while borrowed survives the return. A failed retune leaves the token live
   and retryable. */
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
void set_tab(struct app *app, int new_tab, double now);
void set_decode(struct app *app, int kind, double now);
void adjust_waterfall_scale(struct app *app, int zoom_in);
int scan_strongest_arfcn(const struct app *app);
int scan_strongest_bcch(const struct app *app);
int compare_double(const void *left, const void *right);

/* GSM band-analysis view. */
void draw_gsm(struct app *app);
void handle_gsm_input(struct app *app);
Rectangle gsm_scan_rect(void);
Rectangle gsm_waterfall_rect(void);
Rectangle calibration_chart_rect(const struct app *app);
Rectangle lte_waterfall_rect(const struct app *app);
Rectangle fm_waterfall_rect(const struct app *app);
Rectangle gsm_burst_rect(void);

/* LTE cell-search and broadcast view. */
void draw_fm(struct app *app);
void handle_fm_input(struct app *app);
/* FM's runtime -- what `frame_advance()` calls, the band scan, the tuning --
   moved to `runtime.h`, which this header includes so every existing caller
   still compiles (layer-boundaries ticket 02). */
/* Start or stop the sound. The device opens on the first press. */
void fm_play(struct app *app);
void update_fm_audio(struct app *app);
void fm_audio_close(struct app *app);

void draw_lte(struct app *app);
void handle_lte_input(struct app *app);

/* Mode S / ADS-B view. */
void draw_adsb(struct app *app);
void handle_adsb_input(struct app *app);
Rectangle adsb_waterfall_rect(const struct app *app);


/* Scope tab: the four signal views, and the GPU resources two of them keep
   between frames. render_waterfall and render_scatter are here because the
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
/*
 * The receiver transaction over this application's state, built one way.
 *
 * `retune_receiver()` and the Settings panel both change what the receiver is
 * doing, and both used to construct their own sequence -- which is how the
 * Settings panel came to move the tuning without advancing the generation
 * ADR-0027 publishes. One constructor, so there is one transaction.
 */
struct receiver_runtime runtime_over(struct app *app);

int recreate_waterfall(struct app *app, Rectangle plot, int clear_history);
int allocate_waterfall_history(struct app *app, int rows);
void render_waterfall(struct app *app);
/*
 * advance_waterfall_row and advance_scatter_history are the data halves of
 * what update_waterfall and update_scatter used to be: plain float
 * maintenance, callable from the advance step in frame_advance.h with no GL
 * context. render_waterfall and render_scatter are the GPU halves and stay
 * draw-phase calls; the frame loop calls each pair in sequence.
 */
void advance_waterfall_row(struct app *app);
void advance_scatter_history(struct app *app, double now, int insert);
void render_scatter(struct app *app, double now);
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

struct sdrgui_marker_axes waterfall_marker_axes(const struct app *app,
                                                Rectangle rect,
                                                const struct chart_window *win);

void draw_waterfall_rect(const struct app *app, int calibration_mode,
                         Rectangle rect, const struct chart_window *win);
void draw_waterfall_rect_with_markers(const struct app *app, int calibration_mode,
                                      Rectangle rect, const struct chart_window *win,
                                      const struct sdrgui_waterfall_marker *markers,
                                      int marker_count,
                                      int *out_hovered_marker_id);
/*
 * The Scope's own three views. Each reads its measurements from a
 * `struct scope_view_model` (scope_view_model.h) rather than `app->frame` or
 * `app->applied` directly -- `app` is still passed for what stays
 * view-owned: the plot rectangle, the zoom/pan/drag window, and the GPU
 * resources (the scatter and waterfall textures) that cannot be plain data.
 *
 * draw_spectrum() and draw_waterfall() take an explicit `plot` rather than
 * reading `app->plot` themselves, because the combined Spectrum+Waterfall
 * view (VIEW_SPECTRUM) splits one plot rectangle between the two of them
 * (scope_layout.h's scope_plot_split()) rather than handing either the
 * whole thing.
 */
void draw_waterfall(const struct app *app, const struct scope_view_model *svm,
                    Rectangle plot);
void draw_base_hud(const struct app *app, const struct slot_snapshot *snapshot);
void draw_magnitude(const struct app *app, const struct scope_view_model *svm);
void draw_spectrum(const struct app *app, const struct scope_view_model *svm,
                   Rectangle plot);
void draw_scatter(const struct app *app, const struct scope_view_model *svm);
void view_scope_defaults(struct app *app);
int view_scope_resize_if_needed(struct app *app, Rectangle plot);
void view_scope_release(struct app *app);
void recompute_magnitude_bins(struct app *app);
void decay_spectrum_peak(struct app *app, double now);
void adjust_active_scale(struct app *app, int zoom_in);


/* Band survey (its own tab): sweep a range, find what stands above the local
   floor, and measure whichever candidate is selected. */
void view_survey_defaults(struct app *app);
void view_survey_enter(struct app *app, double now);
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
void draw_tetra(struct app *app);
void handle_tetra_input(struct app *app);
Rectangle tetra_waterfall_rect(const struct app *app);

/* SRD 433-435 MHz (Decode tab): Short Range Devices OOK / Manchester. */
void draw_srd(struct app *app);
void handle_srd_input(struct app *app);
/* The log mode's waterfall rectangle, or a zero rect in analysis mode, so a
   caller that dispatches Up/Down and drag to it does nothing there. */
Rectangle srd_waterfall_rect(const struct app *app);

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
