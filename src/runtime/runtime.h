#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

#include "runtime/app.h"
#include "tech/lte_scan.h"
#include "runtime/survey_record.h"
#include "runtime/survey_session.h"
#include "core/reading_origin.h"

/*
 * What the per-block step calls, for every frontend.
 *
 * `view.h` declares 146 things -- the drawing, the input handling and the
 * runtime all in one header, included by twenty files, and it includes
 * `<raylib.h>` itself for the rectangles half of them take. So a file that
 * only wants to advance a decode gets the declarations for drawing every
 * screen, and compiles against raylib to do it.
 *
 * This is the half that does not draw: what `frame_advance()` dispatches to,
 * and what a technology's runtime exposes to it. No raylib, no rectangle, no
 * input. `.scratch/layer-boundaries/issues/02-*` is moving the rest here one
 * technology at a time; FM is the first, so for now this holds FM's and
 * `view.h` still holds everything else.
 *
 * The rule for what belongs: if it draws, reads a key or a mouse, or takes a
 * `Rectangle`, it is `view.h`'s. If the shared per-block step calls it, or it
 * advances a decode, it is this header's.
 */

struct app;
struct receiver_lease_token;

/*
 * Moving the receiver, and borrowing it.
 *
 * Declared here rather than in `view.h` because none of it draws and FM's
 * runtime needs all three: a scan borrows the receiver, walks it across band
 * II and gives it back. The definitions are still in `sdrprobe.c`; ticket 02
 * moves those in its own commit, and the declarations move first so the
 * technologies can come out ahead of them.
 *
 * `receiver_lease.h` is the rule the borrow functions enforce and says why.
 * All of them are no-ops that report success in file mode, because a capture
 * holds one tuning and nothing can move it -- so a caller does not have to
 * guard each one with `app->receiver_mode`.
 */
int retune_receiver(struct app *app, uint32_t frequency, int ppm);
/* The same, changing the sample rate with the tuning. Only LTE needs it. */
int retune_receiver_at_rate(struct app *app, uint32_t frequency,
                            uint32_t sample_rate, int ppm);
/* Take the receiver where it stands, without moving it. For an owner that
   tunes later, or several times, or not at all. */
int receiver_borrow(struct app *app, struct receiver_lease_token *token);
/* Take it and move it in one step: on a refusal the token is cancelled, since
   retune_receiver*() has already put the hardware back. `sample_rate` of 0
   means "leave the rate alone". */
int receiver_borrow_at(struct app *app, struct receiver_lease_token *token,
                       uint32_t frequency, uint32_t sample_rate);
int receiver_return(struct app *app, struct receiver_lease_token *token);
/* Give up the claim and keep the tuning: the survey's "Open waterfall". */
int receiver_commit(struct app *app, struct receiver_lease_token *token);

/* The clock every runtime path uses. Never raylib's `GetTime()`, which is
   exactly 0.0 before `InitWindow()` -- see `fm_scan_begin()` for what that
   costs when it leaks into a step with no window. */
double monotonic_seconds(void);


/*
 * The band scan and the recorder, both shared between technologies and
 * neither a drawing. Defined in `overlay_scan.c` and `sdrprobe.c` for now --
 * their declarations move first so a technology's runtime can come out ahead
 * of them, the same way the receiver functions above did.
 */
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

/* --- GSM: the channel, the synchronization decode, the view's own state --- */

void update_gsm_sch(struct app *app, double now);
void enter_gsm(struct app *app);
void leave_gsm(struct app *app);
void view_gsm_defaults(struct app *app);
void start_record(struct app *app);
void gsm_tune_selected(struct app *app, int arfcn);

/* --- ADS-B: the tuning, one block of Mode S, the log's row format --- */

void update_adsb(struct app *app, double now);
int adsb_tuned(const struct app *app);
void enter_adsb(struct app *app);
void leave_adsb(struct app *app);
void view_adsb_defaults(struct app *app);
/* Whether the ADS-B view is on its charts rather than its log. A question
   about state, asked by the drawing. */
int adsb_analysis_showing(const struct app *app);

/* --- TETRA: one carrier, one block, to a network identity --- */

void update_tetra(struct app *app, double now);

/* --- SRD 430-440 MHz: the tuning, one block of OOK/Manchester --- */

void update_srd(struct app *app, double now);
void view_srd_defaults(struct app *app);
/* Whether the receiver is tuned within the SRD band and fast enough to see
   it, the same shape as adsb_tuned() above -- off it, the view offers a
   retune affordance instead of decoding silence. */
int srd_tuned(const struct app *app);
/* Entering the view retunes the receiver to 434 MHz when it is not already
   within the SRD band -- the same shape as enter_gsm()/enter_lte(), because
   a manual "Retune to 434 MHz" click depends on a click landing correctly,
   and the whole point of opening this view is to be listening in the right
   place. leave_srd() gives the borrowed tuning back. */
void enter_srd(struct app *app);
void leave_srd(struct app *app);
/* The SRD header's tuning group: the typed centre frequency shown, committed,
   and stepped by half a span. `srd_tune_step()` is the arithmetic and
   `srd_dsp.h` owns it; these three are the state around the field, which the
   input handler drives and which no drawing decides. */
