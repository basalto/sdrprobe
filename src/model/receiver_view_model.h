#ifndef RECEIVER_VIEW_MODEL_H
#define RECEIVER_VIEW_MODEL_H

#include <stddef.h>
#include <stdint.h>

struct receiver_applied;
struct device_profile;

/*
 * What the receiver is doing, as plain data -- the shell's half of what a
 * Viewer is told, and the whole of `receiver_state` on the wire.
 *
 * It used to be six fields on `struct scope_view_model`, which made the
 * Scope's chart model the carrier of facts that have nothing to do with a
 * chart: which screen is showing is not a measurement, and a browser on the
 * Survey tab was reading it out of a message named for the Scope
 * (`.scratch/layer-boundaries/issues/03-*`, item 4). Separated, the shell's
 * state has a type of its own and a builder that needs neither a
 * `struct signal_frame` nor a `struct scope_view`.
 *
 * `struct scope_view_model` **embeds** one rather than restating it: the
 * tuning is what the Scope's frequency axis is drawn against and what
 * ADR-0027's staleness rule is decided by, so the charts genuinely need
 * these numbers -- they just do not own them. Two copies that agree today
 * is the shape this repository keeps paying for.
 */
struct receiver_view_model {
    /*
     * Which screen is showing, as a name -- never as an enum's integer.
     *
     * This was `tab` and `decode`, two of this program's enums re-declared
     * in JavaScript as raw numbers, which is exactly what drew the survey's
     * marks swapped for months (`web-visualization/15`). The vocabulary is
     * the one `view <name>` already uses: "survey", "scope", and on the
     * Decode tab the technology -- "fm", "adsb", "gsm", "lte", "tetra",
     * "srd" -- and, when one is up, the overlay: "settings" or
     * "calibration". The overlays outrank the tab because that is what the
     * window shows: they are full-screen modals over whatever tab is
     * underneath (ADR-0008), so a Viewer that reported the tab would name
     * the screen a reader is *not* looking at. It matches each browser
     * view's own `id` exactly, so the shell
     * looks a view up by name.
     *
     * The two integers were kept beside it for one ticket and are gone:
     * nothing read them, on either side of the wire.
     */
    char screen[12];

    /* The receiver's applied tuning, rate and ppm -- Probe language. */
    uint32_t center_hz;
    uint32_t sample_rate_hz;
    int ppm;

    /* ADR-0027's tuning generation: advanced by the receiver transaction
       (receiver_runtime.h) on a success and by nothing else, so every path
       that moves the receiver -- a Viewer command, the Settings panel, a
       survey step -- moves it. It used to be retune_receiver()'s own line,
       which left the Settings panel changing the tuning while this said
       nothing had changed. */
    uint32_t tuning_generation;

    /* The container's full scale, which every dBFS figure is relative to.
       A device-profile fact rather than a receiver setting, and here
       because a reader deriving levels needs it alongside the tuning. */
    float full_scale;

    /* The build's version, "v<major>.<minor>.<patch>" -- what the window
       draws in its own corner (SDRPROBE_SIGNATURE), so a Viewer's footer can
       say which program it is a view of. A constant rather than a measurement,
       carried here because `receiver_state` is the one message that is about
       the program rather than a technology, and the builder is the one place
       version.h is already in hand. */
    char version[16];
};

/*
 * Tab plus decode kind as the one name above. Exposed so a check can walk
 * every screen without building a receiver or a `struct app`.
 *
 * `tab` is `enum active_tab` (input_route.h) and `decode` is
 * `enum decode_kind` (app.h), as ints -- the way `struct app` holds them.
 * Neither travels; the name they collapse to is what does.
 */
void receiver_screen_name(char *out, size_t size, int tab, int decode,
                          int settings_open, int calibration_open);

/*
 * Fills `out` from the receiver's applied state and the device profile.
 * Reads only plain fields -- no GL or raylib call, no I/O.
 */
void receiver_view_model_build(const struct receiver_applied *applied,
                               const struct device_profile *device,
                               int tab, int decode, int settings_open,
                               int calibration_open,
                               struct receiver_view_model *out);

#endif
