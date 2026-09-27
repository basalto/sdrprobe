#ifndef SRD_DSP_H
#define SRD_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Short Range Device (SRD) technology DSP module.
 *
 * Designed for 430-440 MHz ISM / SRD allocation signals (remote controls, tyre-pressure
 * sensors, weather stations, doorbells) using On-Off Keying (OOK / ASK) or 2-FSK /
 * 2-GFSK, both Manchester encoded -- the two modulations measured off the air here
 * (testfiles/srd_remote_control_ook_*.bin and testfiles/srd_remote_control_fsk.bin), not
 * an assumption that every 433 MHz transmitter is one or the other.
 *
 * Pipeline:
 *   1. srd_find_transmissions() -- scans whole capture/buffer with FFT chunks to
 *      locate bursts in time and frequency, refining carrier via signal_find_carrier()
 *      and classifying OOK against 2-FSK via srd_classify_modulation().
 *   2. srd_demodulate_envelope() (OOK) or srd_demodulate_fsk() (2-FSK) -- mixes to
 *      baseband, filters/decimates to work rate, and extracts either the envelope
 *      amplitude or a signed frequency-deviation estimate. Both produce a signal
 *      that is high during one symbol and low during the other.
 *   3. srd_extract_runs() -- slices that signal at a threshold (half-scale for OOK,
 *      zero for FSK) to identify HIGH/LOW runs.
 *   4. srd_chip_period() -- recovers fundamental chip duration directly from the
 *      distribution of run lengths (not a blind symbol-rate search).
 *   5. srd_runs_to_chips() -- discretises runs into binary chips based on chip period.
 *   6. srd_manchester_decode() -- resolves chip alignment phase and decodes chips to
 *      bits with G.E. Thomas and IEEE 802.3 polarities.
 *
 * Links -lm only. No GUI, no receiver (ADR-0001, ADR-0012).
 */

#define SRD_SCAN_SIZE 1024
#define SRD_WORK_RATE_HZ 200000.0
#define SRD_CHANNEL_HZ_DEFAULT 50000.0
#define SRD_BUSY_BAR_DB_DEFAULT 25.0
#define SRD_GAP_SECONDS_DEFAULT 0.050

#define SRD_MAX_TRANSMISSIONS 32
#define SRD_MAX_RUNS 8192
#define SRD_MAX_CHIPS 16384
#define SRD_MAX_BITS 8192

/*
 * The allocation this module looks at: 430-440 MHz, which is the band plan's
 * own "70 cm amateur / 433 ISM" entry to the hertz. Short-range devices are
 * not confined to the 433.05-434.79 MHz sub-band a European reader expects
 * -- the surrounding ten megahertz carries telemetry, sensors and remotes
 * too, and the survey has always shown them.
 *
 * **A receiver cannot see the whole of it.** Ten megahertz needs 10 MS/s and
 * nothing here samples that fast, so this is deliberately *not* the "can the
 * whole span fit" test it used to be. It used to demand a tuning within a
 * megahertz of 434 on the argument that 433-435 is a 2 MHz span and the
 * receiver may be parked at either edge of it -- true of that sub-band, and
 * it refused every other part of the allocation, including frequencies where
 * this receiver has recorded transmissions. The question now is the simpler
 * one it should always have been: is the receiver pointed **inside** the
 * allocation, and is it sampling fast enough for what it hears to be worth
 * demodulating. It decodes whatever slice it can see.
 *
 * SRD_CENTER_HZ stays the middle of the band and is what the view's retune
 * affordance offers, because somewhere is a better default than nowhere; it
 * is no longer a constraint.
 */
#define SRD_BAND_LOWER_HZ 430000000U
#define SRD_BAND_UPPER_HZ 440000000U
#define SRD_CENTER_HZ 434000000U
#define SRD_MIN_SAMPLE_RATE 1000000U

/*
 * How far one press of a tuning arrow moves the centre, as a fraction of
 * what the receiver can hear at once.
 *
 * Half a span, so consecutive presses overlap by half and nothing can hide
 * in a seam between two tunings -- which is the failure a whole-span step
 * would have: a transmission straddling the boundary is half in each and
 * strong in neither. At 2 MS/s that is 1 MHz a press, so the ten megahertz
 * of the allocation is ten presses end to end and twenty with the overlap.
 *
 * A fraction of the span rather than a round number of hertz, because the
 * useful step is the one that tiles what the receiver sees, and that follows
 * the rate.
 */
