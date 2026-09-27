/*
 * Short Range Device (SRD) technology DSP module.
 *
 * Implements On-Off Keying (OOK) burst discovery, carrier refinement,
 * envelope demodulation, run-length extraction, chip period recovery,
 * and Manchester decoding for 430-440 MHz ISM devices.
 *
 * Links -lm only (ADR-0001, ADR-0012).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdr_dsp.h"
#include "signal_probe.h"
#include "srd_dsp.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int compare_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

static int compare_float(const void *a, const void *b) {
    float x = *(const float *)a, y = *(const float *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/*
 * Analyze a single FFT chunk: return best bin frequency (signed Hz from DC)
 * and height (dB) of strongest bin over the median bin.
 */
static int scan_chunk_peak(struct sdr_dsp *dsp, const float *i_chunk,
                           const float *q_chunk, double sample_rate,
                           float full_scale, double *peak_hz_out,
                           double *over_floor_db_out) {
    float scan_avg[SRD_SCAN_SIZE];
    float scan_max[SRD_SCAN_SIZE];
    float sorted[SRD_SCAN_SIZE];
    float best = -1e9f, median;
    int k, best_k = 0;

    if (sdr_dsp_spectrum(dsp, i_chunk, q_chunk, SRD_SCAN_SIZE, SRD_SCAN_SIZE,
                         full_scale, scan_avg, scan_max) <= 0)
        return 0;

    for (k = 0; k < SRD_SCAN_SIZE; k++) {
        sorted[k] = scan_avg[k];
        if (scan_avg[k] > best) {
            best = scan_avg[k];
            best_k = k;
        }
    }
    qsort(sorted, SRD_SCAN_SIZE, sizeof(sorted[0]), compare_float);
    median = sorted[SRD_SCAN_SIZE / 2];

    *peak_hz_out = (double)(best_k - SRD_SCAN_SIZE / 2) * sample_rate /
                   (double)SRD_SCAN_SIZE;
    *over_floor_db_out = (double)(best - median);
    return 1;
}

