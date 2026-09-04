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

/*
 * The transform's default size, and the largest it will do.
 *
 * SDR_DSP_FFT_SIZE stays 2048 because it is what every caller but the Scope
 * asks for and what their thresholds were chosen against: the survey's floor,
 * the GSM and FM channel scans, the calibration centroid. The Scope may ask
 * for another size; nothing else may, and nothing else has to change to keep
 * getting the one it had.
 *
 * The working buffers are sized to the maximum rather than allocated. Sixteen
 * thousand floats is 64 KB an array, which is not worth a lifetime to get
 * wrong.
 */
#define SDR_DSP_FFT_SIZE 2048
#define SDR_DSP_FFT_MIN 256
#define SDR_DSP_FFT_MAX 16384

/* Whether a size is one this can do: a power of two inside the range. The
   transform is radix-2 and anything else would run and return nonsense. */
static inline int sdr_dsp_fft_size_valid(int size) {
    return size >= SDR_DSP_FFT_MIN && size <= SDR_DSP_FFT_MAX &&
           (size & (size - 1)) == 0;
}
#define SDR_DSP_DBFS_FLOOR (-120.0f)

struct sdr_dsp {
    float hann[SDR_DSP_FFT_MAX];
    /* The size hann[] was built for. The window and its sum both depend on
       it, so a change rebuilds them rather than quietly scaling by the wrong
       total. */
    int hann_size;
    float hann_sum;
    float fft_re[SDR_DSP_FFT_MAX];
    float fft_im[SDR_DSP_FFT_MAX];
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

/*
 * A peak standing above its local noise floor: what a band survey finds before
 * anything is known about what it carries.
 */
/* How far above a measured floor a width threshold is held, so the width of a
   carrier close to its noise is a property of the carrier and not of where the
   noise happened to dip. */
#define SDR_DSP_FLOOR_MARGIN_DB 3.0f

struct sdr_peak {
    int   index;           /* bin of the peak in the array searched */
    float power_dbfs;
    float floor_dbfs;      /* robust local floor either side of it */
    float prominence_db;   /* power - floor */
    int   lower_index;     /* where it falls bandwidth_db below the peak */
    int   upper_index;
};

/*
 * Find local maxima standing at least min_prominence_db above a robust local
 * floor, strongest first, and return how many were written.
 *
 * The floor is a median of the bins either side, not a mean: beside a strong
 * carrier a mean is dragged up far enough to hide a weaker neighbour, which is
 * the case a survey most needs to show. Bins holding `sentinel` were never
 * measured; they bound a hump rather than joining it, so an unswept gap cannot
 * merge two candidates into one.
 *
 * sort_workspace must hold at least `count` floats.
 */
int sdr_dsp_find_peaks(const float *power_dbfs, int count, float sentinel,
                       float min_prominence_db, float bandwidth_db,
                       float *sort_workspace, struct sdr_peak *peaks,
                       int max_peaks);

/*
 * What one carrier looks like in a spectrum: where it actually sits, how far
 * it stands above the floor around it, and how wide it is between the points
 * where it falls bandwidth_db below its peak.
 *
 * A weak carrier may not have bandwidth_db of room above the floor, and
 * measuring its width down there would measure the noise instead. The
 * threshold is held 3 dB clear of the floor in that case, and
 * bandwidth_ref_db reports the drop actually used so the figure can be
 * labelled with the truth rather than with the request.
 *
 * Returns 0 when nothing stands above the floor within the search window.
 */
struct sdr_carrier_report {
    double centre_hz;
    double offset_hz;      /* from the receiver's centre frequency */
    float  peak_dbfs;
    float  floor_dbfs;
    float  prominence_db;
    double bandwidth_hz;
    float  bandwidth_ref_db;  /* dB below the peak the width was taken at */
};

int sdr_dsp_characterise_carrier(const float *spectrum_dbfs, size_t bin_count,
                                 double centre_hz, double sample_rate,
                                 double expected_hz,
                                 double search_half_width_hz,
                                 float bandwidth_db, float *sort_workspace,
                                 struct sdr_carrier_report *report);

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

/*
 * `size` is the transform's length and must satisfy sdr_dsp_fft_size_valid.
 * The output arrays hold that many bins, and the number of windows averaged
 * is pair_count / size -- so a longer transform buys resolution and spends
 * averaging, which is the whole of the trade.
 */
int sdr_dsp_spectrum(struct sdr_dsp *dsp,
                     const float *i_samples, const float *q_samples,
                     size_t pair_count, int size, float *average_dbfs,
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
