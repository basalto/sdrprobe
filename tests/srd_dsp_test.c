/*
 * Deterministic, hardware-free checks for the SRD technology DSP module.
 *
 * Synthetic fixtures only: an OOK Manchester burst laid into noise at a
 * known carrier, chip period and bit pattern. AGENTS.md's standing warning
 * applies: a synthetic round trip agrees with whatever assumption built it,
 * so the real captures in testfiles/srd_remote_control_ook_*.bin remain the only thing
 * that establishes the decode off the air.
 *
 *   make check-srd-dsp
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "device_profile.h"
#include "sdr_dsp.h"
#include "srd_dsp.h"
/* For SRD_FULL_FRAME_BITS alone -- the real-capture claim below is stated in
 * frames, and restating 80 here is how two numbers come to disagree. Nothing
 * from srd_frame.c is called, so the suite still links -lm and srd_dsp. */
#include "srd_frame.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TEST_FS 2000000.0
#define TEST_CHIP_US 500.0
#define TEST_CHIP_S (TEST_CHIP_US * 1e-6)

/*
 * Bit packing and unpacking helpers for synthetic tests.
 */
static void unpack_bytes_to_bits(const uint8_t *bytes, size_t byte_count,
                                 uint8_t *bits_out) {
    for (size_t i = 0; i < byte_count; i++) {
        for (int b = 7; b >= 0; b--) {
            *bits_out++ = (bytes[i] >> b) & 1;
        }
    }
}

/*
 * Manchester encoder for test patterns.
 * Thomas: 0 -> 10, 1 -> 01
 * IEEE:   0 -> 01, 1 -> 10
 */
static size_t encode_manchester(const uint8_t *bits, size_t bit_count,
                                enum srd_manchester_polarity polarity,
                                uint8_t *chips_out) {
    size_t chips = 0;
    for (size_t i = 0; i < bit_count; i++) {
        uint8_t b = bits[i];
        if (polarity == SRD_MANCHESTER_THOMAS) {
            chips_out[chips++] = b ? 0 : 1;
            chips_out[chips++] = b ? 1 : 0;
        } else {
            chips_out[chips++] = b ? 1 : 0;
            chips_out[chips++] = b ? 0 : 1;
        }
    }
    return chips;
}

static void test_manchester_decode_basic(void) {
    const uint8_t test_bytes[] = { 0xa5, 0x5a, 0x12, 0x34, 0xfe, 0xdc };
    const size_t bit_count = sizeof(test_bytes) * 8;
    uint8_t orig_bits[sizeof(test_bytes) * 8];
    uint8_t chips[sizeof(test_bytes) * 16];
    uint8_t packed[sizeof(test_bytes)];
    struct srd_manchester_decode dec_thomas, dec_ieee;
    size_t chip_count;

    unpack_bytes_to_bits(test_bytes, sizeof(test_bytes), orig_bits);

    /* Encode using Thomas convention. */
    chip_count = encode_manchester(orig_bits, bit_count, SRD_MANCHESTER_THOMAS,
                                   chips);
    check_size("thomas chip count is 2x bit count", chip_count, bit_count * 2);

    /* Decode with Thomas convention. */
    check_int("decode thomas succeeds",
              srd_manchester_decode(chips, chip_count, SRD_MANCHESTER_THOMAS,
                                    &dec_thomas), 1);
    check_size("decoded bit count matches", dec_thomas.bit_count, bit_count);
    check_size("zero manchester errors on clean chips", dec_thomas.error_count, 0);
    check_int("detected phase is 0", dec_thomas.phase, 0);
    check_int("bits match original",
              memcmp(dec_thomas.bits, orig_bits, bit_count), 0);

    /* Pack bits and check bytes match. */
    check_size("pack bits returns full bytes",
               srd_pack_bits(dec_thomas.bits, dec_thomas.bit_count, packed,
                             sizeof(packed)), sizeof(test_bytes));
    check_int("packed bytes match original",
              memcmp(packed, test_bytes, sizeof(test_bytes)), 0);

    /* Decode with IEEE convention: should produce exact inverted bits. */
    check_int("decode ieee succeeds",
              srd_manchester_decode(chips, chip_count, SRD_MANCHESTER_IEEE,
                                    &dec_ieee), 1);
    check_size("ieee bit count matches", dec_ieee.bit_count, bit_count);
    check_size("ieee zero errors on clean chips", dec_ieee.error_count, 0);
    for (size_t i = 0; i < bit_count; i++) {
        if (dec_ieee.bits[i] != (1 - orig_bits[i])) {
            check_msg(0, "ieee bit %zu is %d, expected %d\n", i,
                      dec_ieee.bits[i], 1 - orig_bits[i]);
            break;
        }
    }
}

static void test_manchester_phase_auto_detect(void) {
    const uint8_t test_bytes[] = { 0xa5, 0x5a, 0x33, 0xcc };
    const size_t bit_count = sizeof(test_bytes) * 8;
    uint8_t orig_bits[sizeof(test_bytes) * 8];
    uint8_t chips_aligned[sizeof(test_bytes) * 16];
    uint8_t chips_shifted[sizeof(test_bytes) * 16 + 1];
    struct srd_manchester_decode dec;
    size_t chip_count;

    unpack_bytes_to_bits(test_bytes, sizeof(test_bytes), orig_bits);
    chip_count = encode_manchester(orig_bits, bit_count, SRD_MANCHESTER_THOMAS,
                                   chips_aligned);

    /* Prepend 1 dummy chip so the valid stream starts at index 1. */
    chips_shifted[0] = 0;
    memcpy(chips_shifted + 1, chips_aligned, chip_count);

    check_int("decode shifted chips succeeds",
              srd_manchester_decode(chips_shifted, chip_count + 1,
                                    SRD_MANCHESTER_THOMAS, &dec), 1);
    check_int("detected phase is 1", dec.phase, 1);
    check_size("decoded bit count matches", dec.bit_count, bit_count);
    check_size("zero errors in detected phase", dec.error_count, 0);
    check_int("decoded bits match original despite phase offset",
              memcmp(dec.bits, orig_bits, bit_count), 0);
}

