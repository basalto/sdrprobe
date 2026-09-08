/*
 * check-sample-format -- widening the sample container moves no answer.
 *
 * `.scratch/device-model/issues/01-a-format-change-moves-no-answer.md`. Every
 * later ticket in that spec changes how a sample is represented, and this is
 * the gate they are measured against: the same signal in an 8-bit and a 16-bit
 * container, through the format layer, has to arrive as the same floats.
 *
 * **Why comparing floats settles it for the whole program.** The program has
 * exactly one byte-to-float seam -- `sdr_dsp_convert_iq()`, called once, at
 * `sdrprobe.c:311`. Everything downstream of it takes floats: the byte-taking
 * `fm_discriminate()` still exists but no longer has a caller outside tests,
 * `view_fm.c` using `fm_discriminate_f()` instead. So if the two float streams
 * are identical, every decoded answer is identical by construction rather than
 * by measurement, and no capture needs to be decoded twice to establish it.
 * That is a stronger statement than running the decoders would give, and it is
 * available without the program being able to read a 16-bit file at all --
 * which it cannot, until tickets 02 and 03.
 *
 * **Both halves now go through the real format layer.** They did not at first:
 * the program could not read a 16-bit file, so this suite carried its own
 * converter and ticket 03 named replacing it as an acceptance criterion. That
 * has happened -- `sdr_dsp_convert_iq()` takes a `struct device_profile` and
 * reads the container, the full scale and the bytes per pair from it, so what
 * is compared below is the shipping code twice rather than the shipping code
 * against a copy of itself.
 *
 * The corpus is generated, never committed: `build/testfiles16/` is rebuilt
 * from `testfiles/` by `scripts/rescale_capture.c` as a prerequisite of this
 * rule. Nothing in `testfiles/` is touched.
 */

#include "check.h"
#include "sdr_dsp.h"

#include <stdint.h>

/* Pairs converted per pass. The captures run to twelve megabytes and there is
   no reason to hold one whole; the comparison is per-sample anyway. */
#define CHUNK_PAIRS 65536

/* Full scale of each container. 127.5 is the 8-bit convention this program has
   always used. 2040.0 is 127.5 * 16 -- what the rescaling actually produces,
   and NOT a 12-bit part's 2047.5. See `test_the_full_scale_that_matters`. */
#define FULL_SCALE_U8 127.5f /* device_default_full_scale(SAMPLE_FORMAT_U8) */
#define FULL_SCALE_S16 2040.0f

/* Block size, from the house convention in CLAUDE.md. Used only to show what
   ticket 04 is about; nothing here changes it. */
#define BLOCK_BYTES (16 * 16384)

/*
 * One profile per source, which is the whole point of there being a profile:
 * the same bytes mean different things and only the source knows which. The
 * 16-bit corpus rails at 2040.0 -- 127.5 * 16 -- and not at a real 12-bit
 * part's 2047.5; see `test_the_full_scale_that_matters`.
 */
static struct device_profile g_u8, g_s16;

static const char *const CAPTURES[] = {
    "gsm_arfcn_69", "gsm_arfcn_113", "adsb_cpr_pair",
    "lte_b20_pci28", "tetra_cc17",   "fm_rds_tsf",
};
#define CAPTURE_COUNT ((int)(sizeof(CAPTURES) / sizeof(CAPTURES[0])))

/* Scratch, static because these are megabytes and a check has no reason to
   put them on the stack. */
static uint8_t raw8[CHUNK_PAIRS * 2];
static uint8_t raw16[CHUNK_PAIRS * 4];
static float i8[CHUNK_PAIRS], q8[CHUNK_PAIRS], m8[CHUNK_PAIRS];
static float i16[CHUNK_PAIRS], q16[CHUNK_PAIRS], m16[CHUNK_PAIRS];

