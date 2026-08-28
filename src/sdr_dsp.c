#include "sdr_dsp.h"

#include <math.h>
#include <stdlib.h>

#define PI_F 3.14159265358979323846f

static void fft_forward(float *re, float *im) {
    const unsigned int n = SDR_DSP_FFT_SIZE;

    for (unsigned int i = 1, j = 0; i < n; i++) {
        unsigned int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = re[i];
            re[i] = re[j];
            re[j] = t;
            t = im[i];
            im[i] = im[j];
            im[j] = t;
        }
    }

    for (unsigned int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * PI_F / (float)len;
        float step_re = cosf(angle);
        float step_im = sinf(angle);
        for (unsigned int base = 0; base < n; base += len) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            for (unsigned int j = 0; j < len / 2; j++) {
                unsigned int even = base + j;
                unsigned int odd = even + len / 2;
                float odd_re = re[odd] * w_re - im[odd] * w_im;
                float odd_im = re[odd] * w_im + im[odd] * w_re;
                float even_re = re[even];
                float even_im = im[even];

                re[even] = even_re + odd_re;
                im[even] = even_im + odd_im;
                re[odd] = even_re - odd_re;
                im[odd] = even_im - odd_im;

                float next_re = w_re * step_re - w_im * step_im;
                w_im = w_re * step_im + w_im * step_re;
                w_re = next_re;
            }
        }
    }
}

void sdr_dsp_init(struct sdr_dsp *dsp) {
    dsp->hann_sum = 0.0f;
    for (int i = 0; i < SDR_DSP_FFT_SIZE; i++) {
        dsp->hann[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i /
                                         (float)(SDR_DSP_FFT_SIZE - 1));
        dsp->hann_sum += dsp->hann[i];
    }
}

size_t sdr_dsp_convert_iq(const uint8_t *bytes, size_t byte_count,
                          float *i_out, float *q_out,
                          float *magnitude_out, size_t pair_capacity) {
    if (!bytes || !i_out || !q_out || !magnitude_out)
        return 0;

    size_t pairs = byte_count / 2;
    if (pairs > pair_capacity)
        pairs = pair_capacity;
    for (size_t n = 0; n < pairs; n++) {
        float i = (float)bytes[2 * n] - 127.5f;
        float q = (float)bytes[2 * n + 1] - 127.5f;
        i_out[n] = i;
        q_out[n] = q;
        magnitude_out[n] = sqrtf(i * i + q * q);
    }
    return pairs;
}

size_t sdr_dsp_peak_bins(const float *magnitudes, size_t pair_count,
                         float *peaks, size_t peak_capacity) {
    if (!magnitudes || !peaks || pair_count == 0 || peak_capacity == 0)
        return 0;

    size_t bin_size = (pair_count + peak_capacity - 1) / peak_capacity;
    size_t bins = (pair_count + bin_size - 1) / bin_size;
    for (size_t bin = 0; bin < bins; bin++) {
        size_t start = bin * bin_size;
        size_t end = start + bin_size;
        if (end > pair_count)
            end = pair_count;
        float peak = magnitudes[start];
        for (size_t n = start + 1; n < end; n++)
            if (magnitudes[n] > peak)
                peak = magnitudes[n];
        peaks[bin] = peak;
    }
    return bins;
}

void sdr_dsp_remove_dc(float *i_samples, float *q_samples,
                       size_t pair_count) {
    if (!i_samples || !q_samples || pair_count == 0)
        return;

    double i_sum = 0.0;
    double q_sum = 0.0;
    for (size_t n = 0; n < pair_count; n++) {
        i_sum += i_samples[n];
        q_sum += q_samples[n];
    }
    float i_mean = (float)(i_sum / (double)pair_count);
    float q_mean = (float)(q_sum / (double)pair_count);
    for (size_t n = 0; n < pair_count; n++) {
        i_samples[n] -= i_mean;
        q_samples[n] -= q_mean;
    }
}

static int compare_float(const void *left, const void *right) {
    float a = *(const float *)left;
    float b = *(const float *)right;
    return (a > b) - (a < b);
}

static float nearest_rank(const float *sorted, size_t count,
                          double percentile) {
    size_t rank = (size_t)ceil(percentile * (double)count);
    if (rank < 1)
        rank = 1;
    if (rank > count)
        rank = count;
    return sorted[rank - 1];
}

