/*
 * check-device-profile -- the data contract for a receiver.
 *
 * `.scratch/device-model/issues/02-the-device-profile.md`. The profile carries
 * the facts that do not transfer between one receiver and another, and this
 * suite's job is mostly to pin each of them **against the constant it will
 * replace**, so the two cannot drift apart while both exist. Tickets 03 to 06
 * delete the originals one area at a time; until then a change to either side
 * fails here.
 *
 * That is why the suite includes `survey_sweep.h`, `survey_bands.h` and
 * `survey_suspect.h` rather than repeating their numbers: a check that quoted
 * 24 MHz on both sides would pass while the program disagreed with itself.
 * `acquisition.h` is the one it cannot include -- it pulls `<rtl-sdr.h>`, and
 * a check here links `-lm` and nothing else -- so the block size is written
 * out, with the same comment `sample_format_test.c` carries.
 *
 * Nothing reads the profile yet. This suite is the only consumer, deliberately.
 */

#include "check.h"

#include "device_profile.h"
#include "survey_sweep.h"
#include "survey_suspect.h"

/* SAMPLE_BLOCK_BYTES (acquisition.h), which cannot be included here. */
#define BLOCK_BYTES (16 * 16384)

/* What an R820T reports, in tenths of a dB. Enough of the real list to be a
   real list; the point is the shape, not the tuner. */
static const int R820T_GAINS[] = {0,   9,   14,  27,  37,  77,  87,  125,
                                  144, 157, 166, 197, 207, 229, 254, 280,
                                  297, 328, 338, 364, 372, 386, 402, 421,
                                  434, 439, 445, 480, 496};
#define R820T_GAIN_COUNT ((int)(sizeof(R820T_GAINS) / sizeof(R820T_GAINS[0])))

/*
 * The RTL profile is today's program, restated. Every number is pinned against
 * where it lives now, so this fails if either side moves alone.
 */
static void test_rtlsdr_reproduces_todays_constants(void) {
    struct device_profile p =
        device_profile_rtlsdr("RTL-SDR (R820T)", R820T_GAINS, R820T_GAIN_COUNT);

    check_true("the profile is coherent", device_profile_valid(&p));
    check_str("it says what it is", p.name, "RTL-SDR (R820T)");

    check_int("format is unsigned 8-bit", (int)p.format, SAMPLE_FORMAT_U8);
    check_close("full scale is 127.5", p.full_scale, 127.5, 1e-9);
    check_size("two bytes a pair", p.bytes_per_pair, 2);

    /*
     * These were pinned against SURVEY_TUNER_LOWER_HZ / _UPPER_HZ in
     * survey_bands.h. Ticket 05 deleted those: the profile *is* the
     * definition now, so there is nothing left to cross-check against and the
     * numbers are stated here instead. What keeps them honest is
     * check-survey-bands, which runs its both-directions property against this
     * profile and against a wideband one.
     */
    check_close("an R820T reaches 24 MHz", p.tune_lower_hz, 24.0e6, 0.5);
    check_close("up to 1766 MHz", p.tune_upper_hz, 1766.0e6, 0.5);

    check_close("settle is SURVEY_SETTLE_SECONDS", p.settle_seconds,
                SURVEY_SETTLE_SECONDS, 1e-9);
    /*
     * This was pinned against `RECEIVER_REFERENCE_HZ` in survey_suspect.h.
     * That constant is gone: the profile *is* the reference now, and
     * `survey_comb_spacing_hz()` derives the comb from whatever it is handed
     * (ticket 08). So the number is stated here, and the comb is checked as a
     * function of it rather than against a second copy.
     */
    check_close("an RTL2832U's crystal is 28.8 MHz", p.reference_clock_hz,
                28.8e6, 0.5);
    check_close("and the comb it puts out is that halved",
                survey_comb_spacing_hz(p.reference_clock_hz), 14.4e6, 0.5);
    check_close("with a finer one at a eighteenth",
                survey_fine_comb_spacing_hz(p.reference_clock_hz), 1.6e6, 0.5);

    check_true("it can retune", p.can_retune != 0);
    check_true("it has a ppm correction", p.has_ppm_correction != 0);
    check_true("and a crystal, so it drifts", p.ppm_drifts != 0);

    check_true("the house rate is inside its range",
               p.rate_min_hz <= 2000000u && 2000000u <= p.rate_max_hz);
    check_true("and so is LTE's 1.92 MS/s",
               p.rate_min_hz <= 1920000u && 1920000u <= p.rate_max_hz);
    check_true("and the RDS capture's 2.048",
               p.rate_min_hz <= 2048000u && 2048000u <= p.rate_max_hz);
}