/*
 * The scaling is exact in both directions, and the floats it produces are
 * bit-identical -- not close, identical. `byte - 127.5` is a multiple of 0.5,
 * sixteen times it is a multiple of 8, and both normalisations are the
 * correctly rounded result of the same exact rational, because 2040 is exactly
 * 127.5 * 16 and sixteen is a power of two. Nothing rounds anywhere, so a
 * disagreement downstream would be a bug in the format layer and never
 * arithmetic.
 *
 * All 256 values, so the ends of the range are covered rather than assumed.
 */
static void test_scaling_is_exact_and_reversible(void) {
    int lossy = 0, not_reversible = 0, not_identical = 0;

    for (int b = 0; b < 256; b++) {
        float centred = (float)b - 127.5f;
        int scaled = (2 * b - 255) * 8; /* (b - 127.5) * 16, in integers */

        if ((float)scaled != centred * 16.0f)
            lossy++;
        if (scaled < -32768 || scaled > 32767)
            lossy++;
        /* Back to the byte it came from. */
        if ((scaled / 8 + 255) / 2 != b)
            not_reversible++;

        float from8 = centred / FULL_SCALE_U8;
        float from16 = (float)scaled / FULL_SCALE_S16;
        if (from8 != from16)
            not_identical++;
    }

    check_int("all 256 values scale without loss", lossy, 0);
    check_int("all 256 values scale back to their byte", not_reversible, 0);
    check_int("all 256 normalise bit-identically", not_identical, 0);
    check_int("full scale is 127.5 * 16", (int)(FULL_SCALE_U8 * 16.0f),
              (int)FULL_SCALE_S16);
    check_int("the extremes stay inside int16", (2 * 255 - 255) * 8, 2040);
}

/*
 * The full scale of this corpus is 2040.0, and a 12-bit part's 2047.5 is a
 * different number that belongs to a different thing.
 *
 * This is pinned because the ticket that asked for this check got it wrong,
 * and plausibly: `.scratch/device-model/issues/02-the-device-profile.md` names
 * 2047.5 as full scale for "12-in-16", which is right for an AD9361 and wrong
 * for these files. They hold 8-bit samples shifted into a 16-bit container, so
 * their full scale is whatever 8-bit full scale maps to. Normalising by 2047.5
 * instead agrees with nothing: 0 of 256 values match and the worst is off by
 * 3.7e-3 -- 366 times the 1e-6 the ticket allowed, so the check as specified
 * would have failed on every capture and looked like a decode fault.
 *
 * Recording 2047.5 in the generated sidecar would be a claim about hardware
 * this data never went through.
 */
static void test_the_full_scale_that_matters(void) {
    int matches_2040 = 0, matches_2047 = 0;
    double worst_2047 = 0.0;

    for (int b = 0; b < 256; b++) {
        float centred = (float)b - 127.5f;
        int scaled = (2 * b - 255) * 8;
        float from8 = centred / FULL_SCALE_U8;

        if ((float)scaled / FULL_SCALE_S16 == from8)
            matches_2040++;
        float wrong = (float)scaled / 2047.5f;
        if (wrong == from8)
            matches_2047++;
        double d = fabs((double)wrong - (double)from8);
        if (d > worst_2047)
            worst_2047 = d;
    }

    check_int("normalised by 2040.0, every value agrees", matches_2040, 256);
    check_int("normalised by 2047.5, none does", matches_2047, 0);
    check_msg(worst_2047 > 1e-6,
              "2047.5 is wrong by more than the ticket's tolerance: %.3e\n",
              worst_2047);
    check_close("and by this much", worst_2047, 3.663e-3, 1e-5);
}

/* Open a capture, failing the suite rather than the process if it is absent --
   `build/testfiles16/` is generated by the make rule, so a missing file means
   the rule did not run, and that is worth saying plainly. */
static FILE *open_capture(const char *dir, const char *name, const char *label) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
    FILE *f = fopen(path, "rb");
    check_msg(f != NULL, "%s: cannot open %s\n", label, path);
    return f;
}