int sdr_dsp_signal_stats(const float *i_samples, const float *q_samples,
                         const float *magnitudes, size_t pair_count,
                         float *sort_workspace,
                         struct sdr_signal_stats *stats) {
    if (!i_samples || !q_samples || !magnitudes || !sort_workspace ||
        !stats || pair_count == 0)
        return 0;

    size_t clipped = 0;
    float strongest_component = 0.0f;
    for (size_t n = 0; n < pair_count; n++) {
        sort_workspace[n] = magnitudes[n];
        float absolute_i = fabsf(i_samples[n]);
        float absolute_q = fabsf(q_samples[n]);
        if (absolute_i >= 127.5f || absolute_q >= 127.5f)
            clipped++;
        if (absolute_i > strongest_component)
            strongest_component = absolute_i;
        if (absolute_q > strongest_component)
            strongest_component = absolute_q;
    }
    qsort(sort_workspace, pair_count, sizeof(*sort_workspace), compare_float);
    stats->noise_magnitude = nearest_rank(sort_workspace, pair_count, 0.10);
    stats->signal_magnitude = nearest_rank(sort_workspace, pair_count, 0.995);
    float noise = fmaxf(stats->noise_magnitude, 1e-12f);
    float signal = fmaxf(stats->signal_magnitude, noise);
    stats->snr_db = 20.0f * log10f(signal / noise);
    stats->clipping_percent = 100.0f * (float)clipped / (float)pair_count;
    if (strongest_component <= 0.0f)
        stats->headroom_db = 120.0f;
    else
        stats->headroom_db = fmaxf(0.0f,
                                   20.0f * log10f(127.5f /
                                                  strongest_component));
    return 1;
}

int sdr_dsp_estimate_channel_center(const float *spectrum_dbfs,
                                    size_t bin_count,
                                    double lower_frequency_hz,
                                    double upper_frequency_hz,
                                    double expected_frequency_hz,
                                    double coarse_half_width_hz,
                                    double fine_half_width_hz,
                                    float *sort_workspace,
                                    struct sdr_channel_estimate *estimate) {
    if (!spectrum_dbfs || bin_count < 2 || !sort_workspace || !estimate ||
        upper_frequency_hz <= lower_frequency_hz ||
        coarse_half_width_hz <= 0.0 || fine_half_width_hz <= 0.0 ||
        fine_half_width_hz >= coarse_half_width_hz)
        return 0;

    float *workspace = sort_workspace;
    size_t floor_count = 0;
    size_t search_count = 0;
    float peak = -1e30f;
    size_t peak_bin = 0;
    double bin_width = (upper_frequency_hz - lower_frequency_hz) /
                       (double)bin_count;
    for (size_t n = 0; n < bin_count; n++) {
        double frequency = lower_frequency_hz + bin_width * (double)n;
        double distance = fabs(frequency - expected_frequency_hz);
        if (distance > coarse_half_width_hz &&
            distance <= coarse_half_width_hz * 2.0)
            workspace[floor_count++] = spectrum_dbfs[n];
        if (distance > coarse_half_width_hz)
            continue;
        search_count++;
        if (spectrum_dbfs[n] > peak) {
            peak = spectrum_dbfs[n];
            peak_bin = n;
        }
    }
    if (search_count < 5) {
        return 0;
    }
    if (floor_count < 5) {
        floor_count = 0;
        for (size_t n = 0; n < bin_count; n++) {
            double frequency = lower_frequency_hz + bin_width * (double)n;
            if (fabs(frequency - expected_frequency_hz) <= coarse_half_width_hz)
                workspace[floor_count++] = spectrum_dbfs[n];
        }
    }
    qsort(workspace, floor_count, sizeof(*workspace), compare_float);
    float floor = nearest_rank(workspace, floor_count, 0.20);
    if (!isfinite(floor) || !isfinite(peak) || peak - floor < 8.0f)
        return 0;

    double peak_frequency = lower_frequency_hz + bin_width * peak_bin;
    if (fabs(peak_frequency - expected_frequency_hz) >
        coarse_half_width_hz - fine_half_width_hz)
        return 0;

    double floor_power = pow(10.0, floor / 10.0);
    double weighted_frequency = 0.0;
    double total_weight = 0.0;
    for (size_t n = 0; n < bin_count; n++) {
        double frequency = lower_frequency_hz + bin_width * (double)n;
        if (fabs(frequency - peak_frequency) > fine_half_width_hz)
            continue;
        double weight = pow(10.0, spectrum_dbfs[n] / 10.0) - floor_power;
        if (weight <= 0.0)
            continue;
        weighted_frequency += frequency * weight;
        total_weight += weight;
    }
    if (total_weight <= 0.0)
        return 0;
    estimate->measured_frequency_hz = weighted_frequency / total_weight;
    estimate->peak_frequency_hz = peak_frequency;
    estimate->peak_dbfs = peak;
    estimate->floor_dbfs = floor;
    estimate->prominence_db = peak - floor;
    return 1;
}