int srd_find_transmissions(const float *i_samples, const float *q_samples,
                           size_t pair_count, double sample_rate,
                           float full_scale, double bar_db, double gap_seconds,
                           struct srd_transmission *out, size_t max_out) {
    struct sdr_dsp dsp;
    double chunk_seconds;
    size_t num_chunks, chunk;
    size_t found_count = 0;
    int in_run = 0;
    size_t run_start_chunk = 0, last_busy_chunk = 0;
    double peaks[4096];
    size_t peak_count = 0;

    if (!i_samples || !q_samples || !out || max_out == 0 ||
        pair_count < SRD_SCAN_SIZE || !(sample_rate > 0.0))
        return 0;

    if (!(full_scale > 0.0f))
        full_scale = 127.5f;
    if (bar_db <= 0.0)
        bar_db = SRD_BUSY_BAR_DB_DEFAULT;
    if (gap_seconds <= 0.0)
        gap_seconds = SRD_GAP_SECONDS_DEFAULT;

    sdr_dsp_init(&dsp);
    chunk_seconds = (double)SRD_SCAN_SIZE / sample_rate;
    num_chunks = pair_count / SRD_SCAN_SIZE;

    for (chunk = 0; chunk < num_chunks; chunk++) {
        double peak_hz, over_db;
        double at_s = (double)chunk * chunk_seconds;
        const float *ci = i_samples + chunk * SRD_SCAN_SIZE;
        const float *cq = q_samples + chunk * SRD_SCAN_SIZE;

        if (!scan_chunk_peak(&dsp, ci, cq, sample_rate, full_scale,
                             &peak_hz, &over_db))
            continue;

        if (over_db >= bar_db) {
            double gap = in_run ? (at_s - (double)last_busy_chunk * chunk_seconds)
                                : 0.0;
            if (!in_run || gap > gap_seconds) {
                /* Close previous transmission if we were in one */
                if (in_run && found_count < max_out) {
                    struct srd_transmission *prev = &out[found_count];
                    prev->offset_pairs = run_start_chunk * SRD_SCAN_SIZE;
                    prev->pair_count = (last_busy_chunk - run_start_chunk + 1) *
                                       SRD_SCAN_SIZE;
                    prev->start_seconds = (double)run_start_chunk * chunk_seconds;
                    prev->duration_seconds = (double)(last_busy_chunk - run_start_chunk + 1) *
                                             chunk_seconds;
                    if (peak_count > 0) {
                        qsort(peaks, peak_count, sizeof(peaks[0]), compare_double);
                        prev->coarse_hz = peaks[peak_count / 2];
                    } else {
                        prev->coarse_hz = 0.0;
                    }
                    found_count++;
                }
                if (found_count >= max_out)
                    break;
                run_start_chunk = chunk;
                peak_count = 0;
                in_run = 1;
            }
            if (peak_count < sizeof(peaks) / sizeof(peaks[0]))
                peaks[peak_count++] = peak_hz;
            last_busy_chunk = chunk;
        }
    }

    if (in_run && found_count < max_out) {
        struct srd_transmission *tx = &out[found_count];
        tx->offset_pairs = run_start_chunk * SRD_SCAN_SIZE;
        tx->pair_count = (last_busy_chunk - run_start_chunk + 1) * SRD_SCAN_SIZE;
        tx->start_seconds = (double)run_start_chunk * chunk_seconds;
        tx->duration_seconds = (double)(last_busy_chunk - run_start_chunk + 1) *
                               chunk_seconds;
        if (peak_count > 0) {
            qsort(peaks, peak_count, sizeof(peaks[0]), compare_double);
            tx->coarse_hz = peaks[peak_count / 2];
        } else {
            tx->coarse_hz = 0.0;
        }
        found_count++;
    }

    /* Refine carrier for each found transmission using signal_find_carrier */
    for (size_t i = 0; i < found_count; i++) {
        struct srd_transmission *tx = &out[i];
        struct signal_carrier carrier;
        const float *ti = i_samples + tx->offset_pairs;
        const float *tq = q_samples + tx->offset_pairs;
        double search_span = 10000.0; /* coarse peak is within one bin (1953 Hz) */

        tx->chunks = (int)(tx->pair_count / SRD_SCAN_SIZE);
        tx->carrier_hz = tx->coarse_hz;
        tx->carrier_over_noise_db = 0.0;
        tx->carrier_power_fraction = 0.0;

        size_t probe_pairs = tx->pair_count > 65536 ? 65536 : tx->pair_count;

        if (tx->pair_count >= 1024 &&
            signal_find_carrier(ti, tq, probe_pairs, sample_rate,
                                tx->coarse_hz - search_span,
                                tx->coarse_hz + search_span,
                                50000.0, 50000.0, &carrier)) {
            tx->carrier_hz = carrier.offset_hz;
            tx->carrier_over_noise_db = carrier.carrier_over_noise_db;
            tx->carrier_power_fraction = carrier.carrier_power_fraction;
        }

        tx->modulation = srd_classify_modulation(ti, tq, probe_pairs,
                                                  sample_rate, tx->carrier_hz,
                                                  full_scale);
    }

    return (int)found_count;
}

size_t srd_demodulate_envelope(const float *i_samples, const float *q_samples,
                               size_t pair_count, double sample_rate,
                               double carrier_hz, float *envelope_out,
                               size_t max_out, double *work_rate_out) {
    int decimate;
    double w, step_re, step_im;
    double pr = 1.0, pi = 0.0;
    double acc_re = 0.0, acc_im = 0.0;
    size_t n, in_dec = 0, written = 0;

    if (!i_samples || !q_samples || !envelope_out || max_out == 0 ||
        pair_count == 0 || !(sample_rate > 0.0))
        return 0;

    decimate = (int)(sample_rate / SRD_WORK_RATE_HZ);
    if (decimate < 1)
        decimate = 1;

    if (work_rate_out)
        *work_rate_out = sample_rate / (double)decimate;

    w = -2.0 * M_PI * carrier_hz / sample_rate;
    step_re = cos(w);
    step_im = sin(w);

    for (n = 0; n < pair_count && written < max_out; n++) {
        double next_pr = pr * step_re - pi * step_im;
        double next_pi = pi * step_re + pr * step_im;
        acc_re += (double)i_samples[n] * pr - (double)q_samples[n] * pi;
        acc_im += (double)i_samples[n] * pi + (double)q_samples[n] * pr;
        pr = next_pr;
        pi = next_pi;

        /* Renormalize phasor occasionally against numerical drift */
        if ((n & 0xffff) == 0xffff) {
            double m = sqrt(pr * pr + pi * pi);
            if (m > 0.0) {
                pr /= m;
                pi /= m;
            }
        }

        if (++in_dec == (size_t)decimate) {
            double re = acc_re / (double)decimate;
            double im = acc_im / (double)decimate;
            envelope_out[written++] = (float)sqrt(re * re + im * im);
            acc_re = 0.0;
            acc_im = 0.0;
            in_dec = 0;
        }
    }

    return written;
}

