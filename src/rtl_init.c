/* Minimal RTLSDR init example, modeled after dump1090's modesInitRTLSDR().
 *
 * Tunes to 1090 MHz, samples at 2 MS/s with max gain, and prints a few
 * blocks of raw I/Q data statistics, then exits.
 *
 * Build: gcc -o rtl_init rtl_init.c $(pkg-config --cflags --libs librtlsdr)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <rtl-sdr.h>

#define FREQ        1090000000  /* ADS-B frequency. */
#define SAMPLE_RATE 2000000     /* 2 MS/s: 1 sample = 0.5 us. */

/* Convert one raw I/Q byte pair to a magnitude on the 127.5-centered scale. */
static float iq_magnitude(unsigned char i, unsigned char q) {
    float fi = (float)i - 127.5f, fq = (float)q - 127.5f;
    return sqrtf(fi*fi + fq*fq);
}

int main(void) {
    rtlsdr_dev_t *dev = NULL;
    int device_count, numgains, gains[100], gain;
    int j;

    /* Find and open the first device. */
    device_count = rtlsdr_get_device_count();
    if (!device_count) {
        fprintf(stderr, "No supported RTLSDR devices found.\n");
        exit(1);
    }
    fprintf(stderr, "Found %d device(s):\n", device_count);
    for (j = 0; j < device_count; j++) {
        char vendor[256], product[256], serial[256];
        rtlsdr_get_device_usb_strings(j, vendor, product, serial);
        fprintf(stderr, "  %d: %s, %s, SN: %s\n", j, vendor, product, serial);
    }

    if (rtlsdr_open(&dev, 0) < 0) {
        fprintf(stderr, "Error opening the RTLSDR device: %s\n",
                strerror(errno));
        exit(1);
    }

    /* Manual gain mode, set to the maximum available gain. */
    rtlsdr_set_tuner_gain_mode(dev, 1);
    numgains = rtlsdr_get_tuner_gains(dev, gains);
    gain = gains[numgains-1];
    rtlsdr_set_tuner_gain(dev, gain);
    fprintf(stderr, "Setting gain to: %.2f dB\n", gain/10.0);

    /* Tune and set sample rate. */
    rtlsdr_set_center_freq(dev, FREQ);
    rtlsdr_set_sample_rate(dev, SAMPLE_RATE);
    rtlsdr_reset_buffer(dev);
    fprintf(stderr, "Tuned to %.3f MHz at %d S/s, gain reported: %.2f dB\n",
            FREQ/1e6, SAMPLE_RATE, rtlsdr_get_tuner_gain(dev)/10.0);

    /* Read one block of raw I/Q samples synchronously. */
    {
        unsigned char buf[16*16384]; /* 256 KB, like dump1090's block. */
        int n_read = 0;

        if (rtlsdr_read_sync(dev, buf, sizeof(buf), &n_read) < 0) {
            fprintf(stderr, "Synchronous read failed.\n");
            rtlsdr_close(dev);
            exit(1);
        }
        fprintf(stderr, "Read %d bytes (%d I/Q pairs).\n", n_read, n_read/2);
        fprintf(stderr, "First samples (I,Q): ");
        for (j = 0; j < 8 && j*2+1 < n_read; j++)
            fprintf(stderr, "(%d,%d) ", buf[j*2], buf[j*2+1]);
        fprintf(stderr, "\n");

        /* Magnitude stats over the whole block. */
        {
            int pairs = n_read/2;
            float min = 1e30f, max = 0.0f, sum = 0.0f;
            for (j = 0; j < pairs; j++) {
                float m = iq_magnitude(buf[j*2], buf[j*2+1]);
                if (m < min) min = m;
                if (m > max) max = m;
                sum += m;
            }
            if (pairs > 0)
                fprintf(stderr, "Magnitude: min %.1f max %.1f mean %.1f\n",
                        min, max, sum/pairs);
        }
    }

    rtlsdr_close(dev);
    return 0;
}