static void test_manchester_violations(void) {
    uint8_t chips[] = {
        1, 0,  /* bit 0 */
        0, 1,  /* bit 1 */
        0, 0,  /* VIOLATION */
        1, 1,  /* VIOLATION */
        1, 0   /* bit 0 */
    };
    struct srd_manchester_decode dec;

    check_int("decode with violations succeeds",
              srd_manchester_decode(chips, sizeof(chips), SRD_MANCHESTER_THOMAS,
                                    &dec), 1);
    check_size("decoded 5 bit periods", dec.bit_count, 5);
    check_size("detected exactly 2 violations", dec.error_count, 2);
}

static void test_chip_period_recovery_from_runs(void) {
    /*
     * Build runs representing Manchester traffic:
     * mostly 500 us (1 chip) and 1000 us (2 chips), plus some 750 us delimiters.
     */
    struct srd_run runs[200];
    size_t run_count = 0;
    int state = 1;

    /* Preamble: 20 alternating 1-chip runs. */
    for (int i = 0; i < 20; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = 0.000502; /* 502 us with slight jitter */
        runs[run_count].length_samples = 100;
        state = !state;
        run_count++;
    }

    /* Data: mix of 1-chip and 2-chip runs. */
    for (int i = 0; i < 60; i++) {
        runs[run_count].state = state;
        if (i % 3 == 0)
            runs[run_count].duration_seconds = 0.000998; /* 2 chips */
        else
            runs[run_count].duration_seconds = 0.000497; /* 1 chip */
        runs[run_count].length_samples = (size_t)(runs[run_count].duration_seconds * 200000.0);
        state = !state;
        run_count++;
    }

    /* Delimiter: six 750 us runs (1.5 chips). */
    for (int i = 0; i < 6; i++) {
        runs[run_count].state = state;
        runs[run_count].duration_seconds = 0.000751;
        runs[run_count].length_samples = 150;
        state = !state;
        run_count++;
    }

    /* A few stray noise glitches that must not fool the mode finder. */
    runs[run_count].state = state;
    runs[run_count].duration_seconds = 0.000030; /* 30 us glitch */
    runs[run_count].length_samples = 6;
    run_count++;

    double recovered_chip_s = srd_chip_period(runs, run_count);
    check_close("recovered chip period is near 500 us",
                recovered_chip_s * 1e6, 500.0, 15.0);

    /* Convert runs to chips with recovered period. */
    uint8_t chips[400];
    size_t chip_count = srd_runs_to_chips(runs, 20, recovered_chip_s, chips,
                                          sizeof(chips));
    check_size("20 1-chip preamble runs yield 20 chips", chip_count, 20);
    for (size_t i = 0; i < 20; i++) {
        check_int("preamble chip alternates", chips[i], (i % 2 == 0) ? 1 : 0);
    }
}

/*
 * Sub-chip noise does not decide the chip period, at either chip period.
 *
 * A discriminator crossing its threshold on noise makes a decaying
 * population of very short runs, and how short they are is set by the
 * demodulator's bandwidth rather than by the transmitter's chip rate. So the
 * *same* noise is invisible against the remote control's 500 us chips and a serious
 * contaminant against the 2-FSK remote's 64 us ones -- which is exactly why
 * srd_chip_period() used to guard against it with an absolute 20 us floor
 * and exactly why that could not work. Excluding everything under 20 us
 * leaves the tail of the noise population as the first significant mode, and
 * on transmission 0 of the 2-FSK capture that tail beat the real chips by
 * 743 runs to about 90.
 *
 * Both periods are checked with the same noise for that reason: a floor
 * re-fixed to a constant that suits one of them fails the other.
 */
static void add_manchester_runs(struct srd_run *runs, size_t *count,
                                double chip_s, int data_runs, int *state) {
    for (int i = 0; i < data_runs; i++) {
        runs[*count].state = *state;
        /* Two chips every third run, one otherwise, with a little jitter. */
        runs[*count].duration_seconds =
            (i % 3 == 0) ? chip_s * 1.997 : chip_s * 0.996;
        runs[*count].length_samples =
            (size_t)(runs[*count].duration_seconds * 200000.0);
        *state = !*state;
        (*count)++;
    }
}

static void add_subchip_noise(struct srd_run *runs, size_t *count,
                              int noise_runs, int *state) {
    for (int i = 0; i < noise_runs; i++) {
        /*
         * 2 us to 30 us, weighted towards the short end the way threshold
         * crossings are, and crossing the old 20 us floor so the fixture
         * reaches the population that floor could not exclude.
         */
        int bucket = i % 14;
        runs[*count].state = *state;
        runs[*count].duration_seconds = (2.0 + (double)(bucket * bucket) / 7.0) * 1e-6;
        runs[*count].length_samples =
            (size_t)(runs[*count].duration_seconds * 200000.0);
        *state = !*state;
        (*count)++;
    }
}

