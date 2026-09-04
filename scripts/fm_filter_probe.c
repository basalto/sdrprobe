/*
 * Two biphase filters over the same samples, at a sweep of added noise.
 *
 * The question: the transmitted RDS pulse is band-limited, so a rectangular
 * biphase filter is mismatched to it by about a decibel in theory. A decibel
 * is invisible on a strong station -- measured once already as 153 aligned
 * syndrome hits against 153 -- and no station reachable from this site is
 * marginal: they either decode well or carry no RDS at all. So the only way
 * to see it is to take a real recording down towards the noise and watch
 * which filter fails first.
 *
 * Noise is added to the recorded I/Q rather than to the baseband, because
 * that is where a weak signal's noise actually enters and the FM
 * discriminator is nonlinear -- its output noise depends on the
 * carrier-to-noise ratio in a way that adding noise afterwards would not
 * reproduce.
 *
 * Not a test. It prints a table and decides nothing.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fm_dsp.h"
#include "rds.h"

#define CHUNK 32768
#define BITS 262144

static uint32_t rng_state = 12345u;

/* A repeatable normal deviate. Both filters must see the same noise, or the
   comparison measures the noise rather than the filters. */
static double gauss(void) {
    double u1, u2;

    rng_state = rng_state * 1664525u + 1013904223u;
    u1 = ((double)(rng_state >> 8) + 1.0) / 16777217.0;
    rng_state = rng_state * 1664525u + 1013904223u;
    u2 = (double)(rng_state >> 8) / 16777216.0;
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

struct result {
    long blocks;
    long groups;
    char ps[9];
};

static float mpx[CHUNK];
static float bb_i[CHUNK], bb_q[CHUNK];
static float soft[CHUNK];
static float bits[BITS];

static struct result run(const float *fi, const float *fq, size_t pairs,
                         double rate, enum fm_rds_filter filter) {
    struct fm_rds_front front;
    struct rds_station station;
    struct result r;
    size_t bit_count = 0, done = 0, bb_count = 0;

    memset(&station, 0, sizeof(station));
    fm_rds_front_init(&front, rate);
    while (done + CHUNK <= pairs) {
        size_t n = fm_discriminate_f(fi + done, fq + done, CHUNK, mpx, CHUNK);
        size_t got = fm_rds_front_feed(&front, mpx, n, bb_i + bb_count,
                                       bb_q + bb_count, CHUNK - bb_count);
        done += CHUNK;
        bb_count += got;
        if (bb_count >= CHUNK / 2) {
            int offset = 0;
            double axis = 0.0;
            size_t made = fm_rds_soft_bits_with(bb_i, bb_q, bb_count, filter,
                                                soft, CHUNK, &offset, &axis);
            size_t k;
            for (k = 0; k < made && bit_count < BITS; k++)
                bits[bit_count++] = soft[k];
            bb_count = 0;
        }
    }
    rds_decode(bits, bit_count, &station, NULL, 0);
    r.blocks = station.funnel.blocks_matched;
    r.groups = station.funnel.groups;
    snprintf(r.ps, sizeof(r.ps), "%s", station.ps_valid ? station.ps : "-");
    return r;
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "testfiles/fm_rds_tsf.bin";
    double rate = argc > 2 ? atof(argv[2]) : 2048000.0;
    /*
     * FM has a threshold: below roughly ten decibels of carrier to noise the
     * discriminator collapses and nothing downstream matters, so the whole
     * interesting region is above it and the steps have to be fine near the
     * cliff rather than spread evenly.
     */
    static const double noise[] = { -99.0, 16.0, 14.0, 13.0, 12.0, 11.0,
                                    10.0, 9.0, 8.0 };
    /* Several noise draws per point, because one draw at one ratio is an
       anecdote: the difference being looked for is a decibel, and a single
       seed can move a block count further than that on its own. */
    const int seeds = 6;
    FILE *f = fopen(path, "rb");
    uint8_t *raw;
    float *fi, *fq, *ni, *nq;
    size_t pairs, k, n;
    long bytes;
    double power = 0.0;

    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    pairs = (size_t)bytes / 2;
    raw = malloc((size_t)bytes);
    fi = malloc(pairs * sizeof(float));
    fq = malloc(pairs * sizeof(float));
    ni = malloc(pairs * sizeof(float));
    nq = malloc(pairs * sizeof(float));
    if (!raw || !fi || !fq || !ni || !nq ||
        fread(raw, 1, (size_t)bytes, f) != (size_t)bytes) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    fclose(f);
    for (k = 0; k < pairs; k++) {
        fi[k] = ((float)raw[2 * k] - 127.5f) / 127.5f;
        fq[k] = ((float)raw[2 * k + 1] - 127.5f) / 127.5f;
        power += (double)fi[k] * fi[k] + (double)fq[k] * fq[k];
    }
    power /= (double)pairs;

    printf("%s\n%zu pairs at %.0f S/s, %.2f s, mean power %.4f\n\n", path,
           pairs, rate, (double)pairs / rate, power);
    printf("   S/N    rectangular            shaped\n");
    printf("          blocks groups named    blocks groups named\n");
    for (n = 0; n < sizeof(noise) / sizeof(noise[0]); n++) {
        long ab = 0, ag = 0, an = 0, bb = 0, bg = 0, bn = 0;
        int trials = noise[n] > -90.0 ? seeds : 1;
        int t;

        for (t = 0; t < trials; t++) {
            struct result a, b;
            const float *ui = fi, *uq = fq;

            if (noise[n] > -90.0) {
                double sigma = sqrt(power) * pow(10.0, -noise[n] / 20.0);
                rng_state = 12345u + (uint32_t)t * 7919u;
                for (k = 0; k < pairs; k++) {
                    ni[k] = fi[k] + (float)(sigma * gauss() * 0.70710678);
                    nq[k] = fq[k] + (float)(sigma * gauss() * 0.70710678);
                }
                ui = ni;
                uq = nq;
            }
            a = run(ui, uq, pairs, rate, FM_RDS_FILTER_RECTANGULAR);
            b = run(ui, uq, pairs, rate, FM_RDS_FILTER_SHAPED);
            ab += a.blocks; ag += a.groups; an += a.ps[0] != '-';
            bb += b.blocks; bg += b.groups; bn += b.ps[0] != '-';
        }
        if (noise[n] > -90.0)
            printf("  %+5.0f dB %6.1f %6.1f %2ld/%d %8.1f %6.1f %2ld/%d\n",
                   noise[n], (double)ab / trials, (double)ag / trials, an,
                   trials, (double)bb / trials, (double)bg / trials, bn,
                   trials);
        else
            printf("   as is  %6ld %6ld %-6s %8ld %6ld %-6s\n", ab, ag,
                   an ? "yes" : "-", bb, bg, bn ? "yes" : "-");
    }
    printf("\nS/N is the recording against the noise added to it; the\n"
           "recording's own noise is already in there and unmeasured, so\n"
           "these are relative steps rather than absolute figures.\n");
    return 0;
}