enum srd_modulation srd_classify_modulation(const float *i_samples,
                                            const float *q_samples,
                                            size_t pair_count,
                                            double sample_rate,
                                            double carrier_hz,
                                            float full_scale) {
    struct signal_envelope env;

    if (!(full_scale > 0.0f))
        full_scale = 127.5f;

    if (!i_samples || !q_samples || pair_count == 0 ||
        !signal_envelope_stats(i_samples, q_samples, pair_count, sample_rate,
                               carrier_hz, SRD_CHANNEL_HZ_DEFAULT, full_scale,
                               &env) ||
        !env.found)
        return SRD_MOD_OOK;

    return (env.frequency_spread_hz >= SRD_FSK_FREQ_SPREAD_MIN_HZ) ?
           SRD_MOD_FSK2 : SRD_MOD_OOK;
}

size_t srd_demodulate_fsk(const float *i_samples, const float *q_samples,
                          size_t pair_count, double sample_rate,
                          double carrier_hz, float *discriminator_out,
                          size_t max_out, double *work_rate_out) {
    int decimate;
    double w, step_re, step_im;
    double pr = 1.0, pi = 0.0;
    double bb_prev_re = 0.0, bb_prev_im = 0.0;
    double acc_re = 0.0, acc_im = 0.0;
    size_t n, in_dec = 0, written = 0;
    int have_prev = 0;

    if (!i_samples || !q_samples || !discriminator_out || max_out == 0 ||
        pair_count == 0 || !(sample_rate > 0.0))
        return 0;

    decimate = (int)(sample_rate / SRD_WORK_RATE_HZ);
    if (decimate < 1)
        decimate = 1;

    if (work_rate_out)
        *work_rate_out = sample_rate / (double)decimate;

    w = -2.0 * M_PI * carrier_hz / sample_rate;
    step_re = cos(w);
    step_im = sin(w);

    for (n = 0; n < pair_count && written < max_out; n++) {
        double next_pr = pr * step_re - pi * step_im;
        double next_pi = pi * step_re + pr * step_im;
        double bb_re = (double)i_samples[n] * pr - (double)q_samples[n] * pi;
        double bb_im = (double)i_samples[n] * pi + (double)q_samples[n] * pr;

        pr = next_pr;
        pi = next_pi;
        if ((n & 0xffff) == 0xffff) {
            double m = sqrt(pr * pr + pi * pi);
            if (m > 0.0) {
                pr /= m;
                pi /= m;
            }
        }

        /*
         * Discriminator: s[n] * conj(s[n-1]). Its angle is the phase step
         * between consecutive baseband samples, i.e. instantaneous
         * frequency. Summing these products as vectors before taking one
         * atan2() per decimation window averages phase safely -- averaging
         * per-sample *angles* directly would wrap incorrectly whenever a
         * window straddles +/-pi.
         */
        if (have_prev) {
            acc_re += bb_re * bb_prev_re + bb_im * bb_prev_im;
            acc_im += bb_im * bb_prev_re - bb_re * bb_prev_im;
        }
        bb_prev_re = bb_re;
        bb_prev_im = bb_im;
        have_prev = 1;

        if (++in_dec == (size_t)decimate) {
            double angle = (acc_re != 0.0 || acc_im != 0.0) ?
                           atan2(acc_im, acc_re) : 0.0;
            discriminator_out[written++] =
                (float)(angle * sample_rate / (2.0 * M_PI));
            acc_re = 0.0;
            acc_im = 0.0;
            in_dec = 0;
        }
    }

    return written;
}

