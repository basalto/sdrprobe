#include "check.h"

#include "signal_frame.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * What one block becomes, and the decisions that were unreachable while it
 * was a function in `sdrprobe.c` writing twenty fields of `struct app`.
 *
 * `check-sdr-dsp` proves each primitive and `check-input` proves the rule that
 * picks a transform size. Neither could reach their *composition*, which is
 * where a peak hold survives a change of geometry, or a decoder is handed the
 * filtered samples instead of the raw ones. Those are the faults this file is
 * for.
 */

static struct signal_frame frame;     /* ~3 MB; not a stack object */

/* A block of unsigned 8-bit interleaved I/Q, the house container: a tone at a
   given fraction of the sample rate, with a DC offset if asked for one. */
static uint8_t bytes[SIGNAL_FRAME_PAIRS * 2];

static size_t fill_tone(double cycles_per_pair, double amplitude,
                        double dc_i, double dc_q, size_t pairs) {
    size_t n;

    for (n = 0; n < pairs; n++) {
        double phase = 2.0 * M_PI * cycles_per_pair * (double)n;
        double i = amplitude * cos(phase) + dc_i;
        double q = amplitude * sin(phase) + dc_q;
        int bi = (int)(i + 127.5), bq = (int)(q + 127.5);
        bytes[2 * n] = (uint8_t)(bi < 0 ? 0 : bi > 255 ? 255 : bi);
        bytes[2 * n + 1] = (uint8_t)(bq < 0 ? 0 : bq > 255 ? 255 : bq);
    }
    return pairs * 2;
}

static struct device_profile house(void) {
    return device_profile_rtlsdr("check", NULL, 0);
}

static struct signal_frame_input an_input(size_t byte_count, int fft_size) {
    static struct device_profile profile;
    struct signal_frame_input in;

    profile = house();
    memset(&in, 0, sizeof(in));
    in.profile = &profile;
    in.bytes = bytes;
    in.byte_count = byte_count;
    in.fft_size = fft_size;
    in.now = 1.0;
    return in;
}

/* The whole of it, once: a block in, a measured frame out. */
static void test_a_block_becomes_a_frame(void) {
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 40.0, 0.0, 0.0, 16384);

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_SIZE);
    check_int("a block produces a spectrum",
              signal_frame_process(&frame, &in, NULL), 1);
    check_size("every pair converted", frame.pair_count, 16384);
    check_true("the samples are usable", frame.have_samples == 1);
    check_true("and measured: a tone's magnitude barely varies",
               frame.magnitude_max - frame.magnitude_min < 4.0f);
    check_true("its mean sits between the two",
               frame.magnitude_mean >= frame.magnitude_min &&
               frame.magnitude_mean <= frame.magnitude_max);
    check_true("the statistics were taken", frame.signal_stats_ready == 1);
    check_int("the spectrum has the default geometry", frame.spectrum_bins,
              SDR_DSP_FFT_SIZE);
    check_true("and windows to average over", frame.spectrum_windows > 0);
    check_true("it is ready to read", frame.spectrum_ready == 1);
    check_true("with a peak hold started", frame.spectrum_peak_ready == 1);
    check_close("stamped with the time it was handed", frame.spectrum_peak_time,
                1.0, 1e-9);
}

/*
 * DC removal changes the spectrum's input and never the decoder's samples.
 *
 * This is the fault the two pairs of buffers exist to prevent, and it is
 * invisible from any primitive: `sdr_dsp_remove_dc()` is correct either way,
 * and the question is only which array it was pointed at. A decoder handed
 * centred samples it did not ask for reads a different signal.
 */
static void test_dc_removal_never_touches_the_decoders_samples(void) {
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 30.0, 25.0, -20.0, 16384);
    static float raw_i[64], raw_q[64];
    int k;

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_SIZE);
    in.remove_dc = 0;
    check_int("without the filter", signal_frame_process(&frame, &in, NULL), 1);
    for (k = 0; k < 64; k++) {
        raw_i[k] = frame.i_samples[k];
        raw_q[k] = frame.q_samples[k];
    }

    memset(&frame, 0, sizeof(frame));
    in.remove_dc = 1;
    check_int("and with it", signal_frame_process(&frame, &in, NULL), 1);
    for (k = 0; k < 64; k++) {
        check_msg(frame.i_samples[k] == raw_i[k],
                  "sample %d of I was filtered underneath the decoder: "
                  "%.4f against %.4f\n", k, frame.i_samples[k], raw_i[k]);
        check_msg(frame.q_samples[k] == raw_q[k],
                  "sample %d of Q was filtered underneath the decoder: "
                  "%.4f against %.4f\n", k, frame.q_samples[k], raw_q[k]);
    }
    /* And the filtered copy really is filtered: a 25-count offset does not
       survive it. */
    {
        double mean = 0.0;
        size_t n;
        for (n = 0; n < frame.pair_count; n++)
            mean += frame.spectrum_i[n];
        mean /= (double)frame.pair_count;
        check_true("while the spectrum's own copy has the offset removed",
                   fabs(mean) < 0.5);
    }
}

