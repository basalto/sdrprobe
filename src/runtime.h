#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

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