int sdr_dsp_corrected_ppm(int current_ppm, double measured_frequency_hz,
                          double expected_frequency_hz) {
    if (expected_frequency_hz <= 0.0)
        return current_ppm;
    double residual = (measured_frequency_hz - expected_frequency_hz) /
                      expected_frequency_hz * 1000000.0;
    return current_ppm - (int)lround(residual);
}

int sdr_dsp_spectrum(struct sdr_dsp *dsp,
                     const float *i_samples, const float *q_samples,
                     size_t pair_count, float *average_dbfs,
                     float *maximum_dbfs) {
    if (!dsp || !i_samples || !q_samples || !average_dbfs || !maximum_dbfs)
        return 0;

    size_t windows = pair_count / SDR_DSP_FFT_SIZE;
    if (windows == 0)
        return 0;

    for (int k = 0; k < SDR_DSP_FFT_SIZE; k++) {
        average_dbfs[k] = 0.0f;
        maximum_dbfs[k] = 0.0f;
    }

    for (size_t window = 0; window < windows; window++) {
        size_t offset = window * SDR_DSP_FFT_SIZE;
        for (int n = 0; n < SDR_DSP_FFT_SIZE; n++) {
            float scale = dsp->hann[n] / 127.5f;
            dsp->fft_re[n] = i_samples[offset + (size_t)n] * scale;
            dsp->fft_im[n] = q_samples[offset + (size_t)n] * scale;
        }
        fft_forward(dsp->fft_re, dsp->fft_im);

        for (int k = 0; k < SDR_DSP_FFT_SIZE; k++) {
            int shifted = (k + SDR_DSP_FFT_SIZE / 2) %
                          SDR_DSP_FFT_SIZE;
            float re = dsp->fft_re[k] / dsp->hann_sum;
            float im = dsp->fft_im[k] / dsp->hann_sum;
            float power = re * re + im * im;
            average_dbfs[shifted] += power;
            if (window == 0 || power > maximum_dbfs[shifted])
                maximum_dbfs[shifted] = power;
        }
    }

    const float floor_power = powf(10.0f, SDR_DSP_DBFS_FLOOR / 10.0f);
    for (int k = 0; k < SDR_DSP_FFT_SIZE; k++) {
        float average_power = average_dbfs[k] / (float)windows;
        if (average_power < floor_power)
            average_power = floor_power;
        if (maximum_dbfs[k] < floor_power)
            maximum_dbfs[k] = floor_power;
        average_dbfs[k] = 10.0f * log10f(average_power);
        maximum_dbfs[k] = 10.0f * log10f(maximum_dbfs[k]);
    }
    return (int)windows;
}

int sdr_dsp_channel_powers(const float *spectrum_dbfs, size_t bin_count,
                           double spectrum_lower_hz,
                           double spectrum_upper_hz,
                           double accept_lower_hz, double accept_upper_hz,
                           double base_hz, double spacing_hz,
                           int index_min, int index_max,
                           float *powers_dbfs) {
    if (!spectrum_dbfs || !powers_dbfs || bin_count < 2 ||
        spectrum_upper_hz <= spectrum_lower_hz || spacing_hz <= 0.0 ||
        index_min < 0 || index_max < index_min)
        return 0;

    double bin_width = (spectrum_upper_hz - spectrum_lower_hz) /
                       (double)bin_count;
    int written = 0;
    for (int index = index_min; index <= index_max; index++) {
        double center = base_hz + (double)index * spacing_hz;
        if (center < accept_lower_hz || center > accept_upper_hz)
            continue;
        double lo = center - spacing_hz / 2.0;
        double hi = center + spacing_hz / 2.0;
        if (lo < spectrum_lower_hz || hi > spectrum_upper_hz)
            continue;
        double sum = 0.0;
        int count = 0;
        for (size_t n = 0; n < bin_count; n++) {
            double frequency = spectrum_lower_hz + bin_width * (double)n;
            if (frequency < lo || frequency >= hi)
                continue;
            sum += pow(10.0, spectrum_dbfs[n] / 10.0);
            count++;
        }
        if (count == 0)
            continue;
        powers_dbfs[index] = (float)(10.0 * log10(sum / (double)count));
        written++;
    }
    return written;
}