#define SRD_TUNE_STEP_FRACTION 0.5

/*
 * Where an arrow press lands, clamped to the allocation.
 *
 * Clamped rather than free: this is the SRD view, srd_receiver_ready() is
 * its own statement of where it works, and walking off the end of the band
 * with an arrow gives a screen that can only say "no SRD signal expected".
 * The field beside the arrows takes any frequency the receiver can reach,
 * so nothing is unreachable -- the arrows are for walking the band, and the
 * field is for leaving it.
 *
 * Returns the current tuning unchanged when it is already hard against the
 * edge it is being pushed towards, which is what lets a caller tell a step
 * that moved from one that did not.
 */
static inline uint32_t srd_tune_step(uint32_t frequency_hz,
                                     uint32_t sample_rate, int direction) {
    double step = (double)sample_rate * SRD_TUNE_STEP_FRACTION;
    double target = (double)frequency_hz + (double)direction * step;

    if (step <= 0.0 || direction == 0)
        return frequency_hz;
    if (target < (double)SRD_BAND_LOWER_HZ)
        target = (double)SRD_BAND_LOWER_HZ;
    if (target > (double)SRD_BAND_UPPER_HZ)
        target = (double)SRD_BAND_UPPER_HZ;
    return (uint32_t)(target + 0.5);
}

/*
 * What kind of thing a transmission is, not what it says. OOK/ASK carries
 * its bits in the envelope and nowhere else, so an envelope threshold reads
 * it directly; 2-FSK carries them in which of two tones is on, and an
 * envelope threshold reads nothing at all, because a constant-envelope
 * signal has no envelope to threshold.
 */
enum srd_modulation {
    SRD_MOD_OOK  = 0, /* on-off keying / ASK: srd_demodulate_envelope() */
    SRD_MOD_FSK2 = 1  /* 2-FSK / 2-GFSK: srd_demodulate_fsk() */
};

/*
 * Measured on the two real captures this module has: every burst of
 * testfiles/srd_remote_control_ook_a.bin (OOK) reads an instantaneous-frequency
 * scatter (signal_envelope_stats()'s frequency_spread_hz) under 1.5 kHz,
 * because nothing there moves the carrier -- an OOK transmitter's
 * frequency is whatever residual phase noise it has. Every burst of
 * testfiles/srd_remote_control_fsk.bin (2-FSK, +/-15.6 kHz deviation)
 * reads at least 3.9 kHz, wakeup and data alike, because toggling between
 * two tones 31 kHz apart is exactly what this statistic measures. 3000 Hz
 * sits with a factor of 2 margin under the highest OOK reading and clear
 * of the lowest FSK one, and does not depend on burst length or SNR --
 * the captures span 48 dB to 75 dB over their own floor and the two
 * populations do not overlap once in either.
 */
#define SRD_FSK_FREQ_SPREAD_MIN_HZ 3000.0

/*
 * Whether the receiver is where the SRD band is. Outside 430-440 MHz, or too
 * slow for the channel widths this module measures, nothing that arrives can
 * be a SRD remote control or a sensor -- and the view says so with a retune affordance
 * instead of decoding silence and reporting "no frames decoded yet" with
 * nothing to say why.
 */
static inline int srd_receiver_ready(uint32_t frequency_hz,
                                     uint32_t sample_rate) {
    return frequency_hz >= SRD_BAND_LOWER_HZ &&
           frequency_hz <= SRD_BAND_UPPER_HZ &&
           sample_rate >= SRD_MIN_SAMPLE_RATE;
}

struct srd_transmission {
    size_t offset_pairs;
    size_t pair_count;
    double start_seconds;
    double duration_seconds;
    double coarse_hz;              /* peak bin from spectrum scan */
    double carrier_hz;             /* refined carrier from signal_find_carrier */
    double carrier_over_noise_db;  /* carrier dB over local noise floor */
    double carrier_power_fraction; /* standing power fraction */
    int chunks;                    /* number of busy scan chunks */
    enum srd_modulation modulation; /* OOK or 2-FSK, from srd_classify_modulation() */
};

struct srd_run {
    int state;               /* 1 = high (carrier on), 0 = low (carrier off) */
    size_t length_samples;   /* run length in samples at work rate */
    double duration_seconds; /* run duration in seconds */
};

enum srd_manchester_polarity {
    SRD_MANCHESTER_THOMAS = 0, /* 10 -> 0, 01 -> 1 (G.E. Thomas) */
    SRD_MANCHESTER_IEEE   = 1  /* 01 -> 0, 10 -> 1 (IEEE 802.3) */
};