/*
 * The block arithmetic, which is ticket 04's whole subject.
 *
 * The RTL row is today's answer and must not move. The 12-in-16 row is what
 * `SAMPLE_BLOCK_BYTES / 2` would get wrong on the day a device delivers it:
 * the same block, the same rate, half the pairs and half the time, with
 * nothing erroring.
 */
static void test_block_arithmetic_follows_the_container(void) {
    struct device_profile rtl = device_profile_rtlsdr(NULL, NULL, 0);
    struct device_profile wide = device_profile_capture(
        "12-in-16", SAMPLE_FORMAT_S16, 2047.5f, 800.0e6, 2000000);

    check_size("131072 pairs a block at two bytes",
               device_pairs_per_block(&rtl, BLOCK_BYTES), 131072);
    check_close("65.5 ms of signal at 2 MS/s",
                device_block_seconds(&rtl, BLOCK_BYTES, 2.0e6), 0.065536,
                1e-6);

    check_size("65536 pairs at four bytes",
               device_pairs_per_block(&wide, BLOCK_BYTES), 65536);
    check_close("and 32.8 ms, for the same block and rate",
                device_block_seconds(&wide, BLOCK_BYTES, 2.0e6), 0.032768,
                1e-6);

    /* The trap stated directly: today's formula is a bytes-per-pair
       assumption, and it is right only for the device that is here now. */
    check_size("SAMPLE_BLOCK_BYTES / 2 matches the RTL profile",
               (size_t)(BLOCK_BYTES / 2),
               device_pairs_per_block(&rtl, BLOCK_BYTES));
    check_true("and is twice the truth on a four-byte format",
               (size_t)(BLOCK_BYTES / 2) ==
                   2 * device_pairs_per_block(&wide, BLOCK_BYTES));

    /* LTE's rate is not the house rate, and its budget is not 65.5 ms. */
    check_close("LTE's block is 68.3 ms at 1.92 MS/s",
                device_block_seconds(&rtl, BLOCK_BYTES, 1.92e6), 0.0682667,
                1e-6);

    check_size("a zero rate gives no pairs",
               device_pairs_per_block(NULL, BLOCK_BYTES), 0);
    check_close("and no seconds", device_block_seconds(&rtl, BLOCK_BYTES, 0.0),
                0.0, 1e-12);
}

/*
 * The other direction, and the one acquisition actually uses.
 *
 * Ticket 09 made the **pair count** the invariant: a block is always
 * SAMPLE_BLOCK_PAIRS pairs, so it is always the same amount of signal, and
 * the byte count is what varies with the container. Before that a block was a
 * byte count, which meant a four-byte container covered half the time -- and
 * that cost `gsm_arfcn_69` five of its seven broadcast messages and LTE 55%
 * more processing for twice as many half-length blocks.
 */