static void test_chip_period_survives_subchip_noise(void) {
    const double periods[2] = { 0.000500, 0.000064 };
    const char *names[2] = { "500 us", "64 us" };

    for (int k = 0; k < 2; k++) {
        struct srd_run runs[1024];
        size_t count = 0;
        int state = 1;
        double recovered, coverage;
        char label[96];

        add_manchester_runs(runs, &count, periods[k], 90, &state);
        add_subchip_noise(runs, &count, 360, &state);

        recovered = srd_chip_period(runs, count);

        snprintf(label, sizeof(label),
                 "%s chips are recovered under 4x their number in noise",
                 names[k]);
        check_close(label, recovered * 1e6, periods[k] * 1e6,
                    periods[k] * 1e6 * 0.05);

        coverage = srd_chip_coverage(runs, count, recovered);
        snprintf(label, sizeof(label),
                 "%s: the recovered period explains most of the time",
                 names[k]);
        check_true(label, coverage > SRD_CHIP_COVERAGE_MIN);

        /*
         * The specific wrong answer this ticket is about: the noise tail sits
         * around 23 us, and at 500 us chips that is what the old first-mode
         * rule returned. It has to score worse than the truth at both chip
         * periods, or picking the best candidate would not have helped.
         */
        snprintf(label, sizeof(label),
                 "%s: the noise tail explains less than the truth", names[k]);
        check_true(label,
                   srd_chip_coverage(runs, count, 0.000023) < coverage);
    }
}

/*
 * A stream that is mostly noise has no chip period, and saying so is the
 * answer.
 *
 * This used to be impossible to express: a histogram always has a mode, so
 * the function returned one whatever it was shown. That was survivable only
 * while srd_extract_frames() kept a single longest Manchester stretch and a
 * wrong period rarely produced one. It reports every legal stretch now, so a
 * period that explains nothing turns into bytes that are not a frame of
 * anything -- three of them, on the transmission this was written for.
 */
static void test_a_stream_of_noise_has_no_chip_period(void) {
    struct srd_run runs[2048];
    size_t count = 0;
    int state = 1;

    add_manchester_runs(runs, &count, 0.000500, 12, &state);
    add_subchip_noise(runs, &count, 1400, &state);

    check_close("a stream that is mostly noise is refused",
                srd_chip_period(runs, count), 0.0, 1e-12);
}

/*
 * What the coverage measure says on signals whose answer is known by
 * construction, since every threshold above rests on it.
 */
static void test_chip_coverage_measure(void) {
    struct srd_run runs[128];
    size_t count = 0;
    int state = 1;

    add_manchester_runs(runs, &count, 0.000500, 60, &state);

    check_close("clean Manchester is fully explained by its own period",
                srd_chip_coverage(runs, count, 0.000500), 1.0, 0.01);
    /*
     * Half the period reads a one-chip run as two chips, which counts, and a
     * two-chip run as four, which does not -- so it explains exactly the time
     * spent in one-chip runs and no more. This fixture divides its time
     * evenly between the two, so it scores a half, and the true period
     * scoring twice that is why taking the best candidate finds the
     * fundamental rather than a harmonic of it.
     */
    check_close("half the period explains only the one-chip runs",
                srd_chip_coverage(runs, count, 0.000250), 0.50, 0.02);
    check_close("a period nothing is a multiple of explains nothing",
                srd_chip_coverage(runs, count, 0.000333), 0.0, 0.05);
    check_close("a zero period is refused rather than divided by",
                srd_chip_coverage(runs, count, 0.0), 0.0, 1e-12);
    check_close("no runs is no coverage",
                srd_chip_coverage(runs, 0, 0.000500), 0.0, 1e-12);
}

/*
 * The sampling grid is not a chip period.
 *
 * The demodulated signal runs at about SRD_WORK_RATE_HZ, so one sample is
 * about 5 us, and a stream of threshold crossings on noise makes runs that
 * are one or two samples long because they cannot be anything else. Read at
 * a 5 us "chip period" that is a stream of one- and two-chip runs, which is
 * exactly what srd_chip_coverage() is looking for -- it scored **50.7%** on a
 * live 434.35 MHz signal with no chip structure at all, clearing the coverage
 * gate by seven parts in a thousand and handing the frame extractor a period
 * twelve times too short.
 *
 * Coverage cannot catch this on its own, because the noise genuinely does
 * spend its time in one- and two-sample runs. SRD_CHIP_MIN_SAMPLES is the
 * separate question: whether the candidate is long enough for a run length to
 * carry information at all.
 */