/*
 * A change of transform size resets the peak hold and says so.
 *
 * The peak hold is a maximum over bins that no longer mean the same
 * frequencies, so keeping it across a resize draws a chart out of two
 * different instruments. Saying so is the other half: the waterfall's row
 * history has exactly the same problem and belongs to the Scope, which cannot
 * know unless it is told.
 */
static void test_a_new_geometry_resets_the_hold_and_reports_it(void) {
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 40.0, 0.0, 0.0, 16384);
    int changed = -1;

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_SIZE);
    check_int("the first block", signal_frame_process(&frame, &in, &changed), 1);
    check_int("arrives as a change of geometry, from nothing", changed, 1);
    check_int("at the size asked for", frame.spectrum_bins, SDR_DSP_FFT_SIZE);

    changed = -1;
    check_int("the same size again", signal_frame_process(&frame, &in, &changed),
              1);
    check_int("is not a change", changed, 0);
    check_true("and the hold was kept", frame.spectrum_peak_ready == 1);

    changed = -1;
    in.fft_size = 4096;
    check_int("a different size", signal_frame_process(&frame, &in, &changed),
              1);
    check_int("is a change", changed, 1);
    check_int("and the geometry followed", frame.spectrum_bins, 4096);
    check_true("the hold was rebuilt rather than carried over",
               frame.spectrum_peak_ready == 1);
}

/* Equal sizes accumulate: the hold is a maximum, so a louder block raises it
   and a quieter one leaves it alone. */
static void test_the_hold_keeps_the_loudest(void) {
    struct signal_frame_input in;
    float after_loud;
    int bin, peak_bin = 0;
    size_t used;

    memset(&frame, 0, sizeof(frame));
    used = fill_tone(0.10, 60.0, 0.0, 0.0, 16384);
    in = an_input(used, SDR_DSP_FFT_SIZE);
    check_int("a loud block", signal_frame_process(&frame, &in, NULL), 1);
    for (bin = 0; bin < frame.spectrum_bins; bin++)
        if (frame.spectrum_peak[bin] > frame.spectrum_peak[peak_bin])
            peak_bin = bin;
    after_loud = frame.spectrum_peak[peak_bin];

    used = fill_tone(0.10, 6.0, 0.0, 0.0, 16384);
    in.byte_count = used;
    check_int("then a quiet one at the same size",
              signal_frame_process(&frame, &in, NULL), 1);
    check_true("the quiet block's own bin is lower",
               frame.spectrum_candidate[peak_bin] < after_loud - 3.0f);
    check_close("but the hold still remembers the loud one",
                frame.spectrum_peak[peak_bin], after_loud, 0.01);

    /* And going back to a size it has seen before still rebuilds, because the
       bins were thrown away when the geometry changed rather than stashed. */
    in.fft_size = 1024;
    check_int("a resize", signal_frame_process(&frame, &in, NULL), 1);
    in.fft_size = SDR_DSP_FFT_SIZE;
    check_int("and back again", signal_frame_process(&frame, &in, NULL), 1);
    check_true("reads the quiet block, not the loud one it held before",
               frame.spectrum_peak[peak_bin] < after_loud - 3.0f);
}

/* An invalid size is the default rather than a refusal: a frame with no
   spectrum is worse than a frame with the ordinary one. */
static void test_an_impossible_size_falls_back(void) {
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 40.0, 0.0, 0.0, 16384);

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, 1000);          /* not a power of two */
    check_int("it still produces a spectrum",
              signal_frame_process(&frame, &in, NULL), 1);
    check_int("at the default size", frame.spectrum_bins, SDR_DSP_FFT_SIZE);

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_MAX * 4);   /* past the largest */
    check_int("and again for one too large",
              signal_frame_process(&frame, &in, NULL), 1);
    check_int("the default", frame.spectrum_bins, SDR_DSP_FFT_SIZE);
}

/*
 * Too little to transform is not the same as too little to read.
 *
 * `signal_frame_process()` returns whether a *spectrum* was produced, because
 * that is what the frame loop uses it for. Whether the samples are usable is
 * `have_samples`, and a block can convert and measure while yielding no
 * transform window at all -- reporting the first as the second would throw the
 * samples away.
 */
