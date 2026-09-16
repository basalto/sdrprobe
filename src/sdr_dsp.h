#ifndef SDR_DSP_H
#define SDR_DSP_H

#include <stddef.h>
#include <stdint.h>

#include "device_profile.h"

/*
 * Generic, technology-independent SDR DSP primitives.
 *
 * Nothing in this file knows about any particular radio technology: it works on
 * raw interleaved I/Q in whatever container `struct device_profile` describes,
 * centred float I/Q, magnitudes, and dBFS spectra. Per-technology DSP modules
 * (see gsm_dsp.h) build on these primitives.
 *
 * Full scale is the profile's and appears nowhere here as a constant: it is
 * what dBFS is relative to, what clipping is measured against, and what
 * headroom counts down from, and those are three readings of one number that
 * belongs to the device.
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

/*
 * The sizes offered to a reader, smallest first.
 *
 * Every power of two between the minimum and the maximum. Not a subset: the
 * trade between resolution and averaging is smooth and there is no size in
 * the range that is a bad idea, only sizes that suit different questions.
 */
#define SDR_DSP_FFT_CHOICES 7

static inline int sdr_dsp_fft_choice(int index) {
    int size = SDR_DSP_FFT_MIN << index;

    if (index < 0 || index >= SDR_DSP_FFT_CHOICES)
        return 0;
    return size;
}