static void test_the_sampling_grid_is_not_a_chip_period(void) {
    const double work_rate = SRD_WORK_RATE_HZ;
    struct srd_run runs[2048];
    size_t count = 0;
    int state = 1;
    size_t i;

    /*
     * Runs of one and two samples, alternating, the way a slicer chattering
     * on noise produces them. Nothing here is a transmitter.
     */
    for (i = 0; i < 1200; i++) {
        size_t samples = (i % 3 == 0) ? 2 : 1;

        runs[count].state = state;
        runs[count].length_samples = samples;
        runs[count].duration_seconds = (double)samples / work_rate;
        state = !state;
        count++;
    }

    /*
     * The trap in full: by coverage alone this passes, which is why the
     * refusal cannot rest on coverage.
     */
    check_true("chatter does look like one- and two-chip runs",
               srd_chip_coverage(runs, count, 1.0 / work_rate) >
               SRD_CHIP_COVERAGE_MIN);
    check_close("and is refused anyway", srd_chip_period(runs, count), 0.0,
                1e-12);

    /*
     * The floor follows the work rate rather than a constant, which is the
     * whole difference between it and the 20 us floor this function used to
     * have. At ten times the rate the same chatter is ten times shorter and
     * still refused; a real chip period ten times shorter is still found.
     */
    count = 0;
    state = 1;
    for (i = 0; i < 1200; i++) {
        size_t samples = (i % 3 == 0) ? 2 : 1;

        runs[count].state = state;
        runs[count].length_samples = samples;
        runs[count].duration_seconds = (double)samples / (work_rate * 10.0);
        state = !state;
        count++;
    }
    check_close("chatter at ten times the work rate is refused too",
                srd_chip_period(runs, count), 0.0, 1e-12);

    count = 0;
    state = 1;
    add_manchester_runs(runs, &count, SRD_CHIP_MIN_SAMPLES / work_rate * 3.0,
                        90, &state);
    for (i = 0; i < count; i++)
        runs[i].length_samples =
            (size_t)(runs[i].duration_seconds * work_rate + 0.5);
    check_close("a chip three times the floor is still recovered",
                srd_chip_period(runs, count) * work_rate,
                SRD_CHIP_MIN_SAMPLES * 3.0, 1.0);

    /*
     * Both real chip periods sit far above the floor, so it bounds nothing
     * either of them does. Stated here rather than left implicit, because a
     * floor that crept up would break them silently.
     */
    check_true("the 2-FSK remote's 64.2 us clears the floor",
               0.0000642 * work_rate > SRD_CHIP_MIN_SAMPLES);
    check_true("the OOK protocol's 500 us clears it by far",
               0.000500 * work_rate > SRD_CHIP_MIN_SAMPLES * 20.0);
}

/*
 * Where the receiver has to be pointed for any of this to mean anything.
 *
 * The allocation is the band plan's own "70 cm amateur / 433 ISM" entry,
 * 430-440 MHz, and the test is containment rather than coverage: ten
 * megahertz needs 10 MS/s and nothing here samples that fast, so the receiver
 * hears a slice and decodes what is in it.
 *
 * It used to demand a tuning within a megahertz of 434, on the argument that
 * 433-435 is a 2 MHz span and the receiver may be parked at either edge --
 * sound for that sub-band, and it refused eight megahertz of the allocation
 * including frequencies this receiver has recorded transmissions on.
 */
static void test_where_the_receiver_has_to_be(void) {
    check_int("the middle of the band is ready",
              srd_receiver_ready(SRD_CENTER_HZ, 2000000U), 1);
    check_int("so is the bottom edge",
              srd_receiver_ready(SRD_BAND_LOWER_HZ, 2000000U), 1);
    check_int("and the top edge",
              srd_receiver_ready(SRD_BAND_UPPER_HZ, 2000000U), 1);

    /*
     * The part that changed. Each of these is inside the allocation and was
     * refused by the old within-a-megahertz-of-434 rule.
     */
    check_int("431.5 MHz is in the band",
              srd_receiver_ready(431500000U, 2000000U), 1);
    check_int("438.2 MHz is in the band",
              srd_receiver_ready(438200000U, 2000000U), 1);
    check_int("432.9 MHz is in the band",
              srd_receiver_ready(432900000U, 2000000U), 1);

    check_int("a hertz below the band is not",
              srd_receiver_ready(SRD_BAND_LOWER_HZ - 1U, 2000000U), 0);
    check_int("a hertz above the band is not",
              srd_receiver_ready(SRD_BAND_UPPER_HZ + 1U, 2000000U), 0);
    check_int("nor is somewhere else entirely",
              srd_receiver_ready(100000000U, 2000000U), 0);

    /* The rate still has to be enough for the channel widths measured here. */
    check_int("too slow is not ready whatever the tuning",
              srd_receiver_ready(SRD_CENTER_HZ, SRD_MIN_SAMPLE_RATE - 1U), 0);
    check_int("exactly the minimum rate is ready",
              srd_receiver_ready(SRD_CENTER_HZ, SRD_MIN_SAMPLE_RATE), 1);

    /*
     * The allocation is the band plan's, and the two may not drift apart --
     * asserted against the numbers rather than by including band_plan.h,
     * which would drag a table into a DSP check. band_plan.c carries the
     * matching row and check-band-plan walks it.
     */
    check_int("the lower edge is the band plan's 430 MHz",
              (int)(SRD_BAND_LOWER_HZ / 1000000U), 430);
    check_int("the upper edge is the band plan's 440 MHz",
              (int)(SRD_BAND_UPPER_HZ / 1000000U), 440);
}

/*
 * Where a tuning arrow lands.
 *
 * Half a span a press, so consecutive tunings overlap by half and a
 * transmission on a seam is whole in one of them -- a whole-span step would
 * leave it half in each and strong in neither. The step follows the rate
 * rather than being a round number of hertz, because the useful step is the
 * one that tiles what the receiver can hear.
 */