static void test_a_short_block_still_has_samples(void) {
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 40.0, 0.0, 0.0, 64);

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_SIZE);
    check_int("no spectrum comes of 64 pairs",
              signal_frame_process(&frame, &in, NULL), 0);
    check_size("but they were converted", frame.pair_count, 64);
    check_true("and are readable", frame.have_samples == 1);
    check_true("with nothing claiming a spectrum", frame.spectrum_ready == 0);
}

/* And what it refuses outright. */
static void test_refusals(void) {
    struct signal_frame_input in = an_input(1024, SDR_DSP_FFT_SIZE);
    int changed = -1;

    check_int("no frame to fill in",
              signal_frame_process(NULL, &in, NULL), 0);
    check_int("no input", signal_frame_process(&frame, NULL, NULL), 0);
    in.profile = NULL;
    check_int("and no device to read the bytes with",
              signal_frame_process(&frame, &in, &changed), 0);
    check_int("a refusal is not a change of geometry", changed, 0);

    memset(&frame, 0, sizeof(frame));
    in = an_input(0, SDR_DSP_FFT_SIZE);
    check_int("an empty block yields nothing",
              signal_frame_process(&frame, &in, NULL), 0);
    check_true("and says its samples are not usable",
               frame.have_samples == 0);
}

/*
 * The same signal in an 8-bit and a 16-bit container is the same frame.
 *
 * `check-sample-format` pins this for the converter; here it is pinned for
 * everything downstream of it, which is the claim that actually matters:
 * identical floats mean identical answers by construction, so the statistics,
 * the magnitudes and the spectrum must all agree bit for bit rather than
 * closely. `(byte - 127.5) * 16` is exact and 2040 is exactly 127.5 times
 * sixteen, so "close" would be hiding something.
 */
static void test_a_wider_container_is_the_same_frame(void) {
    static uint8_t wide[SIGNAL_FRAME_PAIRS * 4];
    static float narrow_i[256], narrow_q[256], narrow_spectrum[256];
    static struct device_profile wide_profile;
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 40.0, 3.0, -2.0, 16384);
    size_t n;
    float min, max, mean;
    int k;

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_SIZE);
    check_int("the 8-bit block", signal_frame_process(&frame, &in, NULL), 1);
    for (k = 0; k < 256; k++) {
        narrow_i[k] = frame.i_samples[k];
        narrow_q[k] = frame.q_samples[k];
        narrow_spectrum[k] = frame.spectrum_average[k];
    }
    min = frame.magnitude_min;
    max = frame.magnitude_max;
    mean = frame.magnitude_mean;

    /* The same samples in the 16-bit container a 12-bit part delivers.
       `(byte - 127.5) * 16`, which is exact -- 127.5 times sixteen is 2040 --
       and not `(byte - 128) * 16`, which is the same shape and eight counts
       wrong. */
    for (n = 0; n < used; n++) {
        int value = (int)bytes[n] * 16 - 2040;
        wide[2 * n] = (uint8_t)(value & 0xff);
        wide[2 * n + 1] = (uint8_t)((value >> 8) & 0xff);
    }
    wide_profile = house();
    wide_profile.format = SAMPLE_FORMAT_S16;
    wide_profile.bytes_per_pair = 4;
    wide_profile.full_scale = 2040.0f;

    memset(&frame, 0, sizeof(frame));
    in.profile = &wide_profile;
    in.bytes = wide;
    in.byte_count = used * 2;
    check_int("the 16-bit one", signal_frame_process(&frame, &in, NULL), 1);
    check_size("holds the same pairs", frame.pair_count, 16384);
    for (k = 0; k < 256; k++) {
        check_msg(frame.spectrum_average[k] == narrow_spectrum[k],
                  "bin %d differs between containers: %.6f against %.6f\n",
                  k, frame.spectrum_average[k], narrow_spectrum[k]);
    }
    /*
     * The spectrum is identical because dBFS is normalised by the profile's
     * own full scale. **The magnitude summary is not, and must not be**: the
     * floats stay in the device's own counts, because clipping means "at the
     * ADC's rail" and a rail is a count -- a 12-bit part rails at 2047.5,
     * nowhere near its container's 32767.5. So these scale by exactly the
     * sixteen the container was shifted by, and a check asserting they were
     * equal would be asking for the normalisation this program refuses.
     */
    check_close("the magnitudes are the same signal in the device's counts",
                (double)frame.magnitude_min, (double)min * 16.0, 0.01);
    check_close("all the way up", (double)frame.magnitude_max,
                (double)max * 16.0, 0.01);
    check_close("and on average", (double)frame.magnitude_mean,
                (double)mean * 16.0, 0.01);
    for (k = 0; k < 256; k++) {
        check_msg(frame.i_samples[k] == narrow_i[k] * 16.0f,
                  "sample %d of I is not the same signal: %.4f against "
                  "%.4f\n", k, frame.i_samples[k], narrow_i[k] * 16.0f);
        check_msg(frame.q_samples[k] == narrow_q[k] * 16.0f,
                  "sample %d of Q is not the same signal: %.4f against "
                  "%.4f\n", k, frame.q_samples[k], narrow_q[k] * 16.0f);
    }
}