static void test_a_block_is_the_same_signal_on_every_container(void) {
    struct device_profile rtl = device_profile_rtlsdr(NULL, NULL, 0);
    struct device_profile wide = device_profile_capture(
        "12-in-16", SAMPLE_FORMAT_S16, 2047.5f, 800.0e6, 2000000);
    const size_t pairs = 131072; /* SAMPLE_BLOCK_PAIRS */

    check_size("8-bit: a block is 262144 bytes",
               device_block_bytes(&rtl, pairs), 262144);
    check_size("16-bit: the same block is 524288",
               device_block_bytes(&wide, pairs), 524288);
    check_true("so the byte count follows the container",
               device_block_bytes(&rtl, pairs) !=
                   device_block_bytes(&wide, pairs));

    /* And the point of it: the same amount of signal either way. */
    check_close("8-bit: 65.5 ms at 2 MS/s",
                device_block_seconds(&rtl, device_block_bytes(&rtl, pairs),
                                     2.0e6),
                0.065536, 1e-6);
    check_close("16-bit: 65.5 ms too, which is the whole point",
                device_block_seconds(&wide, device_block_bytes(&wide, pairs),
                                     2.0e6),
                0.065536, 1e-6);

    /* Round trips, both ways. */
    check_size("bytes back to pairs, 8-bit",
               device_pairs_per_block(&rtl, device_block_bytes(&rtl, pairs)),
               pairs);
    check_size("bytes back to pairs, 16-bit",
               device_pairs_per_block(&wide, device_block_bytes(&wide, pairs)),
               pairs);

    check_size("a null profile has no block", device_block_bytes(NULL, pairs),
               0);
}

/*
 * Full scale is carried, not derived, and this is the check that says why.
 *
 * Ticket 01 measured it: its rescaled corpus is 8-bit data shifted left by
 * four, so full scale is 127.5 * 16 = 2040.0, while a real 12-bit part rails
 * at 2047.5. **Both are SAMPLE_FORMAT_S16.** A profile that derived full scale
 * from the format would have to pick one and would be wrong about the other,
 * and normalising the corpus by 2047.5 agrees with none of the 256 byte
 * values.
 */
static void test_two_s16_sources_disagree_about_full_scale(void) {
    struct device_profile corpus = device_profile_capture(
        "rescaled corpus", SAMPLE_FORMAT_S16, 2040.0f, 948.4e6, 2000000);
    struct device_profile part = device_profile_capture(
        "a real 12-bit part", SAMPLE_FORMAT_S16, 2047.5f, 948.4e6, 2000000);

    check_int("same format", (int)corpus.format, (int)part.format);
    check_size("same bytes a pair", corpus.bytes_per_pair,
               part.bytes_per_pair);
    check_true("different full scale", corpus.full_scale != part.full_scale);
    check_close("the corpus is 127.5 * 16", corpus.full_scale, 2040.0, 1e-9);
    check_close("the part is a rail short of 2048", part.full_scale, 2047.5,
                1e-9);

    /* So the format cannot supply one, and says so rather than guessing. */
    check_close("S16 has no default full scale",
                device_default_full_scale(SAMPLE_FORMAT_S16), 0.0, 1e-12);
    check_close("U8 does, and it is 127.5",
                device_default_full_scale(SAMPLE_FORMAT_U8), 127.5, 1e-9);
    check_close("CF32 does, and it is 1.0",
                device_default_full_scale(SAMPLE_FORMAT_CF32), 1.0, 1e-9);

    check_size("U8 is two bytes a pair",
               device_format_bytes_per_pair(SAMPLE_FORMAT_U8), 2);
    check_size("S16 is four", device_format_bytes_per_pair(SAMPLE_FORMAT_S16),
               4);
    check_size("CF32 is eight",
               device_format_bytes_per_pair(SAMPLE_FORMAT_CF32), 8);
}

/*
 * A capture is at one place on the band and cannot be moved, which is what the
 * code already enforces. The profile states it rather than leaving it to a
 * refusal deep in a retune path.
 */