static void test_where_a_tuning_arrow_lands(void) {
    const uint32_t rate = 2000000U;

    check_int("up is half a span up",
              (int)srd_tune_step(434000000U, rate, +1), 435000000);
    check_int("down is half a span down",
              (int)srd_tune_step(434000000U, rate, -1), 433000000);
    check_int("the step follows the rate, not a constant",
              (int)srd_tune_step(434000000U, 2400000U, +1), 435200000);

    /*
     * Clamped to the allocation. The arrows walk the band; the field beside
     * them is what leaves it, so nothing is made unreachable by this.
     */
    check_int("up stops at the top of the band",
              (int)srd_tune_step(439500000U, rate, +1),
              (int)SRD_BAND_UPPER_HZ);
    check_int("down stops at the bottom",
              (int)srd_tune_step(430500000U, rate, -1),
              (int)SRD_BAND_LOWER_HZ);

    /*
     * Already hard against the edge: the same frequency comes back, which is
     * what lets a caller tell a press that moved from one that did not
     * rather than issuing a retune to where the receiver already is.
     */
    check_int("pressing up at the top changes nothing",
              (int)srd_tune_step(SRD_BAND_UPPER_HZ, rate, +1),
              (int)SRD_BAND_UPPER_HZ);
    check_int("pressing down at the bottom changes nothing",
              (int)srd_tune_step(SRD_BAND_LOWER_HZ, rate, -1),
              (int)SRD_BAND_LOWER_HZ);

    /* But the other direction still works from an edge. */
    check_int("down from the top still moves",
              (int)srd_tune_step(SRD_BAND_UPPER_HZ, rate, -1), 439000000);
    check_int("up from the bottom still moves",
              (int)srd_tune_step(SRD_BAND_LOWER_HZ, rate, +1), 431000000);

    check_int("no direction is no move",
              (int)srd_tune_step(434000000U, rate, 0), 434000000);
    check_int("no rate is no move",
              (int)srd_tune_step(434000000U, 0U, +1), 434000000);

    /*
     * The band is walkable end to end, which is the point of the arrows
     * existing: stepping up from the bottom reaches the top and stops there.
     */
    {
        uint32_t at = SRD_BAND_LOWER_HZ;
        int presses = 0;

        while (at < SRD_BAND_UPPER_HZ && presses < 100) {
            uint32_t next = srd_tune_step(at, rate, +1);

            if (next == at)
                break;
            at = next;
            presses++;
        }
        check_int("ten presses walk the whole allocation", presses, 10);
        check_int("and land on the top edge", (int)at, (int)SRD_BAND_UPPER_HZ);
    }
}

static void test_envelope_demodulation_and_runs(void) {
    /*
     * Synthesize 40 ms of I/Q samples at 2 MS/s.
     * Carrier at +250 kHz, keyed on and off with 500 us chips.
     */
    const double fs = TEST_FS;
    const double fc = 250000.0;
    const size_t total_pairs = (size_t)(0.040 * fs); /* 80,000 pairs */
    float *i_samples = malloc(total_pairs * sizeof(float));
    float *q_samples = malloc(total_pairs * sizeof(float));
    float *env = malloc(total_pairs * sizeof(float));
    struct srd_run runs[256];
    double work_rate = 0.0;

    check_true("allocated synthetic buffers", i_samples && q_samples && env);

    /* 40 ms at 500 us per chip = 80 chips. Pattern: 10101100... */
    for (size_t n = 0; n < total_pairs; n++) {
        double t = (double)n / fs;
        int chip_idx = (int)(t / 0.000500);
        int on = 0;
        if (chip_idx < 80) {
            /* 20 preamble chips (alternating), then pairs */
            if (chip_idx < 20)
                on = (chip_idx % 2 == 0);
            else
                on = ((chip_idx / 2) % 2 == 0);
        }
        double amp = on ? 100.0 : 0.5; /* high SNR */
        double phase = 2.0 * M_PI * fc * t;
        i_samples[n] = (float)(amp * cos(phase));
        q_samples[n] = (float)(amp * sin(phase));
    }

    size_t env_count = srd_demodulate_envelope(i_samples, q_samples, total_pairs,
                                               fs, fc, env, total_pairs,
                                               &work_rate);
    check_true("demodulated envelope produced samples", env_count > 1000);
    check_close("work rate is 200 kS/s", work_rate, 200000.0, 1.0);

    double threshold = srd_envelope_threshold(env, env_count);
    check_true("threshold sits between floor and peak",
               threshold > 10.0 && threshold < 90.0);

    size_t run_count = srd_extract_runs(env, env_count, work_rate, threshold,
                                        runs, 256);
    check_true("extracted expected runs", run_count >= 30);

    double chip_s = srd_chip_period(runs, run_count);
    check_close("chip period from extracted envelope runs is 500 us",
                chip_s * 1e6, 500.0, 25.0);

    free(i_samples);
    free(q_samples);
    free(env);
}

static uint32_t rng = 1u;
static double test_noise(void) {
    rng = rng * 1664525u + 1013904223u;
    return ((double)((rng >> 16) & 0xffff) / 32768.0) - 1.0;
}

