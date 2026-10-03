#ifndef VIEWER_COMMAND_H
#define VIEWER_COMMAND_H

#include <stddef.h>
#include <stdint.h>

/*
 * The one thing read from a Viewer besides a subscription (ticket 06):
 * a whitespace-delimited command line, parsed with sscanf on the same
 * principle `src/core/capture_sidecar.h` states outright -- this is not a
 * JSON parser and must not become one. `viewer_link.c` decides *that* a
 * line is a command rather than a subscription; parsing what the
 * command actually says lives here, decoupled from sockets and from
 * `struct app`, so it has a check of its own that needs neither.
 */

enum viewer_command_type {
    VIEWER_COMMAND_TUNE = 0,
    VIEWER_COMMAND_VIEW,
    /*
     * `set <field> <value>` stages one Settings field, and `apply` commits
     * the staged set -- which is the window's own shape, not an invention:
     * `handle_settings_input()` only stages and `apply_settings()` validates
     * and applies the whole set at once (`web-visualization/17`).
     *
     * They are two commands rather than one because that is load-bearing.
     * One click on a stepper must not restart acquisition, and
     * `apply_settings()` validates the set *together* -- a rejected PPM must
     * not also lose a transform size the reader had just chosen, which is
     * already why that function applies the size first.
     */
    VIEWER_COMMAND_SET,
    VIEWER_COMMAND_APPLY,
    /*
     * `calibrate gsm|lte` starts a measurement against that reference and
     * `calibrate stop` ends it. Starting is all a command does -- it does
     * **not** apply the result, and that is deliberate: a calibration writes
     * a standing fact about this receiver at this site (ADR-0018, ADR-0022),
     * and applying it is `set ppm` plus `apply`, which is one more
     * deliberate act. A browser that could silently recalibrate a receiver
     * would be a worse thing than one that cannot.
     */
    VIEWER_COMMAND_CALIBRATE,
    /*
     * `scan fm` starts the FM band-II walk and `scan stop` ends it -- the
     * window's "Scan band" button, which the browser needs because in `web`
     * mode there is no window to press it: the Band II table and the
     * waterfall's station marks stay empty until a scan has run, and a scan
     * takes a live receiver (`fm_scan_begin()` says so when there is none).
     * Unlike `calibrate`, a scan applies nothing and takes no standing
     * decision -- it only fills a list -- so it needs no second deliberate
     * act to be safe.
     */
    VIEWER_COMMAND_SCAN,
    /*
     * `select <kind> <n>` is a click on a view's own list or chart, the way
     * `tune` is a click on FM's Band II table: it chooses what the receiver
     * inspects. `select arfcn <n>` inspects a GSM channel, `select cell <n>`
     * parks on an LTE scan row, `select candidate <n>` inspects a survey
     * candidate -- each the window's own click handler, reached from the
     * browser. The value is bounded by the runtime function it calls
     * (`gsm_tune_selected`, `scan_select`, `survey_select`), which refuses an
     * out-of-range index rather than this parser.
     */
    VIEWER_COMMAND_SELECT
};

/* What a `select` names: a GSM channel by ARFCN, an LTE scan row by index, or
   a survey candidate by index. */
enum viewer_select {
    VIEWER_SELECT_ARFCN = 0,
    VIEWER_SELECT_CELL,
    VIEWER_SELECT_CANDIDATE
};

/* Which reference a `calibrate` names, or that it is asking for a stop. */
enum viewer_calibrate {
    VIEWER_CALIBRATE_GSM = 0,
    VIEWER_CALIBRATE_LTE,
    VIEWER_CALIBRATE_STOP
};

/* What a `scan` names: the FM band walk, or a stop. One technology scans
   from a decode view today; a second would be one more name here. */
enum viewer_scan {
    VIEWER_SCAN_FM = 0,
    VIEWER_SCAN_STOP
};

