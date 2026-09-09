/*
 * Where the two-cell separation stops working, swept.
 *
 * This is not a check and asserts nothing; it prints a table so a fixture
 * level can be chosen by measurement. It is compiled from
 * `tests/lte_dsp_test.c` itself, with -DLTE_TWO_CELL_SWEEP, and that is the
 * whole point of the arrangement: an earlier standalone harness copied
 * `build_two_cell_carrier()` into a file of its own and found **zero cells at
 * every level**, because the fixture depends on setup that lives in the
 * suite's `main`. A sweep that finds nothing everywhere will endorse any
 * fixture level at all, so this one refuses to sweep until it has reproduced
 * the two answers the suite already pins -- two cells at 0.85, one at 0.45.
 *
 * `--diagnose` names what the copy was missing, by breaking each condition
 * deliberately and reporting what the fixture then reads.
 */

/* The module compiled in rather than linked, so the sweep can report what
   each Zadoff-Chu root scored -- which is the only place a lost cell can be
   told from a rejected one. This is the pattern the probe targets use. */
#include "lte_dsp.c"

static int two_cell_count(float amplitude, struct lte_cell *cells) {
    build_two_cell_carrier(amplitude);
    return lte_cell_search_all(buffer_i, buffer_q, BUFFER_SAMPLES,
                               LTE_SAMPLE_RATE_HZ, g_probe_device.full_scale,
                               cells, LTE_MAX_CELLS_PER_CARRIER, NULL);
}

/* A hash of the fixture's own bytes. If two compilers disagree about this,
   the divergence is in the fixture and not in the module under test. */
static unsigned long buffer_hash(void) {
    unsigned long h = 1469598103934665603UL;
    size_t k;
    for (k = 0; k < BUFFER_SAMPLES; k++) {
        unsigned char *p;
        float v[2];
        size_t b;
        v[0] = buffer_i[k];
        v[1] = buffer_q[k];
        p = (unsigned char *)v;
        for (b = 0; b < sizeof(v); b++) {
            h ^= p[b];
            h *= 1099511628211UL;
        }
    }
    return h;
}

static double amplitude_db(double amplitude) {
    return 20.0 * log10(amplitude);
}

/* The suite's own setup, in one place, so the sweep cannot drift from it. */
static void sweep_setup(void) {
    g_probe_device = device_profile_rtlsdr("check", NULL, 0);
    twiddles_init();
}

/*
 * The conditions, checked against answers that are *not* in dispute.
 *
 * An earlier gate here asserted the suite's own two-cell count at 0.85, and
 * that was the wrong thing to hold a sweep to: it is the answer under
 * investigation, so under clang the gate refused to run the sweep that would
 * have explained the refusal. What has to be established before a sweep is
 * worth reading is that the fixture is *alive* -- that it builds a carrier
 * with cells on it -- and both compilers agree about that.
 */
static int conditions_hold(void) {
    struct lte_cell cells[LTE_MAX_CELLS_PER_CARRIER];
    int weak = two_cell_count(0.45f, cells);
    int weak_pci = weak > 0 ? cells[0].pci : -1;
    int equal = two_cell_count(1.00f, cells);

    build_two_cell_carrier(0.85f);
    printf("conditions    fixture hash at 0.85 = %016lx\n", buffer_hash());
    printf("conditions    0.45 (%.2f dB) -> %d cell, PCI %d; expected 1, 190\n",
           amplitude_db(0.45), weak, weak_pci);
    printf("conditions    1.00 (0.00 dB)  -> %d cells; expected 2\n", equal);
    if (weak == 1 && weak_pci == 190 && equal == 2) {
        printf("conditions    the fixture is alive; the sweep below is "
               "worth reading\n\n");
        return 1;
    }
    printf("conditions    NOT met -- a sweep over a silent fixture would "
           "endorse any level at all, so it is not run\n");
    return 0;
}

/*
 * What the standalone harness was missing. Each line breaks one condition and
 * reports what the same fixture then reads at 0.85, where the suite reads 2.
 */