static void test_find_transmissions(void) {
    /*
     * Synthesize 160 ms buffer with two separate transmissions:
     * Tx 1: 10 ms to 40 ms (30 ms duration)
     * Gap: 40 ms to 100 ms (60 ms silence > 50 ms gap)
     * Tx 2: 100 ms to 140 ms (40 ms duration)
     * Carrier at +150 kHz.
     */
    const double fs = TEST_FS;
    const double fc = 150000.0;
    const size_t total_pairs = (size_t)(0.160 * fs); /* 320,000 pairs */
    float *i_samples = malloc(total_pairs * sizeof(float));
    float *q_samples = malloc(total_pairs * sizeof(float));
    struct srd_transmission found[SRD_MAX_TRANSMISSIONS];

    check_true("allocated transmission buffers", i_samples && q_samples);

    for (size_t n = 0; n < total_pairs; n++) {
        double t = (double)n / fs;
        int on = (t >= 0.010 && t < 0.040) || (t >= 0.100 && t < 0.140);
        double phase = 2.0 * M_PI * fc * t;
        double sig_i = on ? (80.0 * cos(phase)) : 0.0;
        double sig_q = on ? (80.0 * sin(phase)) : 0.0;
        i_samples[n] = (float)(sig_i + 0.5 * test_noise());
        q_samples[n] = (float)(sig_q + 0.5 * test_noise());
    }

    int count = srd_find_transmissions(i_samples, q_samples, total_pairs, fs,
                                       127.5f, 20.0, 0.050, found,
                                       SRD_MAX_TRANSMISSIONS);
    check_int("found exactly 2 transmissions", count, 2);

    if (count >= 2) {
        check_close("tx 1 start is ~10 ms", found[0].start_seconds, 0.010, 0.005);
        check_close("tx 1 duration is ~30 ms", found[0].duration_seconds, 0.030, 0.005);
        check_close("tx 1 carrier is near +150 kHz", found[0].carrier_hz, fc, 2000.0);
        check_true("tx 1 carrier stands well over floor",
                   found[0].carrier_over_noise_db > 20.0);

        check_close("tx 2 start is ~100 ms", found[1].start_seconds, 0.100, 0.005);
        check_close("tx 2 duration is ~40 ms", found[1].duration_seconds, 0.040, 0.005);
        check_close("tx 2 carrier is near +150 kHz", found[1].carrier_hz, fc, 2000.0);
        check_true("tx 2 carrier stands well over floor",
                   found[1].carrier_over_noise_db > 20.0);
    }

    /* Pure noise buffer should report zero transmissions. */
    for (size_t n = 0; n < 20480; n++) {
        i_samples[n] = (float)(0.5 * test_noise());
        q_samples[n] = (float)(0.5 * test_noise());
    }
    int noise_count = srd_find_transmissions(i_samples, q_samples, 20480, fs,
                                             127.5f, 25.0, 0.050, found,
                                             SRD_MAX_TRANSMISSIONS);
    check_int("noise buffer yields 0 transmissions", noise_count, 0);

    free(i_samples);
    free(q_samples);
}

static void test_refusals_and_edge_cases(void) {
    struct srd_transmission tx;
    struct srd_manchester_decode dec;
    float env[10];
    double rate;

    check_int("null samples refused in find_transmissions",
              srd_find_transmissions(NULL, NULL, 10000, TEST_FS, 127.5f,
                                     25.0, 0.050, &tx, 1), 0);
    check_size("null samples refused in demodulate_envelope",
               srd_demodulate_envelope(NULL, NULL, 100, TEST_FS, 0.0, env,
                                       10, &rate), 0);
    check_close("null runs refused in chip_period",
                srd_chip_period(NULL, 0), 0.0, 1e-9);
    check_int("null chips refused in manchester_decode",
              srd_manchester_decode(NULL, 0, SRD_MANCHESTER_THOMAS, &dec), 0);
}

/*
 * The longest run of consecutive legal Manchester chip pairs at a decode's
 * chosen phase, in bits. srd_extract_frames() keeps exactly this stretch and
 * discards the rest, so it is what says whether a chip period explains the
 * signal -- a violation count over a whole press does not, since the idle
 * between frames violates the code as surely as noise does.
 */
static size_t longest_unbroken(const uint8_t *chips, size_t chip_count,
                               int phase) {
    size_t best = 0, run = 0, c;

    for (c = (size_t)phase; c + 1 < chip_count; c += 2) {
        if (chips[c] == chips[c + 1]) {
            if (run > best)
                best = run;
            run = 0;
        } else {
            run++;
        }
    }
    return run > best ? run : best;
}