struct srd_manchester_decode {
    size_t bit_count;           /* number of bits decoded */
    size_t error_count;         /* Manchester violations (00 or 11 chip pairs) */
    int phase;                  /* 0 or 1: chip alignment offset */
    uint8_t bits[SRD_MAX_BITS]; /* decoded bits (0 or 1) */
};

/*
 * Scan a sample buffer in chunks of SRD_SCAN_SIZE (1024 pairs) using FFT
 * spectra to find transmissions that stand at least `bar_db` over their median
 * bin. Group busy chunks separated by no more than `gap_seconds` into single
 * transmissions.
 *
 * For each transmission, refines the carrier offset using signal_find_carrier().
 *
 * If full_scale <= 0.0f, defaults to 127.5f.
 * If bar_db <= 0.0, defaults to SRD_BUSY_BAR_DB_DEFAULT (25.0 dB).
 * If gap_seconds <= 0.0, defaults to SRD_GAP_SECONDS_DEFAULT (0.050 s).
 *
 * Returns number of transmissions found and written into `out` (up to max_out).
 */
int srd_find_transmissions(const float *i_samples, const float *q_samples,
                           size_t pair_count, double sample_rate,
                           float full_scale, double bar_db, double gap_seconds,
                           struct srd_transmission *out, size_t max_out);

/*
 * Mix a window of samples to `carrier_hz` and boxcar-decimate to about
 * SRD_WORK_RATE_HZ (~200 kS/s), computing the envelope magnitude
 * sqrt(I^2 + Q^2) for each decimated sample.
 *
 * Sets `*work_rate_out` to the actual decimated sample rate.
 * Returns number of envelope samples written to `envelope_out` (up to max_out).
 */
size_t srd_demodulate_envelope(const float *i_samples, const float *q_samples,
                               size_t pair_count, double sample_rate,
                               double carrier_hz, float *envelope_out,
                               size_t max_out, double *work_rate_out);

/*
 * Whether a transmission's envelope is amplitude-keyed or its frequency is.
 * Isolates the transmission to SRD_CHANNEL_HZ_DEFAULT around `carrier_hz`
 * with signal_envelope_stats() and reads frequency_spread_hz against
 * SRD_FSK_FREQ_SPREAD_MIN_HZ -- see that constant's comment for the two real
 * captures this was measured against. Falls back to SRD_MOD_OOK (the
 * original assumption) when the isolated channel is too short to measure.
 */
enum srd_modulation srd_classify_modulation(const float *i_samples,
                                            const float *q_samples,
                                            size_t pair_count,
                                            double sample_rate,
                                            double carrier_hz,
                                            float full_scale);

/*
 * Mix a window of samples to `carrier_hz` and boxcar-average an
 * instantaneous-frequency discriminator to about SRD_WORK_RATE_HZ,
 * producing a signed frequency-deviation estimate in Hz for each decimated
 * sample -- positive for one FSK tone, negative for the other.
 *
 * Each raw sample's discriminator is the angle between it and its
 * predecessor (arg(s[n] * conj(s[n-1]))); rather than average that angle
 * directly, which wraps unsafely across a decimation window, the *products*
 * are summed as vectors and the angle is taken once at the end -- the same
 * trick a coherent accumulator uses to average phase safely.
 *
 * A frequency estimate does not cross zero symmetrically, though: carrier
 * refinement upstream (srd_find_transmissions()'s call to
 * signal_find_carrier()) locks onto whichever FSK tone is strongest inside
 * the transmission window, not the true channel centre between the two
 * tones, so one tone reads near 0 Hz after mixdown and the other reads near
 * minus twice the deviation. **Do not pass a fixed 0.0 threshold to
 * srd_extract_runs()** -- it was tried and cost a burst's worth of spurious
 * runs, because near-zero samples jitter across zero from noise alone.
 * srd_discriminator_threshold() is the slicer this output needs.
 *
 * Sets `*work_rate_out` to the actual decimated sample rate.
 * Returns number of samples written to `discriminator_out` (up to max_out).
 */
size_t srd_demodulate_fsk(const float *i_samples, const float *q_samples,
                          size_t pair_count, double sample_rate,
                          double carrier_hz, float *discriminator_out,
                          size_t max_out, double *work_rate_out);

