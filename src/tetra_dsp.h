#ifndef TETRA_DSP_H
#define TETRA_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * TETRA: one 25 kHz carrier out of a block of samples, down to dibits.
 *
 * Everything above this -- the synchronisation burst, the network identity --
 * is somebody else's file. This one answers the physical question: what
 * symbols is that carrier sending, and are they TETRA's at all
 * (`.scratch/tetra-network-identity/`).
 *
 * Two things here are unlike every other decoder in this repository.
 *
 * **The symbol rate does not divide the sample rate.** 18 000 into 2 000 000
 * is 111.11 samples per symbol, so there is no whole decimation to hide behind.
 * fm_dsp says a broadcast station transmits its pilot precisely so a receiver
 * needs no blind loop, and every rate in the multiplex is a whole multiple of
 * it; there is no such gift here, and the timing has to be recovered.
 *
 * **The modulation is differential, so absolute phase never matters -- and a
 * residual frequency offset is fatal anyway.** pi/4-DQPSK carries its dibit in
 * the phase *step* between consecutive symbols, which means no carrier phase
 * recovery is needed. But an offset rotates every step by the same constant,
 * and the four legal steps are only 90 degrees apart, so a rotation does not
 * blur the constellation -- it turns every dibit cleanly into a different
 * dibit. At this site the receiver is about 35 ppm out, which is 14 kHz at
 * 392 MHz and most of a symbol's worth of phase per symbol. It is measured in
 * two stages, coarse then fine, for the same reason lte_cell_search measures
 * its offset twice.
 *
 * No raylib, no librtlsdr, no receiver: samples in, symbols out (ADR-0012).
 */

/* ETSI EN 300 392-2. A carrier is 25 kHz; the symbol rate is 18 000 and the
   pulse is a root-raised cosine with a roll-off of 0.35. */
#define TETRA_SYMBOL_RATE_HZ 18000.0
#define TETRA_CHANNEL_HZ 25000.0
#define TETRA_RRC_ROLLOFF 0.35
/* How far the matched filter reaches, in symbols either side. Eight is well
   past where a 0.35 root-raised cosine has anything left to contribute. */
#define TETRA_RRC_SPAN 8

/*
 * What the channel is worked at: the house rate decimated by a whole number,
 * which 100 kS/s is and 4-samples-per-symbol would not be. The fractional part
 * does not go away -- 5.5556 samples per symbol -- it just stops being in the
 * way of the decimation.
 */
#define TETRA_WORK_RATE_HZ 100000.0
#define TETRA_SAMPLES_PER_SYMBOL (TETRA_WORK_RATE_HZ / TETRA_SYMBOL_RATE_HZ)

/* One 65.5 ms block at 2 MS/s is 131072 pairs, which is 6553 at the working
   rate and 1179 symbols. Rounded up with room. */
#define TETRA_MAX_WORK 16384
#define TETRA_MAX_SYMBOLS 3000

/*
 * The four phase steps, in quarter-turns: +1, +3, -1, -3 times pi/4.
 *
 * Which dibit each carries is a convention out of the standard, and it is
 * exactly the kind of constant that cannot be checked by a round trip -- an
 * encoder and a decoder sharing a wrong table agree perfectly and decode
 * nothing off the air, which is how a conjugated primary sequence and a
 * scattered SCH field layout each survived here for months. So what this file
 * checks is what a table must be whatever the convention: four distinct steps,
 * all odd multiples of pi/4, a bijection with the four dibits. **The mapping
 * itself is not established until a parity check passes over real symbols**,
 * which is ticket 03's business, and until then nothing above should be
 * believed.
 */
#define TETRA_PHASE_STEPS 4

/* The synchronization training sequence is 38 bits, which is 19 symbols. */
#define TETRA_SYNC_SYMBOLS 19
/* Where it sits in the synchronization continuous downlink burst: bits 215 to
   252 of 510, which is symbols 108 to 126 of 255 (EN 300 392-2 table 9.9). */
#define TETRA_SYNC_AT_SYMBOL 107

/*
 * What one block of samples turned into.
 *
 * `lock` is the measurement that says whether this is TETRA at all: how close
 * the phase steps sit to the four they are allowed to be. One means every step
 * landed exactly; zero means they are spread uniformly, which is what noise, a
 * constant-envelope modulation, or the wrong symbol rate all look like.
 */
struct tetra_symbols {
    int count;
    unsigned char dibit[TETRA_MAX_SYMBOLS];
    float step[TETRA_MAX_SYMBOLS];   /* the phase step, radians, corrected */
    double coarse_offset_hz;         /* removed before filtering */
    double fine_offset_hz;           /* removed after, from the steps */
    double timing_phase;             /* symbol periods, 0 to 1 */
    float lock;                      /* 0 to 1; see above */
    float rms_error_rad;             /* mean distance to the nearest legal step */
};

/*
 * Where the carrier actually is, relative to the tuning, from the power
 * centroid of the channel. Coarse: good to a kilohertz or so, which is what
 * the fine stage needs to be inside its own ambiguity.
 *
 * The fine estimator is ambiguous every quarter of the symbol rate -- 4500 Hz
 * -- because a quarter turn maps the four legal steps onto each other. At
 * 392 MHz a 35 ppm receiver is 14 kHz out, three ambiguities away, so the
 * coarse stage is not optional.
 */