static void test_real_capture(void) {
    const char *path = "testfiles/srd_remote_control_ook_a.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        check_true("required OOK SRD remote-control capture is present", 0);
        return;
    }

    struct device_profile dev = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    size_t pairs = (size_t)sz / dev.bytes_per_pair;
    if (pairs > 10000000)
        pairs = 10000000;

    unsigned char *raw = malloc(pairs * dev.bytes_per_pair);
    float *i_buf = malloc(pairs * sizeof(float));
    float *q_buf = malloc(pairs * sizeof(float));
    float *mag_buf = malloc(pairs * sizeof(float));

    if (raw && i_buf && q_buf && mag_buf &&
        fread(raw, dev.bytes_per_pair, pairs, f) == pairs) {
        sdr_dsp_convert_iq(&dev, raw, pairs * dev.bytes_per_pair, i_buf, q_buf, mag_buf, pairs);

        struct srd_transmission txs[SRD_MAX_TRANSMISSIONS];
        int count = srd_find_transmissions(i_buf, q_buf, pairs, 2000000.0,
                                           dev.full_scale, 25.0, 0.050,
                                           txs, SRD_MAX_TRANSMISSIONS);
        check_int("real OOK capture has 4 activity groups", count, 4);
        if (count >= 4) {
            check_close("real OOK capture group 1 starts near 0.780 s",
                        txs[0].start_seconds, 0.780, 0.050);
            check_close("real OOK capture carrier is near +617 kHz",
                        txs[0].carrier_hz, 617000.0, 5000.0);
            check_true("real carrier stands > 45 dB over floor",
                       txs[0].carrier_over_noise_db > 45.0);

            /* Take the longest transmission and demodulate it */
            int longest = 0;
            for (int k = 1; k < count; k++)
                if (txs[k].duration_seconds > txs[longest].duration_seconds)
                    longest = k;

            size_t env_cap = txs[longest].pair_count / 10 + 1000;
            float *env = malloc(env_cap * sizeof(float));
            double work_rate;
            if (env) {
                size_t env_n = srd_demodulate_envelope(i_buf + txs[longest].offset_pairs,
                                                       q_buf + txs[longest].offset_pairs,
                                                       txs[longest].pair_count,
                                                       2000000.0,
                                                       txs[longest].carrier_hz,
                                                       env, env_cap, &work_rate);
                double thresh = srd_envelope_threshold(env, env_n);
                struct srd_run runs[4096];
                size_t r_count = srd_extract_runs(env, env_n, work_rate, thresh, runs, 4096);
                double chip_s = srd_chip_period(runs, r_count);
                check_close("chip period on real SRD remote-control capture is 500 us",
                            chip_s * 1e6, 500.0, 15.0);

                uint8_t chips[8192];
                size_t chip_n = srd_runs_to_chips(runs, r_count, chip_s, chips, 8192);
                struct srd_manchester_decode thomas, ieee;
                check_int("decode both polarities on real capture succeeds",
                          srd_manchester_decode_both(chips, chip_n, &thomas, &ieee), 1);
                /*
                 * Not a violation *rate* over the whole press, which is
                 * what stood here and is the wrong statistic: a press is
                 * 1.1 s holding twelve 80-bit frames with idle between
                 * them, and the idle is not a legal Manchester symbol, so
                 * the four presses of this capture read 10.3%, 2.8%,
                 * 12.2% and 11.3% while every frame the same run stream
                 * yields carries zero errors (check-srd-frame asserts
                 * that, on this capture). Counting the gaps as decode
                 * errors is the same transcript-not-a-measurement fault
                 * srd_dsp.h recorded for the 2-FSK bursts.
                 *
                 * What this layer can establish is the unbroken stretch,
                 * because a frame has to arrive without a violation
                 * inside it. Measured from both ends: the real period
                 * gives 208 to 215 bits on all four presses, and the best
                 * *wrong* period in the sweep (340-390 us) gives 127,
                 * with everything under 200 us giving 1 or 2. A floor of
                 * two frames sits between them.
                 */
                check_true("real transmission decodes two frames' worth "
                           "without a Manchester violation",
                           longest_unbroken(chips, chip_n, thomas.phase) >=
                               2 * SRD_FULL_FRAME_BITS);
                free(env);
            }
        }
    }
    free(raw);
    free(i_buf);
    free(q_buf);
    free(mag_buf);
    fclose(f);
}

static void test_fsk_classification_and_demodulation(void) {
    /*
     * Synthesize 40 ms of constant-envelope 2-FSK: two tones 30 kHz apart
    * (matching the protocol's measured ~31 kHz tone separation),
    * alternating every 500 us chip in
     * a Manchester-coded pattern, mixed to a channel centred at +150 kHz.
     */
    const double fs = TEST_FS;
    const double fc = 150000.0;
    const double dev = 15000.0; /* +/- 15 kHz, ~31 kHz tone separation */
    const size_t total_pairs = (size_t)(0.040 * fs);
    float *i_samples = malloc(total_pairs * sizeof(float));
    float *q_samples = malloc(total_pairs * sizeof(float));

    check_true("allocated FSK synthetic buffers", i_samples && q_samples);

    double phase = 0.0;
    for (size_t n = 0; n < total_pairs; n++) {
        double t = (double)n / fs;
        int chip_idx = (int)(t / 0.000500);
        int high_tone = (chip_idx % 2 == 0); /* alternating chips */
        double f_inst = fc + (high_tone ? dev : -dev);
        phase += 2.0 * M_PI * f_inst / fs;
        i_samples[n] = (float)(90.0 * cos(phase) + 1.0 * test_noise());
        q_samples[n] = (float)(90.0 * sin(phase) + 1.0 * test_noise());
    }

    enum srd_modulation mod = srd_classify_modulation(i_samples, q_samples,
                                                       total_pairs, fs,
                                                       fc, 127.5f);
    check_int("constant-envelope alternating tones classify as 2-FSK",
              (int)mod, (int)SRD_MOD_FSK2);

    float *disc = malloc(total_pairs * sizeof(float));
    double work_rate = 0.0;
    check_true("allocated discriminator buffer", disc != NULL);
    size_t disc_n = srd_demodulate_fsk(i_samples, q_samples, total_pairs, fs,
                                       fc, disc, total_pairs, &work_rate);
    check_true("discriminator produced samples", disc_n > 1000);

    double threshold = srd_discriminator_threshold(disc, disc_n);
    struct srd_run runs[256];
    size_t run_count = srd_extract_runs(disc, disc_n, work_rate, threshold,
                                        runs, 256);
    check_true("discriminator threshold extracted runs", run_count >= 30);

    double chip_s = srd_chip_period(runs, run_count);
    check_close("chip period recovered from FSK discriminator is 500 us",
                chip_s * 1e6, 500.0, 25.0);

    /*
     * An ordinary OOK burst (test_envelope_demodulation_and_runs()'s
     * fixture) must not be misclassified as 2-FSK -- the two modulations
     * feed different demodulators and different frame extractors, so a
     * misclassification silently runs the wrong pipeline.
     */
    for (size_t n = 0; n < total_pairs; n++) {
        double t = (double)n / fs;
        int chip_idx = (int)(t / 0.000500);
        int on = (chip_idx % 2 == 0);
        double amp = on ? 100.0 : 0.5;
        double ph = 2.0 * M_PI * fc * t;
        i_samples[n] = (float)(amp * cos(ph));
        q_samples[n] = (float)(amp * sin(ph));
    }
    mod = srd_classify_modulation(i_samples, q_samples, total_pairs, fs,
                                  fc, 127.5f);
    check_int("keyed OOK is not misclassified as 2-FSK", (int)mod,
              (int)SRD_MOD_OOK);

    free(i_samples);
    free(q_samples);
    free(disc);
}