/*
 * Invalidation throws the spectrum away and keeps the samples.
 *
 * A retune makes every bin a measurement of a different span. The samples are
 * a different matter: they are what arrived, and a decoder part-way through a
 * block is entitled to finish, so this asserts the asymmetry rather than
 * trusting the comment.
 */
static void test_invalidation_keeps_the_samples(void) {
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 40.0, 0.0, 0.0, 16384);
    float first;

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_SIZE);
    check_int("a block", signal_frame_process(&frame, &in, NULL), 1);
    first = frame.i_samples[7];

    signal_frame_invalidate(&frame);
    check_int("the spectrum is gone", frame.spectrum_ready, 0);
    check_int("and so is its hold", frame.spectrum_peak_ready, 0);
    check_true("but the samples are still there", frame.have_samples == 1);
    check_true("unchanged", frame.i_samples[7] == first);
    check_size("and there are still as many", frame.pair_count, 16384);

    /* And the next block rebuilds both rather than accumulating onto a hold
       gathered over a band the receiver has left. */
    check_int("the next block", signal_frame_process(&frame, &in, NULL), 1);
    check_int("brings the spectrum back", frame.spectrum_ready, 1);
    check_int("with a hold started afresh", frame.spectrum_peak_ready, 1);

    signal_frame_invalidate(NULL);   /* must not fall over */
}

/*
 * The hold falls towards the floor and stops there.
 *
 * The rate is the caller's -- how long a transient stays legible is a display
 * preference -- and the walk is the frame's. Before this had a name it was a
 * loop in `view_scope` reaching into these bins.
 */
static void test_the_hold_decays(void) {
    struct signal_frame_input in;
    size_t used = fill_tone(0.10, 60.0, 0.0, 0.0, 16384);
    int bin, peak_bin = 0;
    float before;

    memset(&frame, 0, sizeof(frame));
    in = an_input(used, SDR_DSP_FFT_SIZE);
    in.now = 10.0;
    check_int("a block", signal_frame_process(&frame, &in, NULL), 1);
    for (bin = 0; bin < frame.spectrum_bins; bin++)
        if (frame.spectrum_peak[bin] > frame.spectrum_peak[peak_bin])
            peak_bin = bin;
    before = frame.spectrum_peak[peak_bin];

    signal_frame_decay_peak(&frame, 12.0, 5.0f);
    check_close("two seconds at 5 dB a second is ten dB off",
                (double)frame.spectrum_peak[peak_bin],
                (double)before - 10.0, 0.01);
    check_close("and the clock moved with it", frame.spectrum_peak_time, 12.0,
                1e-9);

    signal_frame_decay_peak(&frame, 11.0, 5.0f);
    check_close("time going backwards decays nothing",
                (double)frame.spectrum_peak[peak_bin],
                (double)before - 10.0, 0.01);

    signal_frame_decay_peak(&frame, 1e6, 5.0f);
    check_close("and it stops at the floor rather than falling through it",
                (double)frame.spectrum_peak[peak_bin],
                (double)SDR_DSP_DBFS_FLOOR, 0.01);

    /* With no hold to decay, the clock still moves: otherwise the first decay
       after one starts would charge it for all the time it did not exist. */
    memset(&frame, 0, sizeof(frame));
    signal_frame_decay_peak(&frame, 42.0, 5.0f);
    check_close("an absent hold still keeps its clock", frame.spectrum_peak_time,
                42.0, 1e-9);
    signal_frame_decay_peak(NULL, 0.0, 1.0f);   /* must not fall over */
}

int main(void) {
    test_a_block_becomes_a_frame();
    test_dc_removal_never_touches_the_decoders_samples();
    test_a_new_geometry_resets_the_hold_and_reports_it();
    test_the_hold_keeps_the_loudest();
    test_an_impossible_size_falls_back();
    test_a_short_block_still_has_samples();
    test_refusals();
    test_a_wider_container_is_the_same_frame();
    test_invalidation_keeps_the_samples();
    test_the_hold_decays();
    return check_report("one block, converted and measured");
}
