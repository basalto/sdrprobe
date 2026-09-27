#ifndef SIGNAL_FRAME_H
#define SIGNAL_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "device_profile.h"
#include "sdr_dsp.h"

/*
 * One sample block, converted and measured: everything both contexts read
 * before anybody decides what it means.
 *
 * `process_block()` in `sdrprobe.c` did all of this and left the answers in
 * more than twenty loose arrays, counters and ready flags on `struct app`,
 * read directly by views, overlays, survey adapters and headless paths. The
 * primitives underneath are each checked in `check-sdr-dsp` and the rule that
 * picks the transform size is checked in `check-input`; **their composition
 * was checked nowhere**, and the composition is where the interesting faults
 * are. A primitive can be right and the assembled frame still wrong -- a peak
 * hold kept across a change of transform size, or DC-filtered samples handed
 * to a decoder that must have the raw ones.
 *
 * So the conversion, the filtering policy, the transform workspace, the
 * readiness and the invalidation are implementation here, and a consumer
 * reads a result. Delete this module and all of that goes back into
 * `sdrprobe.c` and into every consumer, which is the test for whether it
 * earns its place.
 *
 * **What it deliberately does not know**: which tab or view is on screen. The
 * transform size is an argument, because `input_scope_owns_spectrum()` is a
 * question about presentation and this module is not presentation -- it says
 * its spectrum geometry changed and lets the Scope decide what that means for
 * a waterfall's history.
 */

#define SIGNAL_FRAME_PAIRS 131072   /* one block; SAMPLE_BLOCK_PAIRS */

/*
 * What the caller has to say about a block for it to be measurable.
 *
 * `profile` is the device the bytes came from, and it is the only thing that
 * knows how to read them: the container, the full scale and the bytes per
 * pair all live there, which is what lets an 8-bit capture and a 12-bit part
 * arrive as the same floats.
 *
 * `remove_dc` filters the copy the *spectrum* is taken from and never the
 * samples a decoder reads. That distinction is the whole reason there are two
 * pairs of buffers, and it is asserted rather than described.
 *
 * `fft_size` is what the caller wants, not what it gets: an invalid size
 * falls back to `SDR_DSP_FFT_SIZE` rather than being refused, because a frame
 * with no spectrum is worse than a frame with the default one.
 */
struct signal_frame_input {
    const struct device_profile *profile;
    const uint8_t *bytes;
    size_t byte_count;
    int remove_dc;
    int fft_size;
    double now;                  /* for the peak hold's timestamp */
};

struct signal_frame {
    /* The block itself, centred, and what it measures to. */
    float i_samples[SIGNAL_FRAME_PAIRS];
    float q_samples[SIGNAL_FRAME_PAIRS];
    float magnitudes[SIGNAL_FRAME_PAIRS];
    size_t pair_count;
    int have_samples;
    float magnitude_min;
    float magnitude_mean;
    float magnitude_max;
    struct sdr_signal_stats signal_stats;
    int signal_stats_ready;

    /*
     * One spectrum, sized to the largest transform the Scope may ask for.
     * `spectrum_bins` is how many are currently filled -- everything reading
     * these must use that rather than the array's length, which is a capacity
     * and not a count.
     */
    float spectrum_average[SDR_DSP_FFT_MAX];
    float spectrum_candidate[SDR_DSP_FFT_MAX];
    float spectrum_peak[SDR_DSP_FFT_MAX];
    int spectrum_bins;
    int spectrum_windows;
    int spectrum_ready;
    int spectrum_peak_ready;
    double spectrum_peak_time;

    /*
     * Implementation. `magnitude_sorted` is a block-sized scratch the
     * statistics sort in; it is left reachable because the survey's carrier
     * measurement borrows it rather than carrying a second one, and two
     * block-sized scratch buffers to avoid naming one is the worse trade.
     */
    float spectrum_i[SIGNAL_FRAME_PAIRS];
    float spectrum_q[SIGNAL_FRAME_PAIRS];
    float magnitude_sorted[SIGNAL_FRAME_PAIRS];
    struct sdr_dsp dsp;
};

/*
 * The frame no longer describes what the receiver is pointed at.
 *
 * A retune, a rate change or a DC-filter change makes every bin a measurement
 * of a different span, so the spectrum and its hold are thrown away and the
 * next block rebuilds them. Five call sites set the same **pair** of flags by
 * hand before this existed, which is the shape that drifts: one of them
 * eventually clears the spectrum and keeps the hold, and the chart is then a
 * maximum over two different bands with nothing to say so.
 *
 * The samples are deliberately left alone. They are what they were when they
 * arrived, and a decoder part-way through a block is entitled to finish.
 */
void signal_frame_invalidate(struct signal_frame *frame);

/*
 * Let the peak hold fall towards the floor, `db_per_second` for every second
 * since it was last touched.
 *
 * The rate is the caller's because it is a display preference -- how long a
 * transient should stay legible -- while the walk across the bins is the
 * frame's, and it is the frame's array. It used to be a loop in `view_scope`
 * reaching into these fields.
 */
void signal_frame_decay_peak(struct signal_frame *frame, double now,
                             float db_per_second);

/*
 * Convert and measure one block. Returns 1 when a spectrum was produced, 0
 * when the block yielded nothing usable -- which is what `process_block()`
 * has always returned, because the frame loop uses it to mean "the spectrum
 * moved".
 *
 * Whether the *samples* are usable is `frame->have_samples`, and the two are
 * not the same question: a short block converts and measures but may produce
 * no transform window at all.
 *
 * `geometry_changed`, when not NULL, is set to 1 if the bin count moved. The
 * peak hold is reset here because it belongs to the frame; a waterfall's row
 * history belongs to the Scope, which is why this reports the fact instead of
 * reaching over and clearing it.
 */
int signal_frame_process(struct signal_frame *frame,
                         const struct signal_frame_input *in,
                         int *geometry_changed);

#endif