static void test_real_fsk_capture(void) {
    const char *path = "testfiles/srd_remote_control_fsk.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        check_true("required 2-FSK SRD remote-control capture is present", 0);
        return;
    }

    struct device_profile dev = device_profile_rtlsdr("probe", DEVICE_TUNER_R820T, NULL, 0);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);

    size_t pairs = (size_t)sz / dev.bytes_per_pair;
    if (pairs > 3000000)
        pairs = 3000000;
    unsigned char *raw = malloc(pairs * dev.bytes_per_pair);
    float *i_buf = malloc(pairs * sizeof(float));
    float *q_buf = malloc(pairs * sizeof(float));
    float *mag_buf = malloc(pairs * sizeof(float));

    if (raw && i_buf && q_buf && mag_buf &&
        fread(raw, dev.bytes_per_pair, pairs, f) == pairs) {
        sdr_dsp_convert_iq(&dev, raw, pairs * dev.bytes_per_pair, i_buf, q_buf, mag_buf, pairs);

        struct srd_transmission txs[64];
        int count = srd_find_transmissions(i_buf, q_buf, pairs, 2000000.0,
                                           dev.full_scale, 25.0, 0.050,
                                           txs, 64);
        check_true("real 2-FSK capture has transmissions", count > 0);

        /*
         * Every transmission in the capture is 2-FSK. This closes the loop on
         * srd_classify_modulation() against real hardware rather than a
         * synthetic fixture, per AGENTS.md's standing warning that a round
         * trip alone cannot establish that.
         */
        int all_fsk = 1;
        for (int t = 0; t < count; t++) {
            if (txs[t].modulation != SRD_MOD_FSK2) {
                all_fsk = 0;
                break;
            }
        }
        check_true("every real transmission classifies as 2-FSK",
                   all_fsk);

        if (count > 0) {
            /*
             * Demodulate the shortest transmission and recover its chip
             * period through srd_discriminator_threshold(). Per the doc's
             * burst schedule every real burst (wakeup or data) is 20-30 ms;
             * txs[0] here is actually a longer (~370 ms), unrelated carrier
             * that precedes the first button press, so picking by duration
             * rather than index is what keeps this pinned to a real remote
             * burst rather than whatever the survey happens to find first.
             * The doc's chip-period spec is ~64 us.
             */
            int shortest = -1;
            for (int t = 0; t < count; t++) {
                if (txs[t].duration_seconds < 0.05 &&
                    (shortest < 0 ||
                     txs[t].duration_seconds < txs[shortest].duration_seconds))
                    shortest = t;
            }
            check_true("a real remote-control burst (< 50 ms) is present",
                       shortest >= 0);

            if (shortest >= 0) {
                size_t cap = txs[shortest].pair_count / 2 + 2000;
                float *disc = malloc(cap * sizeof(float));
                double work_rate = 0.0;
                if (disc) {
                    size_t n = srd_demodulate_fsk(i_buf + txs[shortest].offset_pairs,
                                                  q_buf + txs[shortest].offset_pairs,
                                                  txs[shortest].pair_count, 2000000.0,
                                                  txs[shortest].carrier_hz, disc, cap,
                                                  &work_rate);
                    double th = srd_discriminator_threshold(disc, n);
                    struct srd_run runs[8192];
                    size_t r = srd_extract_runs(disc, n, work_rate, th, runs, 8192);
                    double chip_s = srd_chip_period(runs, r);
                    check_close("real 2-FSK capture chip period is ~64 us",
                                chip_s * 1e6, 64.0, 5.0);
                    free(disc);
                }
            }
        }
    }
    free(raw);
    free(i_buf);
    free(q_buf);
    free(mag_buf);
    fclose(f);
}

int main(void) {
    test_manchester_decode_basic();
    test_manchester_phase_auto_detect();
    test_manchester_violations();
    test_chip_period_recovery_from_runs();
    test_chip_period_survives_subchip_noise();
    test_a_stream_of_noise_has_no_chip_period();
    test_chip_coverage_measure();
    test_the_sampling_grid_is_not_a_chip_period();
    test_where_the_receiver_has_to_be();
    test_where_a_tuning_arrow_lands();
    test_envelope_demodulation_and_runs();
    test_find_transmissions();
    test_fsk_classification_and_demodulation();
    test_refusals_and_edge_cases();
    test_real_capture();
    test_real_fsk_capture();
    return check_report("SRD 430-440 MHz OOK/Manchester DSP");
}