double srd_discriminator_threshold(const float *discriminator, size_t count) {
    float *sorted;
    double threshold;

    if (!discriminator || count == 0)
        return 0.0;

    sorted = malloc(count * sizeof(float));
    if (!sorted)
        return 0.0;
    memcpy(sorted, discriminator, count * sizeof(float));
    qsort(sorted, count, sizeof(float), compare_float);

    if (count >= 10) {
        /*
         * Midpoint between 10th and 90th percentile separates the two FSK tone
         * clusters safely even when the symbol distribution is asymmetric (more
         * zeros than ones or long unmodulated lead-ins).
         */
        float p10 = sorted[count / 10];
        float p90 = sorted[count * 9 / 10];
        threshold = 0.5 * (p10 + p90);
    } else {
        threshold = (count % 2 == 0) ?
                    0.5 * (sorted[count / 2 - 1] + sorted[count / 2]) :
                    sorted[count / 2];
    }

    free(sorted);
    return threshold;
}

double srd_envelope_threshold(const float *envelope, size_t count) {
    double max_amp = 0.0;
    if (!envelope || count == 0)
        return 0.0;

    for (size_t i = 0; i < count; i++) {
        if (envelope[i] > max_amp)
            max_amp = envelope[i];
    }
    return 0.5 * max_amp;
}

size_t srd_extract_runs(const float *envelope, size_t count, double work_rate,
                        double threshold, struct srd_run *runs_out,
                        size_t max_runs) {
    int state;
    size_t run_len = 1;
    size_t runs_written = 0;

    if (!envelope || count == 0 || !runs_out || max_runs == 0 ||
        !(work_rate > 0.0))
        return 0;

    state = (envelope[0] >= threshold);

    for (size_t i = 1; i < count; i++) {
        int now = (envelope[i] >= threshold);
        if (now == state) {
            run_len++;
            continue;
        }
        if (runs_written < max_runs) {
            runs_out[runs_written].state = state;
            runs_out[runs_written].length_samples = run_len;
            runs_out[runs_written].duration_seconds = (double)run_len / work_rate;
            runs_written++;
        }
        state = now;
        run_len = 1;
    }

    if (runs_written < max_runs) {
        runs_out[runs_written].state = state;
        runs_out[runs_written].length_samples = run_len;
        runs_out[runs_written].duration_seconds = (double)run_len / work_rate;
        runs_written++;
    }

    return runs_written;
}

#define BUCKET_US 10.0
#define NUM_BUCKETS 1000 /* up to 10 ms */

/* How many candidate modes are scored. A run-length histogram of a real
   transmission has a handful of populations -- 1T, 2T, a delimiter, a
   trailing gap -- and a noisy one has the decay of its own threshold
   crossings on top. Sixteen is past both and bounds the cost of scoring,
   which is a pass over the runs each. */
#define CHIP_CANDIDATES_MAX 16

double srd_chip_coverage(const struct srd_run *runs, size_t run_count,
                         double chip_period) {
    double total = 0.0, explained = 0.0;
    size_t i;

    if (!runs || run_count == 0 || !(chip_period > 0.0))
        return 0.0;

    for (i = 0; i < run_count; i++) {
        double d = runs[i].duration_seconds;
        double chips = d / chip_period;
        long whole = (long)(chips + 0.5);

        total += d;
        if ((whole == 1 || whole == 2) &&
            fabs(chips - (double)whole) <= SRD_CHIP_ROUND_TOLERANCE)
            explained += d;
    }

    return total > 0.0 ? explained / total : 0.0;
}