/* Which Settings field a `set` names. Each is one row in a table the parser
   walks, for the reason the screen names are a table: a hand-written branch
   per name is what left the subscribe parser two names short, silently. */
enum viewer_setting {
    VIEWER_SETTING_PPM = 0,
    VIEWER_SETTING_FFT,
    VIEWER_SETTING_GAIN,
    VIEWER_SETTING_DC,
    VIEWER_SETTING_DRIFT
};

/*
 * The screens `view <name>` can ask for -- `view` names the screen, which is
 * the whole of what a command is for.
 *
 * This said "a third is one more name and one more `set_tab()` branch", and
 * the third turned out not to be. FM is not a tab: it is the Decode tab with
 * `DECODE_FM` chosen, so reaching it is a `set_decode()` *and* a `set_tab()`,
 * in that order -- the order sdrprobe.c's own startup sequence already
 * explains, since switching the tab first enters whichever decode kind is
 * already recorded and leaves it again on the way past. A screen name here is
 * therefore a name for a *destination*, not for a member of `enum active_tab`,
 * and the four decode views still to come (ticket 07) are each one more name
 * and one more line of that mapping.
 */
enum viewer_screen {
    VIEWER_SCREEN_SCOPE = 0,
    VIEWER_SCREEN_SURVEY,
    VIEWER_SCREEN_FM,
    VIEWER_SCREEN_GSM,
    VIEWER_SCREEN_ADSB,
    VIEWER_SCREEN_TETRA,
    VIEWER_SCREEN_SRD,
    VIEWER_SCREEN_LTE,
    /* The two overlays. They are screens here for the same reason
       `receiver_state.screen` names them: the window shows a full-screen
       modal, so "which screen is up" is the overlay, not the tab
       underneath (`web-visualization/17`). */
    VIEWER_SCREEN_SETTINGS,
    VIEWER_SCREEN_CALIBRATION
};

struct viewer_command {
    enum viewer_command_type type;
    uint32_t hz;               /* VIEWER_COMMAND_TUNE */
    enum viewer_screen screen; /* VIEWER_COMMAND_VIEW */
    enum viewer_setting setting; /* VIEWER_COMMAND_SET */
    enum viewer_calibrate reference; /* VIEWER_COMMAND_CALIBRATE */
    enum viewer_scan scan;       /* VIEWER_COMMAND_SCAN */
    enum viewer_select select;   /* VIEWER_COMMAND_SELECT */
    /*
     * The value, as a signed integer for every field there is: a PPM is
     * signed, a transform size and a gain index are counts, and an on/off is
     * 1 or 0. `ppm` is parsed here rather than passed as text because a
     * command is not a text field -- there is no half-typed state to
     * represent, which is the one thing `settings_view_model`'s
     * `staged_ppm` has to carry and this does not.
     */
    int value;
};

/* Longest command line this parser will read at all -- past this, a line
   is refused rather than silently truncated. Generous for "tune <hz>"
   and its trailing whitespace; generous for "view survey" too, ticket
   07's own second command, and generous for `set drift off` too. */
#define VIEWER_COMMAND_LINE_MAX 128

/*
 * Parses one line with no trailing newline (the caller has already split
 * on the WebSocket frame boundary, not this). Returns 0 and fills `out` on
 * a recognized, well-formed command -- "tune <hz>" or "view
 * scope|survey|fm". Returns -1 and -- if `error` is non-NULL -- a
 * human-readable reason (truncated to fit `error_cap`, always
 * NUL-terminated) on: an empty or all-whitespace line, a line at or past
 * VIEWER_COMMAND_LINE_MAX, an unrecognized command word, a `tune` value
 * that does not parse as an integer or does not fit `uint32_t`, a `view`
 * naming a screen this build does not have, or a line carrying anything
 * after the value or the screen name but whitespace.
 */
int viewer_command_parse(const char *line, size_t len, struct viewer_command *out,
                         char *error, size_t error_cap);

#endif