void srd_freq_show(struct app *app);
int srd_freq_commit(struct app *app);
void srd_tune_arrow(struct app *app, int direction);

/*
 * The bands this receiver can sweep, for whichever panel is drawing the row.
 *
 * One accessor because **two panels draw the same row** -- the LTE view's
 * header and the calibration overlay's 4G arrangement -- and two copies of
 * "which bands" is how they come to disagree about what the buttons mean.
 * It used to be a compiled-in literal that both read, which had the same
 * effect and was wrong about every tuner but one.
 *
 * `out` must hold LTE_BANDS_MAX. Returns the count, which is **0 for a
 * capture**: its profile reaches one frequency, so it sweeps no band.
 */
static inline int view_lte_bands(const struct app *app, int *out) {
    if (!app || !out)
        return 0;
    return lte_bands_reachable(app->device.tune_lower_hz,
                               app->device.tune_upper_hz, out,
                               LTE_BANDS_MAX);
}

/* --- LTE: the 1.92 MS/s grid, the band scan, one block's cell search --- */

void update_lte(struct app *app, double now);
void view_lte_defaults(struct app *app);
/* The Scope's and the survey's starting numbers. Set by `main()` before any
   window exists, so they cannot live beside a drawing. */
void view_scope_defaults(struct app *app);
void scope_release_history(struct app *app);
void view_survey_defaults(struct app *app);
/* The ppm correction, applied to whatever device is open. */
int set_frequency_correction(struct device_session *source, int ppm);
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
/* Which band the scan's picker has selected, or NULL when the receiver
   reaches none -- a capture. Read by the drawing and by the scan alike. */
const struct lte_band *selected_band(const struct app *app);
/* Put the receiver in the selected band, start and stop a scan, and take the
   cell a row names. The input handler drives all four; none of them draws. */
void park_in_band(struct app *app);
int scan_start(struct app *app, double now);
void scan_stop(struct app *app);
void scan_select(struct app *app, int row);

/*
 * One block: convert it, measure it, and say whether a spectrum came out.
 * `fft_size` is the caller's choice -- see `frame_advance.h`. Defined in
 * `sdrprobe.c` for now; ticket 02 has not moved it, the application layer
 * being arguably its home.
 */
int process_block(struct app *app, double now, int fft_size);
/* What size the screen wants the spectrum measured at -- the Scope's
   resolution stepper, but only while the Scope owns the spectrum. A question
   about presentation, handed to `process_block()` rather than looked up
   inside it; `headless` and `server` answer it without a screen. */
int scope_requested_fft_size(const struct app *app);
/* The one global the program has, read through a function so it stays one. */
int stop_requested(void);
/* The band survey with no window: sweep, then print the candidates to
   stdout, one per line. src/runtime/survey_report.c. */
int survey_report_run(struct app *app);
/*
 * The window, to whichever binary has one.
 *
 * `sdrprobe` fills this in with its frame loop and its teardown;
 * `sdrprobe` passes NULL, is built with no raylib at all, and refuses
 * a windowed mode by naming the build (`.scratch/layer-boundaries/issues/04-*`).
 * Everything else about the two runs -- the flags, the subcommands, the
 * messages -- is `app_main.c` and identical.
 */
struct app_window {
    int (*run)(struct app *app);        /* the frame loop */
    void (*release)(struct app *app);   /* textures, audio, the window */
};

int sdrprobe_main(int argc, char **argv, const struct app_window *window);

/* Every run with no window -- `headless` and `server` both. src/app/headless_run.c. */
int run_headless(struct app *app);
/* The receivers this machine has, as the backend enumerates them. */
int list_devices(void);
/* Which basename and technology a `--record` on the command line means. */
void cli_record_labels(const struct options *options, const char **basename,
                       const char **technology);

/* --- The Scope's per-block data: the peak's decay, the two histories --- */

/*
 * `advance_waterfall_row` and `advance_scatter_history` are the data halves
 * of what `update_waterfall` and `update_scatter` used to be: plain float
 * maintenance with no GL context. `render_waterfall` and `render_scatter`
 * are the GPU halves and stay in `view.h`; the window's frame loop calls
 * each pair in sequence.
 *
 * `allocate_waterfall_history` sizes the dBFS rows and is how `server` gets
 * a waterfall with no texture behind it (ADR-0027 has the Viewer build its
 * own history from the rows it is sent).
 */
int allocate_waterfall_history(struct app *app, int rows);
void advance_waterfall_row(struct app *app);
void advance_scatter_history(struct app *app, double now, int insert);
void decay_spectrum_peak(struct app *app, double now);

/* --- The GSM band scan's per-block step --- */

void update_scan(struct app *app);

/* --- Calibration's per-block work: the residual buffer and the drift
       re-check. ADR-0004's source-homogeneity rule is theirs to keep. --- */