double srd_chip_period(const struct srd_run *runs, size_t run_count) {
    int hist[NUM_BUCKETS];
    int candidate[CHIP_CANDIDATES_MAX];
    size_t candidates = 0;
    int max_bin_count = 0, min_thresh;
    double best_period = 0.0, best_coverage = 0.0, sample_s = 0.0;
    int b;
    size_t c;

    if (!runs || run_count < 4)
        return 0.0;

    memset(hist, 0, sizeof(hist));

    /*
     * Every run is binned, including the short ones.
     *
     * There used to be a 20 us floor here, and it was the fault this
     * function had: it is an absolute number, chosen when the only known
     * transmitter had 500 us chips, and it is a third of a chip at the
     * 64.2 us the 2-FSK remote uses. Worse, it cannot do the job it was
     * given -- a discriminator crossing its threshold on noise produces a
     * decaying population of short runs, and excluding the runs below 20 us
     * leaves that population's *tail* as the first significant mode. On
     * transmission 0 of testfiles/srd_remote_control_fsk.bin that tail
     * held 743 runs against about 90 real ones and the answer came back
     * 23.01 us for a signal whose runs sit at 500 and 1000.
     *
     * What replaces it is not a better floor but a different question,
     * below: whether a candidate explains the signal.
     */
    for (c = 0; c < run_count; c++) {
        double dur_us = runs[c].duration_seconds * 1e6;

        if (dur_us >= 0.0 && dur_us < (double)NUM_BUCKETS * BUCKET_US) {
            b = (int)(dur_us / BUCKET_US);
            if (b >= 0 && b < NUM_BUCKETS)
                hist[b]++;
        }
    }

    for (b = 0; b < NUM_BUCKETS; b++)
        if (hist[b] > max_bin_count)
            max_bin_count = hist[b];
    if (max_bin_count < 3)
        return 0.0;

    min_thresh = max_bin_count / 8;
    if (min_thresh < 3)
        min_thresh = 3;

    /*
     * Every population in the histogram is a candidate, not just the first
     * one. Taking the first significant mode is what made a noise tail beat
     * a real chip population that was sitting in the same histogram three
     * hundred microseconds further along.
     */
    for (b = 0; b < NUM_BUCKETS; b++) {
        size_t slot;

        if (hist[b] < min_thresh)
            continue;
        if (b > 0 && hist[b] < hist[b - 1])
            continue;
        if (b + 1 < NUM_BUCKETS && hist[b] < hist[b + 1])
            continue;

        /*
         * Keep the largest modes rather than the first ones. The cap has to
         * be resolved by size and not by position, or a histogram whose low
         * bins are busy could crowd out the real chip population further
         * along -- which is the shape of this function's original fault, at
         * one remove.
         */
        if (candidates < CHIP_CANDIDATES_MAX) {
            slot = candidates++;
        } else {
            size_t smallest = 0;
            for (slot = 1; slot < candidates; slot++)
                if (hist[candidate[slot]] < hist[candidate[smallest]])
                    smallest = slot;
            if (hist[candidate[smallest]] >= hist[b])
                continue;
            slot = smallest;
        }
        candidate[slot] = b;
    }

    if (candidates == 0)
        return 0.0;

    /*
     * A Manchester run is one chip or two and nothing else -- that is the
     * whole of the code -- so a correct period accounts for very nearly all
     * of the transmission's *time*, and a wrong one accounts for whatever
     * happens to land in its windows. Time rather than count, because a
     * threshold crossing on noise is a short run and there can be thousands
     * of them: by count they are the population, by duration they are a
     * rounding error.
     *
     * Measured over 39 transmissions in the three SRD remote control captures: 38 of
     * them score 91.1% to 99.8% at their own period, wakeup and data alike,
     * OOK and 2-FSK alike. The one that does not is the transmission this
     * was written for, at 24.1%. See SRD_CHIP_COVERAGE_MIN.
     */
    /*
     * One sample of the demodulated signal, taken from the runs themselves:
     * srd_extract_runs() sets duration_seconds to length_samples over the
     * work rate, so any run with a length gives the sample period back. The
     * longest run gives it most precisely, its rounding being one part in
     * its own length.
     */
    {
        size_t longest = 0, i;

        for (i = 0; i < run_count; i++)
            if (runs[i].length_samples > runs[longest].length_samples)
                longest = i;
        if (runs[longest].length_samples > 0)
            sample_s = runs[longest].duration_seconds /
                       (double)runs[longest].length_samples;
    }

    for (c = 0; c < candidates; c++) {
        double center_s = ((double)candidate[c] + 0.5) * BUCKET_US * 1e-6;
        double sum = 0.0, period, coverage;
        size_t near = 0, i;

        for (i = 0; i < run_count; i++) {
            double d = runs[i].duration_seconds;
            if (fabs(d - center_s) <= 0.25 * center_s) {
                sum += d;
                near++;
            }
        }

        period = near > 0 ? sum / (double)near : center_s;

        /*
         * A period the sampling grid could have produced on its own says
         * nothing about the transmitter -- see SRD_CHIP_MIN_SAMPLES.
         */
        if (sample_s > 0.0 && period < SRD_CHIP_MIN_SAMPLES * sample_s)
            continue;

        coverage = srd_chip_coverage(runs, run_count, period);
        if (coverage > best_coverage) {
            best_coverage = coverage;
            best_period = period;
        }
    }

    /*
     * Refusing is an answer. A histogram always has a mode, so this used to
     * return a number whatever it had been shown, and a wrong chip period
     * does not read as silence any more: srd_extract_frames() reports every
     * legal Manchester stretch now, so a period that explains nothing still
     * produces bytes that are not a frame of anything.
     */
    if (best_coverage < SRD_CHIP_COVERAGE_MIN)
        return 0.0;

    return best_period;
}