/*
 * A slicing threshold for srd_demodulate_fsk()'s output, to feed
 * srd_extract_runs(). The median of the discriminator array, not its
 * mean or its min/max midpoint:
 *
 *   - 0.0 fails because the two tones are not symmetric about it (see
 *     srd_demodulate_fsk()'s comment) -- one tone reads near 0 Hz and the
 *     other near minus twice the deviation, so 0.0 sits inside the upper
 *     tone's own noise rather than between the two.
 *   - The min/max midpoint fails on real captures because the coarse
 *     transmission window (from srd_find_transmissions(), 512 us FFT
 *     chunks) can include a noisy lead-in before the real signal starts;
 *     transient discriminator outliers there run to hundreds of kHz and
 *     drag the midpoint off-centre.
 *   - The median is robust to both: half the samples sit on each tone (or
 *     close to it) whenever the burst is not almost entirely lead-in noise,
 *     and a handful of large outliers cannot move it.
 *
 * Measured on the 2-FSK SRD remote control capture's "long wakeup" burst (a single
 * repeating tone, no data): the median threshold recovers a ~64.2 us chip
 * period with zero Manchester violations, where 0.0 and the midpoint both
 * failed.
 *
 * **The data bursts read better than this comment used to claim.** It said
 * they carried a 10-13% bit error rate, established in a transcript and
 * reproducible nowhere; `make probe-srd` measures them, and at the recovered
 * period each data burst violates the Manchester code **8 or 9 times in
 * about 378 chips** -- under 5% of chip pairs -- with 164 consecutive bits
 * decoding cleanly out of about 189. That is a frame with a broken tail, not
 * a bit error rate, and srd_frame.h extracts it. What was actually missing
 * was an extractor that reports more than one frame per run stream.
 */
double srd_discriminator_threshold(const float *discriminator, size_t count);

/*
 * Compute a slicing threshold between the carrier-off floor and the carrier-on
 * peak. For high-SNR OOK, returns 0.5 * maximum envelope magnitude.
 */
double srd_envelope_threshold(const float *envelope, size_t count);

/*
 * Slice an envelope array at `threshold` and group consecutive identical states
 * into runs.
 *
 * Returns number of runs written to `runs_out` (up to max_runs).
 */
size_t srd_extract_runs(const float *envelope, size_t count, double work_rate,
                        double threshold, struct srd_run *runs_out,
                        size_t max_runs);

/*
 * How near a run has to sit to a whole number of chips to count as that
 * many, as a fraction of one chip. A quarter, which is what the mode
 * refinement already uses, and it is what separates 1T from 2T with the
 * widest margin available: 1.5T is the only run length a Manchester coder
 * cannot emit, so the two windows meet exactly there and neither claims it.
 */
#define SRD_CHIP_ROUND_TOLERANCE 0.25

/*
 * How much of a transmission's time a chip period has to account for before
 * it is believed.
 *
 * Manchester run lengths are one chip or two and nothing else, so a correct
 * period explains the whole signal and a wrong one explains whatever falls
 * in its windows. Measured over the 39 transmissions in the three SRD remote control
 * captures, at whatever period the recovery picked for each:
 *
 *   38 of 39 score 91.1% to 99.8%   -- every real burst, wakeup and data,
 *                                      OOK at 500 us and 2-FSK at 64.2 us
 *    1 of 39 scores 24.1%           -- transmission 0 of the 2-FSK SRD remote control capture,
 *                                      a weak neighbour whose discriminator
 *                                      output is mostly noise
 *
 * 0.50 is the geometric middle of that gap (46.9% to a decimal place),
 * which leaves a factor of 1.8 to the lowest true reading and 2.1 to the
 * only false one. It is deliberately not placed near 0.9: nothing here has
 * measured a *correct* period on a marginal signal, and the cost of the two
 * errors is not symmetric -- a missed decode is silence, where a false one
 * is this module answering the only question it exists to answer, wrongly.
 */
#define SRD_CHIP_COVERAGE_MIN 0.50