static void test_a_capture_refuses_to_retune(void) {
    struct device_profile c = device_profile_capture(
        "gsm_arfcn_69", SAMPLE_FORMAT_U8, 127.5f, 948.4e6, 2000000);

    check_true("the profile is coherent", device_profile_valid(&c));
    check_true("it cannot retune", c.can_retune == 0);
    check_close("its range is the one frequency it holds, below",
                c.tune_lower_hz, 948.4e6, 0.5);
    check_close("and above", c.tune_upper_hz, 948.4e6, 0.5);
    check_int("its rate is fixed", (int)c.rate_min_hz, (int)c.rate_max_hz);

    check_true("nothing to settle", c.settle_seconds == 0.0);
    check_int("and no gain to set", (int)c.gain_model, GAIN_MODEL_NONE);

    /*
     * No reference clock, and this is not an omission. Whichever device
     * recorded it had one, but the file does not, so nothing downstream may
     * attribute a comb to this source.
     */
    check_close("no reference clock", c.reference_clock_hz, 0.0, 1e-12);
    check_true("and no ppm correction left to make",
               c.has_ppm_correction == 0);
}

/* A list and a range are both representable, and tell each other apart. */
static void test_both_gain_models(void) {
    struct device_profile listed =
        device_profile_rtlsdr(NULL, R820T_GAINS, R820T_GAIN_COUNT);

    check_int("a tuner list is a list", (int)listed.gain_model,
              GAIN_MODEL_LIST);
    check_int("with every entry", listed.gain_count, R820T_GAIN_COUNT);
    check_int("in tenths of a dB", (int)listed.gain_unit,
              GAIN_UNIT_TENTHS_DB);
    check_int("lowest is 0.0 dB", listed.gain_list[0], 0);
    check_int("highest is 49.6", listed.gain_list[R820T_GAIN_COUNT - 1], 496);

    /* An AD9361 has a continuous range in dB; the same struct holds it. */
    struct device_profile ranged = device_profile_rtlsdr(NULL, NULL, 0);
    ranged.gain_model = GAIN_MODEL_RANGE;
    ranged.gain_unit = GAIN_UNIT_DB;
    ranged.gain_min = 0.0;
    ranged.gain_max = 76.0;
    ranged.gain_step = 1.0;

    check_true("a range is coherent", device_profile_valid(&ranged));
    check_true("and distinguishable from a list",
               ranged.gain_model != listed.gain_model);
    check_close("0 to 76 dB", ranged.gain_max - ranged.gain_min, 76.0, 1e-9);

    /* Asked with no list, the profile carries no gain model rather than an
       invented one -- an empty list would offer a settings panel nothing. */
    struct device_profile unasked = device_profile_rtlsdr(NULL, NULL, 0);
    check_int("no list asked for, no model", (int)unasked.gain_model,
              GAIN_MODEL_NONE);
    check_int("and no entries", unasked.gain_count, 0);
}

/*
 * A list too long is refused, not truncated. A settings panel silently short
 * one entry is a gain nobody can select and nothing that says so.
 */
static void test_an_oversized_gain_list_is_refused(void) {
    struct device_profile p = device_profile_rtlsdr(NULL, NULL, 0);
    int many[DEVICE_GAIN_LIST_MAX + 1];
    for (int i = 0; i < DEVICE_GAIN_LIST_MAX + 1; i++)
        many[i] = i;

    check_int("exactly full fits",
              device_profile_set_gain_list(&p, many, DEVICE_GAIN_LIST_MAX), 0);
    check_int("with every entry", p.gain_count, DEVICE_GAIN_LIST_MAX);
    check_int("and the last one is the last one",
              p.gain_list[DEVICE_GAIN_LIST_MAX - 1], DEVICE_GAIN_LIST_MAX - 1);

    struct device_profile q = device_profile_rtlsdr(NULL, NULL, 0);
    check_int("one more is refused",
              device_profile_set_gain_list(&q, many, DEVICE_GAIN_LIST_MAX + 1),
              -1);
    check_int("and nothing was written", q.gain_count, 0);
    check_int("nor a model implied", (int)q.gain_model, GAIN_MODEL_NONE);

    check_int("a null list is refused",
              device_profile_set_gain_list(&q, NULL, 4), -1);
    check_int("an empty one too",
              device_profile_set_gain_list(&q, many, 0), -1);
    check_int("and a negative count",
              device_profile_set_gain_list(&q, many, -1), -1);
    check_int("a null profile is refused",
              device_profile_set_gain_list(NULL, many, 4), -1);
}