size_t srd_runs_to_chips(const struct srd_run *runs, size_t run_count,
                         double chip_period, uint8_t *chips_out,
                         size_t max_chips) {
    size_t chips_written = 0;

    if (!runs || run_count == 0 || !(chip_period > 0.0) ||
        !chips_out || max_chips == 0)
        return 0;

    for (size_t i = 0; i < run_count && chips_written < max_chips; i++) {
        double dur = runs[i].duration_seconds;
        int n_chips = (int)round(dur / chip_period);
        if (n_chips < 1)
            n_chips = 1;

        uint8_t val = runs[i].state ? 1 : 0;
        for (int k = 0; k < n_chips && chips_written < max_chips; k++) {
            chips_out[chips_written++] = val;
        }
    }

    return chips_written;
}

int srd_manchester_decode(const uint8_t *chips, size_t chip_count,
                          enum srd_manchester_polarity polarity,
                          struct srd_manchester_decode *out) {
    size_t errors[2] = {0, 0};
    size_t pairs[2] = {0, 0};
    int best_phase;

    if (!chips || chip_count < 2 || !out)
        return 0;

    memset(out, 0, sizeof(*out));

    /* Evaluate violations in both phase 0 and phase 1 */
    for (int p = 0; p < 2; p++) {
        pairs[p] = (chip_count - (size_t)p) / 2;
        for (size_t k = 0; k < pairs[p]; k++) {
            uint8_t c0 = chips[p + 2 * k];
            uint8_t c1 = chips[p + 2 * k + 1];
            if (c0 == c1)
                errors[p]++;
        }
    }

    /* Pick the phase with fewer violations */
    best_phase = (errors[0] <= errors[1]) ? 0 : 1;
    out->phase = best_phase;

    size_t num_pairs = pairs[best_phase];
    if (num_pairs > SRD_MAX_BITS)
        num_pairs = SRD_MAX_BITS;

    for (size_t k = 0; k < num_pairs; k++) {
        uint8_t c0 = chips[best_phase + 2 * k];
        uint8_t c1 = chips[best_phase + 2 * k + 1];

        if (c0 == c1) {
            out->error_count++;
            out->bits[k] = 0;
        } else if (c0 == 1 && c1 == 0) {
            /* 10 */
            out->bits[k] = (polarity == SRD_MANCHESTER_THOMAS) ? 0 : 1;
        } else {
            /* 01 */
            out->bits[k] = (polarity == SRD_MANCHESTER_THOMAS) ? 1 : 0;
        }
    }
    out->bit_count = num_pairs;
    return 1;
}

int srd_manchester_decode_both(const uint8_t *chips, size_t chip_count,
                               struct srd_manchester_decode *thomas_out,
                               struct srd_manchester_decode *ieee_out) {
    int ok = 1;
    if (thomas_out)
        ok &= srd_manchester_decode(chips, chip_count, SRD_MANCHESTER_THOMAS,
                                    thomas_out);
    if (ieee_out)
        ok &= srd_manchester_decode(chips, chip_count, SRD_MANCHESTER_IEEE,
                                    ieee_out);
    return ok;
}

size_t srd_pack_bits(const uint8_t *bits, size_t bit_count,
                     uint8_t *bytes_out, size_t max_bytes) {
    size_t bytes = 0;
    if (!bits || !bytes_out || max_bytes == 0)
        return 0;

    size_t full_bytes = bit_count / 8;
    if (full_bytes > max_bytes)
        full_bytes = max_bytes;

    for (size_t b = 0; b < full_bytes; b++) {
        uint8_t byte = 0;
        for (int bit = 0; bit < 8; bit++) {
            byte = (uint8_t)((byte << 1) | (bits[b * 8 + bit] & 1));
        }
        bytes_out[bytes++] = byte;
    }
    return bytes;
}