static void diagnose(void) {
    struct lte_cell cells[LTE_MAX_CELLS_PER_CARRIER];
    int n;

    sweep_setup();
    n = two_cell_count(0.85f, cells);
    printf("diagnose      everything set up            -> %d cells\n", n);

    memset(twiddle_re, 0, sizeof(twiddle_re));
    memset(twiddle_im, 0, sizeof(twiddle_im));
    n = two_cell_count(0.85f, cells);
    printf("diagnose      without twiddles_init()      -> %d cells\n", n);

    sweep_setup();
    g_probe_device.full_scale = 0.0f;
    n = two_cell_count(0.85f, cells);
    printf("diagnose      with full_scale 0            -> %d cells\n", n);

    sweep_setup();
}

static unsigned long hash_bytes(const void *data, size_t bytes) {
    const unsigned char *p = (const unsigned char *)data;
    unsigned long h = 1469598103934665603UL;
    size_t b;
    for (b = 0; b < bytes; b++) {
        h ^= p[b];
        h *= 1099511628211UL;
    }
    return h;
}

/* Which stage of the fixture two compilers stop agreeing at. */
static void fixture_stages(void) {
    sweep_setup();
    printf("stage  twiddles          %016lx\n",
           hash_bytes(twiddle_re, sizeof(twiddle_re)) ^
               hash_bytes(twiddle_im, sizeof(twiddle_im)));
    {
        /* build_buffer's grid steps, one at a time. */
        float pss_re[LTE_SYNC_SUBCARRIERS], pss_im[LTE_SYNC_SUBCARRIERS];
        float sss[LTE_SYNC_SUBCARRIERS];
        float crs_re[2 * LTE_FFT_SIZE], crs_im[2 * LTE_FFT_SIZE];
        int got;
        lte_pss_sequence(402 % 3, pss_re, pss_im);
        printf("stage  pss sequence      %016lx\n",
               hash_bytes(pss_re, sizeof(pss_re)) ^
                   hash_bytes(pss_im, sizeof(pss_im)));
        lte_sss_sequence(402 / 3, 402 % 3, 0, sss);
        printf("stage  sss sequence      %016lx\n",
               hash_bytes(sss, sizeof(sss)));
        got = lte_crs_sequence(402, 1, 0, 0, 0, crs_re, crs_im);
        printf("stage  crs sequence (%2d) %016lx\n", got,
               hash_bytes(crs_re, sizeof(float) * (size_t)(got > 0 ? got : 0)) ^
                   hash_bytes(crs_im,
                              sizeof(float) * (size_t)(got > 0 ? got : 0)));
        memset(grid_re, 0, sizeof(grid_re));
        memset(grid_im, 0, sizeof(grid_im));
        rng_seed(61u);
        fill_other_traffic();
        printf("stage  after traffic     %016lx\n",
               hash_bytes(grid_re, sizeof(grid_re)) ^
                   hash_bytes(grid_im, sizeof(grid_im)));
        place_sync(402);
        printf("stage  after sync        %016lx\n",
               hash_bytes(grid_re, sizeof(grid_re)) ^
                   hash_bytes(grid_im, sizeof(grid_im)));
        place_broadcast(402, 2);
        printf("stage  after broadcast   %016lx\n",
               hash_bytes(grid_re, sizeof(grid_re)) ^
                   hash_bytes(grid_im, sizeof(grid_im)));
    }
    build_buffer(402, 2, 0.0, 0.0, 61u);
    printf("stage  pbch bits         %016lx\n",
           hash_bytes(pbch_bits, sizeof(pbch_bits)));
    printf("stage  grid, no noise    %016lx\n",
           hash_bytes(grid_re, sizeof(grid_re)) ^
               hash_bytes(grid_im, sizeof(grid_im)));
    printf("stage  buffer, no noise  %016lx\n", buffer_hash());
    build_buffer(402, 2, 0.0, 0.004, 61u);
    printf("stage  buffer with noise %016lx\n", buffer_hash());
    build_two_cell_carrier(0.85f);
    printf("stage  two-cell fixture  %016lx\n", buffer_hash());
}

/*
 * The same construction over other draws of the interfering traffic.
 *
 * One seed is one carrier, and a check that passes on it is a claim about
 * that carrier. Whether the *capability* exists is how often two cells come
 * back across many of them.
 */