/*
 * How many samples of the demodulated signal a chip has to span before its
 * duration means anything.
 *
 * The work rate is about SRD_WORK_RATE_HZ, so one sample is about 5 us. A
 * candidate period of one sample is not a chip period, it is the sampling
 * grid: every run is one or two samples long because it cannot be anything
 * else, so a dense stream of threshold crossings on noise reads as a stream
 * of one- and two-chip runs and scores just over half the time by
 * srd_chip_coverage(). Measured on a live 434.35 MHz signal with no chip
 * structure at all -- a run histogram decaying from zero with nothing in it
 * past 120 us -- which returned "5.00 us, explaining 50.7%" and cleared the
 * coverage gate by seven parts in a thousand.
 *
 * Four, because that is where a one-chip run and a two-chip run stop being
 * distinguishable: at four samples they are 4 and 8 with a +/-25% tolerance
 * of one sample, and at one sample they are 1 and 2 with a tolerance of a
 * quarter of one. The two real chip periods are far above it -- the 2-FSK
 * remote's 64.2 us is 12.8 samples and the OOK remote control's 500 us is 100 -- so it
 * bounds nothing either of them does.
 *
 * It is emphatically not the 20 us floor this function used to have, which
 * happens to be the same number at this work rate. That one excluded *runs*
 * from the histogram, which is what left the noise population's tail as the
 * first mode; this rejects a *candidate period*, and it is derived from the
 * run lengths rather than compiled in, so it follows the work rate wherever
 * it goes.
 */
#define SRD_CHIP_MIN_SAMPLES 4.0

/*
 * What fraction of a run stream's *time* is spent in runs of one or two
 * chips at `chip_period` -- the evidence that a candidate period is the one
 * the transmitter used.
 *
 * Weighted by duration and not by count, which is the whole point: a
 * discriminator crossing its threshold on noise makes thousands of runs a
 * few microseconds long, so by count they are the dominant population and
 * by time they are a rounding error.
 */
double srd_chip_coverage(const struct srd_run *runs, size_t run_count,
                         double chip_period);

/*
 * Recover the fundamental chip period in seconds from a collection of run
 * lengths.
 *
 * In Manchester code a run is one chip (T) or two (2T) and never anything
 * else, with delimiters and inter-frame gaps at higher multiples. This bins
 * every run by duration, takes **every** significant mode as a candidate,
 * and returns the one that explains most of the transmission's time by
 * srd_chip_coverage().
 *
 * It used to take the *first* significant mode and guard against noise with
 * an absolute 20 us floor. Both parts were wrong and in the same direction:
 * the floor is a twenty-fifth of a chip for the OOK remote control and a third of one
 * for the 2-FSK remote, and excluding the runs under it leaves the tail of
 * the noise population as the first mode -- so a transmitter whose runs sit
 * at 500 and 1000 us was answered 23.01 us, with the real population in the
 * same histogram.
 *
 * Returns chip period in seconds (e.g. 0.000500 for 500 us), or **0.0 when
 * no candidate reaches SRD_CHIP_COVERAGE_MIN**, which is a refusal rather
 * than a failure to look: a histogram always has a mode, and returning it
 * regardless is how a signal that is not Manchester at all acquires a chip
 * period.
 */
double srd_chip_period(const struct srd_run *runs, size_t run_count);

/*
 * Discretise runs into binary chips (0 or 1) by dividing each run duration
 * by `chip_period` and rounding to the nearest integer count of chips.
 *
 * Returns number of chips written to `chips_out` (up to max_chips).
 */
size_t srd_runs_to_chips(const struct srd_run *runs, size_t run_count,
                         double chip_period, uint8_t *chips_out,
                         size_t max_chips);

/*
 * Manchester-decode a stream of chips into bits.
 *
 * Automatically tests phase 0 and phase 1 chip alignments, choosing the phase
 * with the minimum number of Manchester violations (00 or 11 chip pairs).
 * Decodes each pair according to `polarity`:
 *   SRD_MANCHESTER_THOMAS: 10 -> 0, 01 -> 1
 *   SRD_MANCHESTER_IEEE:   01 -> 0, 10 -> 1
 *
 * Returns 1 on success, 0 on invalid parameters.
 */
int srd_manchester_decode(const uint8_t *chips, size_t chip_count,
                          enum srd_manchester_polarity polarity,
                          struct srd_manchester_decode *out);

/*
 * Decode with both Thomas and IEEE polarities simultaneously.
 * Returns 1 on success, 0 on invalid parameters.
 */
int srd_manchester_decode_both(const uint8_t *chips, size_t chip_count,
                               struct srd_manchester_decode *thomas_out,
                               struct srd_manchester_decode *ieee_out);

/*
 * Pack an array of bits (0 or 1) MSB-first into bytes.
 * Returns number of full bytes written to `bytes_out`.
 */
size_t srd_pack_bits(const uint8_t *bits, size_t bit_count,
                     uint8_t *bytes_out, size_t max_bytes);

#endif /* SRD_DSP_H */
