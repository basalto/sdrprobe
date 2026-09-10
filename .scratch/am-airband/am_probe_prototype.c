/*
 * Prototype: is there AM voice in this channel, or only a carrier?
 *
 * Mix the channel to zero, filter it to its own width, detect the envelope,
 * and then measure the *audio* -- because a carrier with nothing on it and a
 * carrier carrying speech are the same carrier until you look at what rides
 * it. The two-band comparison is the RDS trick: energy where speech lives
 * against energy where it does not, in the same demodulated stream.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define RATE 2000000.0
#define DECIM 40                  /* 2 MS/s -> 50 kHz */
#define AUDIO_RATE (RATE / DECIM)
#define TAPS 161
#define NFFT 2048

static float *si, *sq;
static size_t pairs;

static int load(const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char *raw;
    long n;
    size_t k;
    if (!f) return -1;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    raw = malloc((size_t)n);
    if (fread(raw, 1, (size_t)n, f) != (size_t)n) { fclose(f); return -1; }
    fclose(f);
    pairs = (size_t)n / 2;
    si = malloc(pairs * sizeof(*si));
    sq = malloc(pairs * sizeof(*sq));
    for (k = 0; k < pairs; k++) {
        si[k] = ((float)raw[2 * k] - 127.5f) / 127.5f;
        sq[k] = ((float)raw[2 * k + 1] - 127.5f) / 127.5f;
    }
    free(raw);
    return 0;
}

/* Hamming-windowed sinc, cutoff as a fraction of the sample rate. */
static void make_lowpass(double cutoff, double *h) {
    int n; double sum = 0.0;
    for (n = 0; n < TAPS; n++) {
        double m = n - (TAPS - 1) / 2.0;
        double s = (fabs(m) < 1e-9) ? 2.0 * cutoff
                                    : sin(2.0 * M_PI * cutoff * m) / (M_PI * m);
        double w = 0.54 - 0.46 * cos(2.0 * M_PI * n / (TAPS - 1));
        h[n] = s * w;
        sum += h[n];
    }
    for (n = 0; n < TAPS; n++) h[n] /= sum;
}

/* Mix to zero, filter, decimate, envelope-detect. Returns audio sample count. */
static size_t demodulate(double offset_hz, float *audio, size_t capacity,
                         double *carrier_dc) {
    static double h[TAPS];
    size_t out = 0, k;
    double dc = 0.0;
    make_lowpass(12500.0 / RATE, h);
    for (k = TAPS; k + TAPS < pairs && out < capacity; k += DECIM) {
        double ar = 0.0, ai = 0.0;
        int t;
        for (t = 0; t < TAPS; t++) {
            size_t j = k + (size_t)t - TAPS / 2;
            double ph = -2.0 * M_PI * offset_hz * (double)j / RATE;
            double c = cos(ph), s = sin(ph);
            double xr = si[j] * c - sq[j] * s;
            double xi = si[j] * s + sq[j] * c;
            ar += xr * h[t];
            ai += xi * h[t];
        }
        audio[out] = (float)sqrt(ar * ar + ai * ai);   /* envelope */
        dc += audio[out];
        out++;
    }
    if (out) dc /= (double)out;
    *carrier_dc = dc;
    return out;
}

/* Average power spectrum of the audio, in dB relative to its own total. */
static void audio_spectrum(const float *audio, size_t n, double dc,
                           double *power, int bins) {
    size_t start; int b; int frames = 0;
    for (b = 0; b < bins; b++) power[b] = 0.0;
    for (start = 0; start + NFFT < n; start += NFFT) {
        static double re[NFFT], im[NFFT];
        int i, k;
        for (i = 0; i < NFFT; i++) {
            double w = 0.5 - 0.5 * cos(2.0 * M_PI * i / (NFFT - 1));
            re[i] = (audio[start + (size_t)i] - dc) * w;
            im[i] = 0.0;
        }
        for (k = 0; k < bins; k++) {
            double sr = 0.0, s2 = 0.0;
            for (i = 0; i < NFFT; i++) {
                double ph = -2.0 * M_PI * (double)k * i / NFFT;
                sr += re[i] * cos(ph);
                s2 += re[i] * sin(ph);
            }
            power[k] += sr * sr + s2 * s2;
        }
        frames++;
        if (frames >= 12) break;   /* enough to average */
    }
    if (frames) for (b = 0; b < bins; b++) power[b] /= frames;
}

