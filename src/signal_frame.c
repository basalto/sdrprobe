#include "signal_frame.h"

#include <math.h>
#include <string.h>

int signal_frame_process(struct signal_frame *frame,
                         const struct signal_frame_input *in,
                         int *geometry_changed) {
    const float *spectrum_i, *spectrum_q;
    double sum = 0.0;
    size_t i;
    int size, windows;

    if (geometry_changed)
        *geometry_changed = 0;
    if (!frame || !in || !in->profile)
        return 0;

    frame->pair_count = sdr_dsp_convert_iq(in->profile, in->bytes,
                                           in->byte_count, frame->i_samples,
                                           frame->q_samples,
                                           frame->magnitudes,
                                           SIGNAL_FRAME_PAIRS);
    if (frame->pair_count == 0)
        return 0;
    frame->have_samples = 1;

    frame->magnitude_min = frame->magnitudes[0];
    frame->magnitude_max = frame->magnitudes[0];
    for (i = 0; i < frame->pair_count; i++) {
        float magnitude = frame->magnitudes[i];
        if (magnitude < frame->magnitude_min)
            frame->magnitude_min = magnitude;
        if (magnitude > frame->magnitude_max)
            frame->magnitude_max = magnitude;
        sum += magnitude;
    }
    frame->magnitude_mean = (float)(sum / (double)frame->pair_count);
    frame->signal_stats_ready = sdr_dsp_signal_stats(
        frame->i_samples, frame->q_samples, frame->magnitudes,
        frame->pair_count, frame->magnitude_sorted, in->profile->full_scale,
        &frame->signal_stats);

    /*
     * The spectrum is taken from a filtered copy when asked, and the decoder's
     * samples are never touched. Copying a block to filter it is the cost of
     * that guarantee and it is worth paying: the alternative is one buffer
     * whose contents depend on a flag set somewhere else.
     */
    spectrum_i = frame->i_samples;
    spectrum_q = frame->q_samples;
    if (in->remove_dc) {
        memcpy(frame->spectrum_i, frame->i_samples,
               frame->pair_count * sizeof(*frame->spectrum_i));
        memcpy(frame->spectrum_q, frame->q_samples,
               frame->pair_count * sizeof(*frame->spectrum_q));
        sdr_dsp_remove_dc(frame->spectrum_i, frame->spectrum_q,
                          frame->pair_count);
        spectrum_i = frame->spectrum_i;
        spectrum_q = frame->spectrum_q;
    }

    /*
     * A different number of bins is a different chart: the peak hold was
     * gathered against the old one and means nothing under the new. The
     * waterfall's history is in the same position and is not ours to clear --
     * `geometry_changed` is how the Scope is told.
     */
    size = sdr_dsp_fft_size_valid(in->fft_size) ? in->fft_size
                                                : SDR_DSP_FFT_SIZE;
    if (size != frame->spectrum_bins) {
        frame->spectrum_peak_ready = 0;
        frame->spectrum_bins = size;
        if (geometry_changed)
            *geometry_changed = 1;
    }

    windows = sdr_dsp_spectrum(&frame->dsp, spectrum_i, spectrum_q,
                               frame->pair_count, frame->spectrum_bins,
                               in->profile->full_scale,
                               frame->spectrum_average,
                               frame->spectrum_candidate);
    if (windows <= 0)
        return 0;

    /* The live bins, not the array's length: it is sized to the largest
       transform and mostly empty at every other size. */
    if (!frame->spectrum_peak_ready) {
        memcpy(frame->spectrum_peak, frame->spectrum_candidate,
               (size_t)frame->spectrum_bins *
               sizeof(frame->spectrum_peak[0]));
        frame->spectrum_peak_ready = 1;
    } else {
        int bin;
        for (bin = 0; bin < frame->spectrum_bins; bin++)
            if (frame->spectrum_candidate[bin] > frame->spectrum_peak[bin])
                frame->spectrum_peak[bin] = frame->spectrum_candidate[bin];
    }
    frame->spectrum_peak_time = in->now;
    frame->spectrum_windows = windows;
    frame->spectrum_ready = 1;
    return 1;
}

void signal_frame_invalidate(struct signal_frame *frame) {
    if (!frame)
        return;
    frame->spectrum_ready = 0;
    frame->spectrum_peak_ready = 0;
}

void signal_frame_decay_peak(struct signal_frame *frame, double now,
                             float db_per_second) {
    double elapsed;
    float decay;
    int bin;

    if (!frame)
        return;
    if (!frame->spectrum_peak_ready) {
        frame->spectrum_peak_time = now;
        return;
    }
    elapsed = now - frame->spectrum_peak_time;
    if (elapsed <= 0.0)
        return;
    decay = (float)elapsed * db_per_second;
    for (bin = 0; bin < frame->spectrum_bins; bin++)
        frame->spectrum_peak[bin] = fmaxf(SDR_DSP_DBFS_FLOOR,
                                          frame->spectrum_peak[bin] - decay);
    frame->spectrum_peak_time = now;
}