/*
 * The whole of each capture, both containers, through the format layer.
 *
 * Whole rather than a prefix: an overflow would show at the ends of the range,
 * and which byte value sits where in a capture is not something to assume.
 * `test_byte_coverage` establishes that the ends are actually present.
 */
static void test_captures_agree(void) {
    for (int c = 0; c < CAPTURE_COUNT; c++) {
        const char *name = CAPTURES[c];
        FILE *f8 = open_capture("testfiles", name, name);
        FILE *f16 = open_capture("build/testfiles16", name, name);
        if (!f8 || !f16) {
            if (f8)
                fclose(f8);
            if (f16)
                fclose(f16);
            continue;
        }

        unsigned long long pairs_total = 0, mismatches = 0, mag_mismatches = 0;
        unsigned long long bytes8 = 0, bytes16 = 0;
        size_t short8 = 0, short16 = 0;
        double worst = 0.0;

        for (;;) {
            size_t got8 = fread(raw8, 1, sizeof(raw8), f8);
            size_t got16 = fread(raw16, 1, sizeof(raw16), f16);
            if (got8 == 0 && got16 == 0)
                break;
            bytes8 += got8;
            bytes16 += got16;

            size_t p8 = sdr_dsp_convert_iq(&g_u8, raw8, got8, i8, q8, m8,
                                           CHUNK_PAIRS);
            size_t p16 = sdr_dsp_convert_iq(&g_s16, raw16, got16, i16, q16,
                                            m16, CHUNK_PAIRS);
            /* A length disagreement is its own fault and not a value one --
               reporting it as a pair that differs sends a reader looking at
               the arithmetic when the file is simply the wrong size. */
            if (p8 != p16) {
                short8 = p8;
                short16 = p16;
                break;
            }

            for (size_t n = 0; n < p8; n++) {
                float a_i = i8[n] / FULL_SCALE_U8;
                float a_q = q8[n] / FULL_SCALE_U8;
                float b_i = i16[n] / FULL_SCALE_S16;
                float b_q = q16[n] / FULL_SCALE_S16;
                if (a_i != b_i || a_q != b_q) {
                    mismatches++;
                    double d = fabs((double)a_i - (double)b_i);
                    double e = fabs((double)a_q - (double)b_q);
                    if (d > worst)
                        worst = d;
                    if (e > worst)
                        worst = e;
                }
                if (m8[n] / FULL_SCALE_U8 != m16[n] / FULL_SCALE_S16)
                    mag_mismatches++;
            }
            pairs_total += p8;
        }
        fclose(f8);
        fclose(f16);

        check_msg(short8 == short16,
                  "%s: a chunk held %zu pairs against %zu\n", name, short8,
                  short16);
        check_msg(mismatches == 0,
                  "%s: %llu of %llu pairs disagree, worst %.3e\n", name,
                  mismatches, pairs_total, worst);
        check_msg(mag_mismatches == 0, "%s: %llu magnitudes disagree\n", name,
                  mag_mismatches);
        check_msg(pairs_total > 0, "%s: no pairs read\n", name);
        check_msg(bytes16 == 2 * bytes8,
                  "%s: %llu bytes became %llu, expected %llu\n", name, bytes8,
                  bytes16, 2 * bytes8);
    }
}

/*
 * The agreement above is only worth something if the corpus actually contains
 * the values where a container change would show. Counting them is the
 * difference between "every value agrees" and "every value that happens to be
 * here agrees".
 */
static void test_byte_coverage(void) {
    int seen[256];
    memset(seen, 0, sizeof(seen));

    for (int c = 0; c < CAPTURE_COUNT; c++) {
        FILE *f = open_capture("testfiles", CAPTURES[c], "coverage");
        if (!f)
            continue;
        size_t got;
        while ((got = fread(raw8, 1, sizeof(raw8), f)) > 0)
            for (size_t n = 0; n < got; n++)
                seen[raw8[n]] = 1;
        fclose(f);
    }

    int distinct = 0;
    for (int b = 0; b < 256; b++)
        distinct += seen[b];

    check_int("the corpus exercises every byte value", distinct, 256);
    check_true("including 0, the negative rail", seen[0] != 0);
    check_true("including 255, the positive rail", seen[255] != 0);
}