double tetra_coarse_offset_hz(const float *i_samples, const float *q_samples,
                              size_t pairs, double sample_rate,
                              double search_half_width_hz);

/*
 * One carrier, downconverted by `offset_hz`, low-pass filtered to the channel,
 * decimated to TETRA_WORK_RATE_HZ and matched-filtered. Returns how many pairs
 * were written, or 0 if the sample rate is not a whole multiple of the working
 * rate -- refused rather than resampled, the way lte_ refuses anything but its
 * own grid (ADR-0014).
 */
size_t tetra_channel(const float *i_samples, const float *q_samples,
                     size_t pairs, double sample_rate, double offset_hz,
                     float *out_i, float *out_q, size_t capacity);

/*
 * The symbol timing, as a fraction of a symbol period, from the symbol-rate
 * line in the squared magnitude (Oerder and Meyr).
 *
 * The same statistic that says a carrier is TETRA at all: a linearly modulated
 * signal with excess bandwidth puts a line at its symbol rate in |x|^2, and its
 * *phase* is where the symbols are. Open-loop and one estimate per chunk, so
 * there is no loop to lose lock -- and a chunk short enough that the clock
 * cannot drift a symbol inside it, which at 35 ppm is a long way.
 *
 * Returns the strength of that line relative to the block's mean power through
 * `strength`, which is the detection statistic: near zero for noise or for a
 * constant-envelope modulation, well above it for pi/4-DQPSK.
 */
double tetra_symbol_timing(const float *i_samples, const float *q_samples,
                           size_t pairs, double *strength);

/*
 * A filtered channel to dibits: timing, differential demodulation, the fine
 * frequency offset, and how well the result fits the four steps it should.
 *
 * Returns the symbol count, and fills `out`. `coarse_offset_hz` is recorded
 * rather than used -- it has already been removed by tetra_channel() -- so a
 * caller can report the whole offset it found.
 */
int tetra_demodulate(const float *i_samples, const float *q_samples,
                     size_t pairs, double coarse_offset_hz,
                     struct tetra_symbols *out);

/*
 * The burst grid: 255 symbols to a timeslot, four slots to a frame, eighteen
 * frames to a multiframe. 255 symbols at 18 000 is 14.167 ms and a frame is
 * 56.67 ms.
 */
#define TETRA_SLOT_SYMBOLS 255
#define TETRA_FRAME_SLOTS 4
#define TETRA_MULTIFRAME_FRAMES 18

/*
 * What repeats, and how strongly.
 *
 * Found from the symbols themselves rather than by correlating for a training
 * sequence, and that is a deliberate choice rather than a shortcut. The
 * sequence is a constant out of ETSI EN 300 392-2; a constant transcribed
 * wrongly still correlates with an encoder that shares the mistake and finds
 * nothing on air, which is the failure this repository has met twice. A burst
 * grid measured from the signal needs nothing transcribed at all -- and it
 * finds the grid whether or not the guess about which technology this is was
 * right.
 *
 * `profile[k]` is how often the symbol at position k of the period equals the
 * one a period earlier. A burst is a mixture: the training sequence and any
 * static filler sit near 1, the data near 0.25, and it is that *mixture* which
 * says a burst structure is there. A flat profile at 0.25 is noise; a flat
 * profile at 1.0 is a stuck receiver.
 */
struct tetra_burst_sync {
    int period;                            /* symbols, 0 when none stands out */
    float repeat;                          /* matches at that period, 0.25 up */
    float runner_up;                       /* the best other period */
    int fixed;                             /* positions above 0.9 */
    int varying;                           /* positions below 0.4 */
    float profile[TETRA_SLOT_SYMBOLS];
};

/*
 * Find the burst period in a run of dibits, searching `low` to `high`.
 *
 * Returns 1 when one period stands clear of the rest. The profile is only
 * filled in when the period found is TETRA_SLOT_SYMBOLS, since that is the
 * only length it is sized for.
 *
 * Refuses unless `count` is at least four times `high`, so every lag in the
 * range is judged on the same amount of evidence; the struct is zeroed either
 * way, so a refusal reads as a period of 0 rather than as leftovers.
 *
 * The caller must hand in symbols that were demodulated **contiguously**: this
 * counts matches at a fixed lag, so a stream stitched from chunks that each
 * began at their own timing phase has a discontinuity at every join, and the
 * profile smears every burst position together. That mistake was made while
 * writing this and is why the warning is here rather than in a comment.
 */
int tetra_burst_find(const unsigned char *dibits, int count, int low, int high,
                     struct tetra_burst_sync *out);

/*
 * The synchronization training sequence as dibits, and where it best matches in
 * a run of them.
 *
 * `tetra_find_sync` returns the symbol index of the best match, or -1 when
 * nothing reaches two thirds of the nineteen. It is the check that a
 * transcription is right: known bits, on air, in a place the burst layout
 * predicts. A round trip cannot do that job, which is why the phase table was
 * wrong here for a day and every synthetic test passed.
 */
int tetra_sync_dibits(const unsigned char **dibits);
int tetra_find_sync(const unsigned char *dibits, int count, int *matched);

/* The phase step a dibit is sent as, and the dibit a step nearest to. Exposed
   so a check can assert the table is a bijection without an encoder. */
double tetra_step_for_dibit(int dibit);
int tetra_dibit_for_step(double radians);

#endif
