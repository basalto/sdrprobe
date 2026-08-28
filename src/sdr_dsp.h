#ifndef SDR_DSP_H
#define SDR_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * Generic, technology-independent SDR DSP primitives.
 *
 * Nothing in this file knows about any particular radio technology: it works on
 * raw interleaved 8-bit I/Q, centred float I/Q, magnitudes, and dBFS spectra.
 * Per-technology plugins (see gsm_dsp.h) build on top of these primitives.
 */

#define SDR_DSP_FFT_SIZE 2048
#define SDR_DSP_DBFS_FLOOR (-120.0f)

struct sdr_dsp {
    float hann[SDR_DSP_FFT_SIZE];
    float hann_sum;
    float fft_re[SDR_DSP_FFT_SIZE];
    float fft_im[SDR_DSP_FFT_SIZE];
};

struct sdr_signal_stats {
    float noise_magnitude;
    float signal_magnitude;
    float snr_db;
    float clipping_percent;
    float headroom_db;
};

struct sdr_channel_estimate {
    double measured_frequency_hz;
    double peak_frequency_hz;
    float peak_dbfs;
    float floor_dbfs;
    float prominence_db;
};

void sdr_dsp_init(struct sdr_dsp *dsp);

size_t sdr_dsp_convert_iq(const uint8_t *bytes, size_t byte_count,
                          float *i_out, float *q_out,
                          float *magnitude_out, size_t pair_capacity);

size_t sdr_dsp_peak_bins(const float *magnitudes, size_t pair_count,
                         float *peaks, size_t peak_capacity);

void sdr_dsp_remove_dc(float *i_samples, float *q_samples,
                       size_t pair_count);

int sdr_dsp_signal_stats(const float *i_samples, const float *q_samples,
                         const float *magnitudes, size_t pair_count,
                         float *sort_workspace,
                         struct sdr_signal_stats *stats);

int sdr_dsp_estimate_channel_center(const float *spectrum_dbfs,
                                    size_t bin_count,
                                    double lower_frequency_hz,
                                    double upper_frequency_hz,
                                    double expected_frequency_hz,
                                    double coarse_half_width_hz,
                                    double fine_half_width_hz,
                                    float *sort_workspace,
                                    struct sdr_channel_estimate *estimate);

int sdr_dsp_corrected_ppm(int current_ppm, double measured_frequency_hz,
                          double expected_frequency_hz);

int sdr_dsp_spectrum(struct sdr_dsp *dsp,
                     const float *i_samples, const float *q_samples,
                     size_t pair_count, float *average_dbfs,
                     float *maximum_dbfs);

/*
 * Average power (dBFS) of each channel on an evenly spaced channel grid.
 * A channel with grid index i is centred at base_hz + i * spacing_hz. Channels
 * whose centre lies in [accept_lower_hz, accept_upper_hz] and whose full width
 * fits inside the spectrum span are written into powers_dbfs[i]; others are
 * left untouched. The grid is generic: a cellular caller passes channel numbers
 * (e.g. GSM ARFCNs) as indices.
 */
int sdr_dsp_channel_powers(const float *spectrum_dbfs, size_t bin_count,
                           double spectrum_lower_hz,
                           double spectrum_upper_hz,
                           double accept_lower_hz, double accept_upper_hz,
                           double base_hz, double spacing_hz,
                           int index_min, int index_max,
                           float *powers_dbfs);

#endif