/*
 * The silent one, demonstrated rather than fixed -- it is ticket 04.
 *
 * `SAMPLE_BLOCK_PAIRS` is `SAMPLE_BLOCK_BYTES / 2`, and the 2 means bytes per
 * pair rather than anything about the block. The same signal in a four-byte
 * format has the same number of pairs and twice the bytes, so that formula
 * returns twice the truth, nothing errors, and every timing budget in
 * CLAUDE.md is wrong by a factor of two with nothing on screen to say so.
 */
static void test_pair_count_is_not_half_the_bytes(void) {
    size_t pairs_u8 = BLOCK_BYTES / 2;
    size_t pairs_s16 = BLOCK_BYTES / 4;

    check_size("a 256 KB block at two bytes a pair", pairs_u8, 131072);
    check_size("the same block at four", pairs_s16, 65536);
    check_true("so bytes / 2 is a bytes-per-pair assumption",
               pairs_u8 != pairs_s16);
    /* And what it costs: 65.5 ms of signal at 2 MS/s becomes 32.8. */
    check_close("65.5 ms a block becomes 32.8", (double)pairs_s16 / 2.0e6,
                0.0327, 0.0005);
}

/*
 * The profile is what makes a pair count a pair count. Today's formula --
 * `SAMPLE_BLOCK_BYTES / 2` -- is a two-byte format's answer, and the converter
 * no longer uses it: it divides by `bytes_per_pair`, so the same block of
 * bytes yields half as many pairs on a four-byte container.
 */
static void test_the_converter_counts_pairs_by_the_container(void) {
    static uint8_t block[BLOCK_BYTES];
    static float bi[BLOCK_BYTES / 2], bq[BLOCK_BYTES / 2],
        bm[BLOCK_BYTES / 2];
    memset(block, 0x80, sizeof(block));

    check_size("two bytes a pair fills the block",
               sdr_dsp_convert_iq(&g_u8, block, sizeof(block), bi, bq, bm,
                                  BLOCK_BYTES / 2),
               131072);
    check_size("four bytes a pair fills half of it",
               sdr_dsp_convert_iq(&g_s16, block, sizeof(block), bi, bq, bm,
                                  BLOCK_BYTES / 2),
               65536);

    /* And it refuses what it cannot lay out, rather than guessing. */
    struct device_profile unknown = g_u8;
    unknown.format = SAMPLE_FORMAT_CF32;
    unknown.bytes_per_pair = 8;
    check_size("a container nothing produces is refused",
               sdr_dsp_convert_iq(&unknown, block, sizeof(block), bi, bq, bm,
                                  BLOCK_BYTES / 2),
               0);
    check_size("and a null profile is too",
               sdr_dsp_convert_iq(NULL, block, sizeof(block), bi, bq, bm,
                                  BLOCK_BYTES / 2),
               0);
}

int main(void) {
    g_u8 = device_profile_capture("testfiles", SAMPLE_FORMAT_U8,
                                  device_default_full_scale(SAMPLE_FORMAT_U8),
                                  0.0, 2000000);
    g_s16 = device_profile_capture("build/testfiles16", SAMPLE_FORMAT_S16,
                                   FULL_SCALE_S16, 0.0, 2000000);

    test_scaling_is_exact_and_reversible();
    test_the_full_scale_that_matters();
    test_captures_agree();
    test_byte_coverage();
    test_pair_count_is_not_half_the_bytes();
    test_the_converter_counts_pairs_by_the_container();
    return check_report("a format change moves no answer");
}