/* The incoherent profiles a hand-filled struct produces. */
static void test_validity_catches_a_mismatched_struct(void) {
    struct device_profile good = device_profile_rtlsdr(NULL, NULL, 0);
    check_true("the RTL profile is coherent", device_profile_valid(&good));
    check_true("a null one is not", device_profile_valid(NULL) == 0);

    struct device_profile p = good;
    p.bytes_per_pair = 4; /* claims S16's width under U8 */
    check_true("a format at odds with its width", device_profile_valid(&p) == 0);

    p = good;
    p.full_scale = 0.0f;
    check_true("no full scale", device_profile_valid(&p) == 0);

    p = good;
    p.tune_lower_hz = p.tune_upper_hz + 1.0;
    check_true("a tuning range the wrong way round",
               device_profile_valid(&p) == 0);

    p = good;
    p.rate_min_hz = p.rate_max_hz + 1;
    check_true("a rate range the wrong way round",
               device_profile_valid(&p) == 0);

    p = good;
    p.settle_seconds = -0.1;
    check_true("a negative settle", device_profile_valid(&p) == 0);

    p = good;
    p.gain_model = GAIN_MODEL_LIST; /* with gain_count still 0 */
    check_true("a list model with no list", device_profile_valid(&p) == 0);

    p = good;
    p.gain_model = GAIN_MODEL_RANGE;
    p.gain_min = 40.0;
    p.gain_max = 10.0;
    check_true("a range model with no range", device_profile_valid(&p) == 0);

    /* A capture built the same way stays coherent. */
    struct device_profile c = device_profile_capture(
        "x", SAMPLE_FORMAT_S16, 2040.0f, 100.0e6, 1920000);
    check_true("a rescaled capture is coherent", device_profile_valid(&c));
}

/* A name longer than the field is cut, not written past. */
static void test_a_long_name_is_bounded(void) {
    struct device_profile p = device_profile_rtlsdr(NULL, NULL, 0);
    char lengthy[DEVICE_NAME_MAX * 2];
    memset(lengthy, 'x', sizeof(lengthy) - 1);
    lengthy[sizeof(lengthy) - 1] = '\0';

    device_profile_set_name(&p, lengthy);
    check_size("cut to the field", strlen(p.name), DEVICE_NAME_MAX - 1);
    check_true("and terminated", p.name[DEVICE_NAME_MAX - 1] == '\0');

    device_profile_set_name(&p, NULL);
    check_str("a null name empties it", p.name, "");

    struct device_profile d = device_profile_rtlsdr(NULL, NULL, 0);
    check_str("the default names the family", d.name, "RTL-SDR");
    struct device_profile c =
        device_profile_capture(NULL, SAMPLE_FORMAT_U8, 127.5f, 1.0e8, 2000000);
    check_str("and a capture says so", c.name, "capture");
}

int main(void) {
    test_rtlsdr_reproduces_todays_constants();
    test_block_arithmetic_follows_the_container();
    test_a_block_is_the_same_signal_on_every_container();
    test_two_s16_sources_disagree_about_full_scale();
    test_a_capture_refuses_to_retune();
    test_both_gain_models();
    test_an_oversized_gain_list_is_refused();
    test_validity_catches_a_mismatched_struct();
    test_a_long_name_is_bounded();
    return check_report("what a receiver is, in the terms the numbers need");
}