static double band_energy(const double *power, int bins, double lo, double hi) {
    double sum = 0.0; int k;
    for (k = 0; k < bins; k++) {
        double hz = (double)k * AUDIO_RATE / NFFT;
        if (hz >= lo && hz < hi) sum += power[k];
    }
    return sum;
}

int main(int argc, char **argv) {
    static float audio[400000];
    static double power[400];
    const int bins = 400;          /* up to ~9.8 kHz */
    double offset, dc;
    size_t n;

    if (argc < 3) { fprintf(stderr, "usage: %s <capture> <offset_hz>...\n", argv[0]); return 2; }
    if (load(argv[1]) != 0) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    printf("%s  %.2f s at %.0f S/s, audio at %.0f Hz\n\n",
           argv[1], (double)pairs / RATE, RATE, AUDIO_RATE);
    printf("%-12s %10s %10s %10s %10s %10s\n", "offset", "carrier",
           "depth", "speech dB", "4-8k dB", "speech-hiss");
    if (getenv("AM_SLICES")) {
        /* Per-slice carrier and depth: an air-traffic channel is idle most of
           the time, so one number over the whole look is the average of a
           transmission and a lot of silence. */
        double slice = atof(getenv("AM_SLICES"));
        size_t per;
        offset = atof(argv[2]);
        n = demodulate(offset, audio, sizeof(audio) / sizeof(audio[0]), &dc);
        per = (size_t)(slice * AUDIO_RATE);
        printf("per %.2f s at %+.0f Hz (whole-look carrier %.4f)\n\n",
               slice, offset, dc);
        printf("%-8s %10s %10s %10s\n", "from", "carrier", "depth", "vs quiet");
        {
            double quiet = 1e9;
            size_t st;
            for (st = 0; st + per <= n; st += per) {
                double m = 0.0; size_t i;
                for (i = 0; i < per; i++) m += audio[st + i];
                m /= (double)per;
                if (m < quiet) quiet = m;
            }
            for (st = 0; st + per <= n; st += per) {
                double m = 0.0, ac = 0.0; size_t i;
                for (i = 0; i < per; i++) m += audio[st + i];
                m /= (double)per;
                for (i = 0; i < per; i++) ac += (audio[st + i] - m) * (audio[st + i] - m);
                ac = sqrt(ac / (double)per);
                printf("%-8.1f %10.4f %10.3f %9.1f dB\n",
                       (double)st / AUDIO_RATE, m, m > 0 ? ac / m : 0.0,
                       20.0 * log10(m / quiet));
            }
        }
        return 0;
    }
    for (int a = 2; a < argc; a++) {
        double ac = 0.0, speech, hiss, peak = 0.0; int k, peak_k = 0;
        offset = atof(argv[a]);
        n = demodulate(offset, audio, sizeof(audio) / sizeof(audio[0]), &dc);
        for (size_t i = 0; i < n; i++) ac += (audio[i] - dc) * (audio[i] - dc);
        ac = n ? sqrt(ac / (double)n) : 0.0;
        audio_spectrum(audio, n, dc, power, bins);
        speech = band_energy(power, bins, 300.0, 3400.0);
        hiss = band_energy(power, bins, 4000.0, 8000.0);
        for (k = 1; k < bins; k++) {
            double hz = (double)k * AUDIO_RATE / NFFT;
            if (hz > 200.0 && hz < 4000.0 && power[k] > peak) { peak = power[k]; peak_k = k; }
        }
        printf("%+-12.0f %10.4f %10.3f %10.1f %10.1f %10.1f   peak %.0f Hz\n",
               offset, dc, dc > 0 ? ac / dc : 0.0,
               10.0 * log10(speech + 1e-30), 10.0 * log10(hiss + 1e-30),
               10.0 * log10((speech + 1e-30) / (hiss + 1e-30)),
               (double)peak_k * AUDIO_RATE / NFFT);
    }
    return 0;
}