void update_calibration_measurement(struct app *app);
void update_drift_check(struct app *app, int have_block);
/* Which band the calibration overlay's 4G arrangement has selected, or NULL.
   Read by the drawing and by the measurement alike, so it lives with the
   measurement. */
const struct lte_band *cal_selected_band(const struct app *app);

/* --- The startup machine's frame step (ADR-0024) --- */

void update_startup(struct app *app, int have_block);
/* Give the receiver back, whatever happened to the measurement. The form's
   own Cancel and Apply call it too, which is why it is exported. */
void startup_release(struct app *app);

/* --- The survey's per-block step, and the five helpers a click shares
       with it. `survey_session.h` is the machine; these are the adapter. --- */

/* Back to where this owner started, still holding the receiver. The survey
   between sweeps: it has finished walking the band but still owns the right
   to sweep again. */
int receiver_restore_held(struct app *app,
                          const struct receiver_lease_token *token);
/* The crystal error and the correction in force, for a reading's origin. */
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
/* The four facts a candidate needs, read out of `struct app` once. This is
   the adapter between the application and the survey's contract, so it lives
   here and not in `survey_view_model.c`, which now takes plain state and
   compiles without raylib (layer-boundaries ticket 03). */
void survey_tuning_from(struct survey_record_tuning *out,
                        const struct app *app);
/* What a scripted sweep prints about a confirmation pass. */
void survey_print_confirm_header(void);
void survey_print_confirm_target(const struct survey_confirm_target *target);
void survey_print_confirm_summary(const struct survey_session *ss);
/* The range fields' own spelling of a frequency, and two predicates the view
   and the step share. */
void survey_format_hz(char *text, size_t size, uint32_t hz);
void survey_history_refresh(struct app *app);
int survey_peak_visible(const struct survey_view *s, int index);
double survey_bin_hz(const struct survey_view *s, int bin);
void survey_clamp_view(struct survey_view *s);
/* The receiver's own lifecycle, called by `main()` in either binary: open
   and configure the device or the capture, and install the handlers that
   stop a long run. They were `static` beside `run_gui()`. */
struct view_input;
struct input_state;
struct view_input view_input_now(const struct app *app);
struct input_state input_state_now(const struct app *app);
/* Whether the receiver's worker is still inside a read, and whether it has
   finished -- what a shutdown waits on. */
int worker_is_reading(struct app *app, int *done);
/* Switching screens. Both retune -- leaving the GSM view restores the tuning
   it borrowed, entering the survey borrows the receiver -- so they are
   application layer, not drawing, and `web` reaches them from a Viewer
   command with no window anywhere. */
void set_tab(struct app *app, int new_tab, double now);
void set_decode(struct app *app, int kind, double now);
int configure_receiver(struct app *app);
int start_acquisition(struct app *app);
int stop_acquisition(struct app *app);
int open_capture(struct app *app);
int install_signal_handlers(struct app *app);

/* Arriving on the survey tab and leaving it. Called by `set_tab()`, which is
   application layer, so neither may live beside the drawing -- and neither
   draws. The four below were `static` in `view_survey.c` and are named here
   because its own buttons still call them. */
void view_survey_enter(struct app *app, double now);
void view_survey_leave(struct app *app);
int survey_start(struct app *app, double now);
void survey_clear(struct survey_view *s);
void survey_reset_view(struct survey_view *s);
void survey_load_installation(struct app *app);
struct survey_block survey_block_of(struct app *app);
void survey_obey(struct app *app, const struct survey_session_event *event,
                 double now);
void survey_confirm_if_asked(struct app *app, double now);
int survey_strongest_visible(const struct survey_view *s, int rank);
void survey_select(struct app *app, int index, double now);
void update_survey(struct app *app, double now, int spectrum_updated);

/* --- FM: the discriminator, the RDS chain, the band scan, the tuning --- */

void update_fm(struct app *app, double now);
/* As above, but `flush` decodes a short final chunk instead of waiting for a
   full one -- what the band scan needs, since a visit is shorter than a
   chunk. */
void update_fm_flush(struct app *app, double now, int flush);
void view_fm_defaults(struct app *app);
/* Retune and start over: everything in the view belongs to one carrier. */
void fm_tune(struct app *app, double hz);
/* Whether the FM view's frequency field has focus. Asked by the view itself;
   the frame loop reads the raw field and lets view_input.h decide what it
   means, which is where survey_editing() and srd_editing() went. */
int fm_editing(const struct app *app);
/*
 * Walking band II: a coarse sweep, then the carriers it found.
 *
 * `now` is the caller's clock and never raylib's `GetTime()`, which is
 * exactly 0.0 before `InitWindow()` -- and this is reachable with no window,
 * through a Viewer's `view fm`. See `fm_scan_begin()`'s own comment.
 */
void fm_scan_begin(struct app *app, double now);
void fm_scan_stop(struct app *app);
void update_fm_scan(struct app *app, double now, int have_block);
int fm_scan_showing(const struct app *app);
/* Put the receiver in band II when the view is opened. Takes `now` for the
   same reason `fm_scan_begin()` does -- it is one of its callers. */
void enter_fm(struct app *app, double now);

#endif