static void seed_sweep(int seeds) {
    struct lte_cell cells[LTE_MAX_CELLS_PER_CARRIER];
    const float levels[4] = { 1.00f, 0.92f, 0.85f, 0.45f };
    int counts[4][4], right[4], stronger[4];
    int s, l;

    memset(counts, 0, sizeof(counts));
    memset(right, 0, sizeof(right));
    memset(stronger, 0, sizeof(stronger));
    for (s = 0; s < seeds; s++) {
        two_cell_seed_offset = (uint32_t)(s * 2);
        for (l = 0; l < 4; l++) {
            int found = two_cell_count(levels[l], cells);
            if (found >= 0 && found < 4)
                counts[l][found]++;
            if (found == 2 &&
                ((cells[0].pci == 190 && cells[1].pci == 402) ||
                 (cells[0].pci == 402 && cells[1].pci == 190)))
                right[l]++;
            if (found == 1 && cells[0].pci == 190)
                stronger[l]++;
        }
    }
    two_cell_seed_offset = 0;
    printf("%d draws of the interfering traffic, same two identities\n\n",
           seeds);
    printf("%-10s %-8s %8s %8s %8s   %s\n", "level", "dB", "0 cells",
           "1 cell", "2 cells", "when 2, both right / when 1, it is 190");
    for (l = 0; l < 4; l++)
        printf("%-10.2f %-8.2f %8d %8d %8d   %d / %d\n", (double)levels[l],
               amplitude_db(levels[l]), counts[l][0], counts[l][1],
               counts[l][2], right[l], stronger[l]);
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--seeds") == 0) {
        sweep_setup();
        seed_sweep(argc > 2 ? atoi(argv[2]) : 40);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--fixture") == 0) {
        fixture_stages();
        return 0;
    }
    struct lte_cell cells[LTE_MAX_CELLS_PER_CARRIER];
    double amplitude;

    if (argc > 1 && strcmp(argv[1], "--diagnose") == 0) {
        diagnose();
        return 0;
    }

    sweep_setup();
    if (!conditions_hold() &&
        !(argc > 1 && strcmp(argv[1], "--force") == 0)) {
        printf("\n(--force sweeps anyway, which is what says whether the "
               "capability is absent or merely narrow)\n");
        return 1;
    }

    printf("%-9s %-7s %-5s %-12s  %s\n", "amplitude", "dB", "cells",
           "identities", "per root: peak (detected) -> cell");
    for (amplitude = 1.00; amplitude > 0.399; amplitude -= 0.01) {
        struct lte_pss_result per_root[LTE_N_ID_2_COUNT], best;
        int found = two_cell_count((float)amplitude, cells);
        char ids[32];
        int n, len = 0;

        ids[0] = '\0';
        for (n = 0; n < found && len < (int)sizeof(ids) - 6; n++)
            len += snprintf(ids + len, sizeof(ids) - (size_t)len, "%s%d",
                            n ? "+" : "", cells[n].pci);
        printf("%-9.2f %-7.2f %-5d %-12s ", amplitude,
               amplitude_db(amplitude), found, ids);

        /* The buffer is still the one just built, so this reads the same
           carrier the count above was taken from. */
        if (pss_detect_scan(buffer_i, buffer_q, BUFFER_SAMPLES,
                            LTE_SAMPLE_RATE_HZ, &best, per_root, NULL) > 0) {
            int r;
            for (r = 0; r < LTE_N_ID_2_COUNT; r++) {
                struct lte_cell cell;
                float coh[LTE_PORT_COUNT];
                int got = per_root[r].detected &&
                          cell_from_pss(buffer_i, buffer_q, BUFFER_SAMPLES,
                                        LTE_SAMPLE_RATE_HZ, per_root[r],
                                        &cell, NULL) == 1;
                if (!got) {
                    printf(" r%d %.3f -           ", r,
                           (double)per_root[r].peak);
                    continue;
                }
                coh[0] = 0.0f;
                lte_port_coherence(buffer_i, buffer_q, BUFFER_SAMPLES,
                                   LTE_SAMPLE_RATE_HZ, &cell, coh);
                printf(" r%d %.3f %3d coh %.3f", r, (double)per_root[r].peak,
                       cell.pci, (double)coh[0]);
            }
        }
        printf("\n");
    }
    return 0;
}