/* Which choice a size is, or -1. */
static inline int sdr_dsp_fft_choice_of(int size) {
    int i;

    for (i = 0; i < SDR_DSP_FFT_CHOICES; i++)
        if (sdr_dsp_fft_choice(i) == size)
            return i;
    return -1;
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

/*
 * The one byte-to-float seam. The profile supplies the container's layout and
 * the ADC's full scale; the floats come out in the device's own counts,
 * centred on zero and deliberately not normalised. Returns pairs written,
 * which is `byte_count / profile->bytes_per_pair` capped by `pair_capacity`
 * -- not `byte_count / 2`, which is a two-byte format's answer to a different
 * question.
 */
size_t sdr_dsp_convert_iq(const struct device_profile *profile,
                          const uint8_t *bytes, size_t byte_count,
                          float *i_out, float *q_out,
                          float *magnitude_out, size_t pair_capacity);

size_t sdr_dsp_peak_bins(const float *magnitudes, size_t pair_count,
                         float *peaks, size_t peak_capacity);

void sdr_dsp_remove_dc(float *i_samples, float *q_samples,
                       size_t pair_count);

/* `full_scale` is the ADC's rail in the same counts the samples are in --
   what clipping is measured against and what headroom counts down from. */
int sdr_dsp_signal_stats(const float *i_samples, const float *q_samples,
                         const float *magnitudes, size_t pair_count,
                         float *sort_workspace, float full_scale,
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
    /* power - floor, and always above zero: see the note on the floor being
       measured on a neighbour in sdr_dsp_find_peaks(). */
    float prominence_db;
    int   lower_index;     /* where it falls bandwidth_db below the peak */
    int   upper_index;
};

/*
 * The bars a candidate has to clear.
 *
 * Two of them, because "how far this stands out" is two different
 * measurements and they do not agree. `topographic_db` is the descent needed
 * before higher ground can be reached, which is what rejects the shoulder of a
 * strong carrier; `floor_db` is height above the median level around it, which
 * is what rejects a bump in the noise. A shoulder clears the second and not
 * the first; a noise excursion clears the first and not the second. Both are
 * needed and neither is the other.
 *
 * They were one number for a long time, checked against the first and reported
 * as the second, with the filtering that should have been the second done by
 * accident instead -- ADR-0013 is the whole story, and this struct is what
 * closes it. Two floats side by side in an argument list would be swappable
 * without a compiler complaint, and swapping them silently turns the gate into
 * something else that still runs.
 */
struct sdr_peak_gate {
    float topographic_db;  /* the descent to reach anything higher */
    float floor_db;        /* height above the median level around it */
    float bandwidth_db;    /* how far down its occupied width is taken */
};

/*
 * Find local maxima clearing both bars, strongest first, and return how many
 * were written.
 *
 * The floor is a median of the bins either side, not a mean: beside a strong
 * carrier a mean is dragged up far enough to hide a weaker neighbour, which is
 * the case a survey most needs to show. Bins holding `sentinel` were never
 * measured; they bound a hump rather than joining it, so an unswept gap cannot
 * merge two candidates into one.
 *
 * Both walks -- the occupied width and the floor window around it -- are
 * bounded to the same span the topographic test judges over. Unbounded, a
 * candidate with no -bandwidth_db point ran to the ends of the array and was
 * then discarded for having no floor left to measure, which did the filtering
 * an explicit threshold should do and made the effective bar depend on how
 * ragged the noise happened to be.
 *
 * sort_workspace must hold at least `count` floats.
 */
int sdr_dsp_find_peaks(const float *power_dbfs, int count, float sentinel,
                       const struct sdr_peak_gate *gate,
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
/*
 * What a retune does to a stored spectrum row, and what is left of it.
 *
 * A waterfall row is `bins` dBFS values spread evenly across the received
 * span, so bin i sits at `centre - rate/2 + i*rate/bins`. Move the centre and
 * every one of those frequencies is still where it was -- what changes is
 * which bin of the *new* span holds it:
 *
 *     j = i + (old_centre - new_centre) * bins / rate
 *
 * Tune upwards and the old picture slides left, which is the direction a
 * reader expects: a carrier that sat at the right of the screen is nearer the
 * middle once you have tuned towards it.
 *
 * The history used to be thrown away whole on any retune, on the sound
 * reasoning that rows gathered at another frequency do not belong at this
 * one. They do, though -- just not in the same bins -- and once the SRD view
 * grew arrows that walk a ten-megahertz allocation half a span at a time,
 * discarding the half that still overlaps meant the waterfall was blank more
 * often than not, with the detection labels left standing over nothing.
 *
 * Returns the shift in bins. A shift of `bins` or more in either direction
 * means the two spans do not overlap and nothing survives; the caller is
 * expected to notice that rather than be protected from it, because clearing
 * is then the right answer and this function is not the one to decide it.
 */
static inline int sdr_dsp_retune_bin_shift(double old_centre_hz,
                                           double new_centre_hz,
                                           double sample_rate, int bins) {
    double shift;

    if (!(sample_rate > 0.0) || bins <= 0)
        return 0;
    shift = (old_centre_hz - new_centre_hz) * (double)bins / sample_rate;
    if (shift > (double)bins)
        return bins;
    if (shift < -(double)bins)
        return -bins;
    return (int)(shift < 0.0 ? shift - 0.5 : shift + 0.5);
}

/*
 * Slide one row by `shift` bins, filling what slides in with `fill`.
 *
 * `fill` is what "not measured here" looks like: a dBFS far under anything a
 * receiver reports, so it renders as the waterfall's own background rather
 * than as a quiet signal. A row shifted clear of itself is entirely fill.
 */
static inline void sdr_dsp_shift_row(float *row, int bins, int shift,
                                     float fill) {
    int i;

    if (!row || bins <= 0)
        return;
    if (shift >= bins || shift <= -bins) {
        for (i = 0; i < bins; i++)
            row[i] = fill;
        return;
    }
    if (shift > 0) {
        for (i = bins - 1; i >= shift; i--)
            row[i] = row[i - shift];
        for (i = 0; i < shift; i++)
            row[i] = fill;
    } else if (shift < 0) {
        for (i = 0; i < bins + shift; i++)
            row[i] = row[i - shift];
        for (i = bins + shift; i < bins; i++)
            row[i] = fill;
    }
}

/*
 * Whether a gain change should clear the waterfall's history rather than
 * leave it standing.
 *
 * `sdr_dsp_retune_bin_shift()` already covers every retune that moves the
 * received frequency, including a PPM correction change: at typical
 * settings that moves the true tuning by well under one bin (100 Hz at
 * 100 MHz for one ppm), and the shift above rounds that to zero on its own
 * -- there is no second implementation to write for it, and none is added
 * here. Toggling DC removal touches one bin. Neither is a parameter below,
 * deliberately: this function answers one question and the caller decides
 * the others by not clearing.
 *
 * A gain change is different in kind rather than degree. It moves nothing
 * in frequency, so the shift above has nothing to say about it -- but every
 * row already on screen was measured at the old gain, and the waterfall's
 * colour scale means a different level afterwards. A history whose top half
 * reads high or low for a reason that is not the signal is worse than a
 * short one, so this clears.
 *
 * `manual` is compared as well as `tenths`: switching to automatic gain
 * with the same last-known tenths on record is still a change, because
 * automatic is not pinned to that number going forward.
 */
static inline int sdr_dsp_gain_change_clears_waterfall(int old_manual,
                                                       int old_gain_tenths,
                                                       int new_manual,
                                                       int new_gain_tenths) {
    return old_manual != new_manual || old_gain_tenths != new_gain_tenths;
}

/* A dBFS no receiver reports, standing for "this bin was never measured". */
#define SDR_DSP_UNMEASURED_DBFS (-300.0f)

int sdr_dsp_spectrum(struct sdr_dsp *dsp,
                     const float *i_samples, const float *q_samples,
                     size_t pair_count, int size, float full_scale,
                     float *average_dbfs, float *maximum_dbfs);

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
