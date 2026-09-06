#include "sdr_dsp.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

#define PI_F 3.14159265358979323846f

static void fft_forward(float *re, float *im, unsigned int n) {

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

/* The window and its sum, for one size. Rebuilt when the size changes, which
   is rare -- and never scaled by the sum of a window it is not. */
static void sdr_dsp_build_hann(struct sdr_dsp *dsp, int size) {
    int i;

    dsp->hann_sum = 0.0f;
    for (i = 0; i < size; i++) {
        dsp->hann[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * (float)i /
                                         (float)(size - 1));
        dsp->hann_sum += dsp->hann[i];
    }
    dsp->hann_size = size;
}

void sdr_dsp_init(struct sdr_dsp *dsp) {
    sdr_dsp_build_hann(dsp, SDR_DSP_FFT_SIZE);
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

/* The index nearest_rank() reads for a percentile: one before the ceiling of
   p*count, clamped into the array. Shared so that selecting a rank without
   sorting picks exactly the element sorting would have picked. */
static size_t rank_index(size_t count, double percentile) {
    size_t rank = (size_t)ceil(percentile * (double)count);

    if (rank < 1)
        rank = 1;
    if (rank > count)
        rank = count;
    return rank - 1;
}

/*
 * Put the k-th smallest element of values[low..high] at index k, with
 * everything before it no greater and everything after no smaller -- the
 * arrangement sorting would give at that index, for the cost of a pass rather
 * than a sort.
 *
 * Three-way partitioning is not an optimisation here but a requirement: these
 * are magnitudes of 8-bit samples, so the same values recur in their
 * thousands, and a two-way split on equal keys is quadratic on exactly that
 * input. Median-of-three keeps sorted and reversed blocks off the worst case
 * as well.
 */
static void select_nth(float *values, size_t low_index, size_t high_index,
                       size_t k) {
    ptrdiff_t low = (ptrdiff_t)low_index;
    ptrdiff_t high = (ptrdiff_t)high_index;
    ptrdiff_t target = (ptrdiff_t)k;

    while (low < high) {
        ptrdiff_t mid = low + (high - low) / 2;
        float a = values[low];
        float b = values[mid];
        float c = values[high];
        float pivot;
        ptrdiff_t less = low;
        ptrdiff_t i = low;
        ptrdiff_t greater = high;

        if (a < b)
            pivot = b < c ? b : (a < c ? c : a);
        else
            pivot = a < c ? a : (b < c ? c : b);

        while (i <= greater) {
            if (values[i] < pivot) {
                float swap = values[less];
                values[less++] = values[i];
                values[i++] = swap;
            } else if (values[i] > pivot) {
                float swap = values[greater];
                values[greater--] = values[i];
                values[i] = swap;
            } else {
                i++;
            }
        }
        if (target < less)
            high = less - 1;
        else if (target > greater)
            low = greater + 1;
        else
            return;   /* target landed inside the run equal to the pivot */
    }
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
    /* Two percentiles out of 131072 magnitudes. Sorting the block to read two
       of its elements cost about 9 ms of the 65.5 ms a block covers -- the
       largest single cost in the frame -- and sorting is not what is being
       asked for. Selecting each rank in place is linear, and picks exactly the
       element the sort would have put there.

       The second selection searches only above the first: selecting a rank
       leaves the array partitioned around it, so the higher percentile cannot
       have moved below. */
    size_t noise_index = rank_index(pair_count, 0.10);
    size_t signal_index = rank_index(pair_count, 0.995);

    select_nth(sort_workspace, 0, pair_count - 1, noise_index);
    stats->noise_magnitude = sort_workspace[noise_index];
    if (signal_index > noise_index)
        select_nth(sort_workspace, noise_index + 1, pair_count - 1,
                   signal_index);
    stats->signal_magnitude = sort_workspace[signal_index];
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

/*
 * The median level of the bins within `window` either side of `centre`,
 * skipping the hump itself (lower..upper) and any bin never measured.
 *
 * A median rather than a mean because the neighbour of a strong carrier is
 * exactly where a mean floor goes wrong: it rises toward the carrier and
 * buries the weaker signal beside it, which is the one a survey exists to
 * show. Returns NAN when there is nothing to measure a floor from.
 *
 * That NaN is load-bearing, which ADR-0013 discovered and ADR-0017 confirmed
 * the hard way. A candidate whose hump fills the window has no floor to be
 * measured against, and dropping it is right: it is a feature inside something
 * larger, not a signal of its own. Measuring outward from the hump's edges
 * instead -- which is what the name suggests and what a first attempt at
 * ADR-0017 did -- always finds *a* floor, and a 470-690 MHz sweep then reports
 * every ripple on every television multiplex as a carrier of its own: 38
 * carriers became 58, and all twenty additions were inside a DVB-T channel.
 */
static float local_floor(const float *power_dbfs, int count, float sentinel,
                         int centre, int lower, int upper, int window,
                         float *workspace) {
    int gathered = 0;
    int from = centre - window;
    int to = centre + window;

    if (from < 0)
        from = 0;
    if (to > count - 1)
        to = count - 1;
    for (int i = from; i <= to; i++) {
        if (i >= lower && i <= upper)
            continue;
        if (power_dbfs[i] <= sentinel)
            continue;
        workspace[gathered++] = power_dbfs[i];
    }
    if (gathered == 0)
        return NAN;
    qsort(workspace, (size_t)gathered, sizeof(*workspace), compare_float);
    return workspace[gathered / 2];
}

/*
 * Topographic prominence: how far this peak stands above the lowest point that
 * must be crossed to reach anything higher. It is the right measure here and a
 * "how far above the local floor" rule is not, because the shoulder of a
 * strong carrier sits far above the floor while being no signal at all -- the
 * first cut of this function reported three signals where there were two, and
 * four where a gap split two humps. Crossing to a higher peak costs a shoulder
 * almost nothing, so its prominence collapses and it drops out, while a weak
 * carrier beside a strong one keeps the full drop to the floor between them.
 *
 * The walk is bounded: prominence is judged within `window` bins either side,
 * which keeps a smooth ramp from costing a full pass per bin, and matches what
 * a reader means by "stands out around here" anyway. Unmeasured bins end the
 * walk like the array's edge does.
 */
static float topographic_prominence(const float *power_dbfs, int count,
                                    float sentinel, int at, int window) {
    float level = power_dbfs[at];
    float left_saddle = level;
    float right_saddle = level;
    int steps;

    steps = 0;
    for (int i = at - 1; i >= 0 && steps < window; i--, steps++) {
        if (power_dbfs[i] <= sentinel || power_dbfs[i] > level)
            break;
        if (power_dbfs[i] < left_saddle)
            left_saddle = power_dbfs[i];
    }
    steps = 0;
    for (int i = at + 1; i < count && steps < window; i++, steps++) {
        if (power_dbfs[i] <= sentinel || power_dbfs[i] > level)
            break;
        if (power_dbfs[i] < right_saddle)
            right_saddle = power_dbfs[i];
    }
    return level - (left_saddle > right_saddle ? left_saddle : right_saddle);
}

static int compare_peaks(const void *left, const void *right) {
    const struct sdr_peak *a = left;
    const struct sdr_peak *b = right;
    return (a->power_dbfs < b->power_dbfs) - (a->power_dbfs > b->power_dbfs);
}

/* Walk out from `centre` while the trace stays at or above `threshold`, no
   further than `reach` either side. */
static void widen(const float *power_dbfs, int count, float sentinel,
                 int centre, float threshold, int reach, int *lower,
                 int *upper) {
    int limit_low = centre - reach < 0 ? 0 : centre - reach;
    int limit_high = centre + reach > count - 1 ? count - 1 : centre + reach;

    *lower = centre;
    while (*lower > limit_low && power_dbfs[*lower - 1] > sentinel &&
           power_dbfs[*lower - 1] >= threshold)
        (*lower)--;
    *upper = centre;
    while (*upper < limit_high && power_dbfs[*upper + 1] > sentinel &&
           power_dbfs[*upper + 1] >= threshold)
        (*upper)++;
}

int sdr_dsp_find_peaks(const float *power_dbfs, int count, float sentinel,
                       const struct sdr_peak_gate *gate,
                       float *sort_workspace, struct sdr_peak *peaks,
                       int max_peaks) {
    int found = 0;
    int window = count / 4;

    if (!power_dbfs || !sort_workspace || !peaks || !gate || count <= 2 ||
        max_peaks <= 0)
        return 0;
    if (window > 1024)
        window = 1024;
    if (window < 16)
        window = 16;

    for (int i = 1; i < count - 1; i++) {
        if (power_dbfs[i] <= sentinel)
            continue;
        /* A local maximum, taking a flat top only once. */
        if (!(power_dbfs[i] >= power_dbfs[i - 1] &&
              power_dbfs[i] > power_dbfs[i + 1]))
            continue;
        if (topographic_prominence(power_dbfs, count, sentinel, i, window) <
            gate->topographic_db)
            continue;

        struct sdr_peak candidate;
        candidate.index = i;
        candidate.power_dbfs = power_dbfs[i];
        /*
         * Out to the -bandwidth_db points, stopping where the sweep did.
         *
         * Deliberately unbounded, and the consequence is deliberate too: a
         * candidate standing less than bandwidth_db above its own noise has no
         * -bandwidth_db point, so this runs to the ends of the array, the
         * floor window below is left nothing to measure, and the candidate is
         * dropped. The real bar is therefore nearer 20 dB than the 8 dB asked
         * for, and it moves with how ragged the noise is.
         *
         * ADR-0017 tried to replace that with an explicit threshold, measured
         * the result, and put it back. What the rule is really saying is that
         * a candidate with no end to it is a feature inside something larger:
         * bound the walk and measure the floor outside the hump instead, and a
         * 470-690 MHz sweep reports every ripple on every television multiplex
         * as a carrier of its own -- 38 carriers became 58, all twenty
         * additions inside a DVB-T channel. What it costs is weak isolated
         * carriers, which is real: ARFCN 63 in testfiles/gsm_arfcn_69.bin is
         * named by the cell's own neighbour list and is not reported here.
         * `.scratch/survey-extent/` is the way out, and it is a trough walk
         * rather than a threshold.
         */
        float threshold = power_dbfs[i] - gate->bandwidth_db;
        /*
         * Bounded, and the bound's only job is to leave something outside the
         * hump to measure a floor against. It is not a statement about how
         * wide a signal may be -- a quarter of the array keeps three quarters
         * available, and that is the whole of the reasoning.
         */
        int reach = count / 4 < 4 ? 4 : count / 4;
        candidate.lower_index = i;
        while (candidate.lower_index > 0 &&
               i - candidate.lower_index < reach &&
               power_dbfs[candidate.lower_index - 1] > sentinel &&
               power_dbfs[candidate.lower_index - 1] >= threshold)
            candidate.lower_index--;
        candidate.upper_index = i;
        while (candidate.upper_index < count - 1 &&
               candidate.upper_index - i < reach &&
               power_dbfs[candidate.upper_index + 1] > sentinel &&
               power_dbfs[candidate.upper_index + 1] >= threshold)
            candidate.upper_index++;

        int width = candidate.upper_index - candidate.lower_index + 1;
        int floor_window = width * 8 < 16 ? 16 : width * 8;
        candidate.floor_dbfs = local_floor(power_dbfs, count, sentinel, i,
                                           candidate.lower_index,
                                           candidate.upper_index, floor_window,
                                           sort_workspace);
        if (!isfinite(candidate.floor_dbfs))
            continue;
        /*
         * A maximum below its own floor is not a candidate.
         *
         * local_floor() leaves out the peak's own hump and nothing else, so
         * inside a busy stretch it measures the *neighbours*. A peak sitting
         * in a notch between two carriers passes the topographic gate --
         * there is a real descent either side of it -- and then has its floor
         * taken from the carriers, which are above it. A survey of 24-1766 MHz
         * reported one such entry at 1603.219 MHz standing -3.0 dB above its
         * floor, and a second sweep two days later reported the same bin at
         * -3.4 dB: reproducible, and an arithmetic impossibility for the
         * quantity the column claims, which is how far the peak stands above
         * the noise either side of it.
         *
         * Dropping it rather than clamping the number to zero, because there
         * is nothing here a reader wanted: the carriers whose level became
         * this "floor" are themselves in the list, a bin or two away. This is
         * not a level threshold -- it says nothing about how strong a peak has
         * to be, only that it has to be above its own surroundings -- and it
         * removed 1 candidate of 289 in the survey above.
         *
         * The wider fault is the one ADR-0013 names: the gate and the reported
         * figure are different measures of prominence, and this is where they
         * contradict each other outright rather than merely disagreeing.
         */
        if (candidate.floor_dbfs >= candidate.power_dbfs)
            continue;
        candidate.prominence_db = candidate.power_dbfs - candidate.floor_dbfs;
        /* And the second bar, which is the one the caller can reason about:
           how far this stands above the level around it. */
        if (candidate.prominence_db < gate->floor_db)
            continue;

        if (found < max_peaks) {
            peaks[found++] = candidate;
        } else {
            /* Full: keep the strongest, not the first to arrive. */
            int weakest = 0;
            for (int k = 1; k < found; k++)
                if (peaks[k].power_dbfs < peaks[weakest].power_dbfs)
                    weakest = k;
            if (candidate.power_dbfs > peaks[weakest].power_dbfs)
                peaks[weakest] = candidate;
        }
    }
    qsort(peaks, (size_t)found, sizeof(*peaks), compare_peaks);
    return found;
}

int sdr_dsp_characterise_carrier(const float *spectrum_dbfs, size_t bin_count,
                                 double centre_hz, double sample_rate,
                                 double expected_hz,
                                 double search_half_width_hz,
                                 float bandwidth_db, float *sort_workspace,
                                 struct sdr_carrier_report *report) {
    if (!spectrum_dbfs || !sort_workspace || !report || bin_count < 4 ||
        sample_rate <= 0.0)
        return 0;

    double bin_hz = sample_rate / (double)bin_count;
    double lower_hz = centre_hz - sample_rate / 2.0;
    double from_hz = expected_hz - search_half_width_hz;
    double to_hz = expected_hz + search_half_width_hz;
    int from = (int)((from_hz - lower_hz) / bin_hz);
    int to = (int)((to_hz - lower_hz) / bin_hz);
    int peak;

    if (from < 0)
        from = 0;
    if (to > (int)bin_count - 1)
        to = (int)bin_count - 1;
    if (from >= to)
        return 0;

    peak = from;
    for (int i = from; i <= to; i++)
        if (spectrum_dbfs[i] > spectrum_dbfs[peak])
            peak = i;

    /* A first pass at the width, to know which bins to keep out of the floor;
       the width is then taken again against a threshold that cannot fall into
       the floor it just measured.


       Bounded, for the reason find_peaks bounds its own: a carrier standing
       less than bandwidth_db above its noise has no -bandwidth_db point, and
       an unbounded walk leaves the floor window below nothing to measure --
       which came back to the operator as "nothing measurable at that
       frequency now" on a carrier that was plainly there. */
    int reach = (int)bin_count / 16;
    float threshold = spectrum_dbfs[peak] - bandwidth_db;
    int lower;
    int upper;

    if (reach < 8)
        reach = 8;
    widen(spectrum_dbfs, (int)bin_count, SDR_DSP_DBFS_FLOOR - 1.0f, peak,
          threshold, reach, &lower, &upper);

    int width = upper - lower + 1;
    int window = width * 8 < 32 ? 32 : width * 8;
    float floor_dbfs = local_floor(spectrum_dbfs, (int)bin_count,
                                   SDR_DSP_DBFS_FLOOR - 1.0f, peak, lower,
                                   upper, window, sort_workspace);
    /* No floor to measure against means the "carrier" is the whole window --
       a flat spectrum, in other words, in which there is nothing to report. */
    if (!isfinite(floor_dbfs))
        return 0;
    report->floor_dbfs = floor_dbfs;
    report->peak_dbfs = spectrum_dbfs[peak];
    report->prominence_db = report->peak_dbfs - report->floor_dbfs;
    if (report->prominence_db <= 0.0f)
        return 0;

    /* Re-take the width, holding the threshold clear of the floor. */
    float guarded = floor_dbfs + SDR_DSP_FLOOR_MARGIN_DB;
    if (threshold < guarded)
        threshold = guarded;
    report->bandwidth_ref_db = report->peak_dbfs - threshold;
    widen(spectrum_dbfs, (int)bin_count, SDR_DSP_DBFS_FLOOR - 1.0f, peak,
          threshold, reach, &lower, &upper);
    width = upper - lower + 1;

    /* Power-weighted centre across the occupied bins, so a carrier between two
       bins is not reported at whichever of them happened to win. */
    double weight_sum = 0.0;
    double weighted_hz = 0.0;
    for (int i = lower; i <= upper; i++) {
        double weight = pow(10.0, (spectrum_dbfs[i] - report->floor_dbfs) / 10.0);
        if (weight <= 1.0)
            continue;
        weight -= 1.0; /* the floor itself carries no information */
        weighted_hz += weight * (lower_hz + ((double)i + 0.5) * bin_hz);
        weight_sum += weight;
    }
    report->centre_hz = weight_sum > 0.0
                            ? weighted_hz / weight_sum
                            : lower_hz + ((double)peak + 0.5) * bin_hz;
    report->offset_hz = report->centre_hz - centre_hz;
    report->bandwidth_hz = (double)width * bin_hz;
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
                     size_t pair_count, int size, float *average_dbfs,
                     float *maximum_dbfs) {
    if (!dsp || !i_samples || !q_samples || !average_dbfs || !maximum_dbfs)
        return 0;
    if (!sdr_dsp_fft_size_valid(size))
        return 0;
    if (dsp->hann_size != size)
        sdr_dsp_build_hann(dsp, size);

    size_t windows = pair_count / (size_t)size;
    if (windows == 0)
        return 0;

    for (int k = 0; k < size; k++) {
        average_dbfs[k] = 0.0f;
        maximum_dbfs[k] = 0.0f;
    }

    for (size_t window = 0; window < windows; window++) {
        size_t offset = window * (size_t)size;
        for (int n = 0; n < size; n++) {
            float scale = dsp->hann[n] / 127.5f;
            dsp->fft_re[n] = i_samples[offset + (size_t)n] * scale;
            dsp->fft_im[n] = q_samples[offset + (size_t)n] * scale;
        }
        fft_forward(dsp->fft_re, dsp->fft_im, (unsigned int)size);

        for (int k = 0; k < size; k++) {
            int shifted = (k + size / 2) % size;
            float re = dsp->fft_re[k] / dsp->hann_sum;
            float im = dsp->fft_im[k] / dsp->hann_sum;
            float power = re * re + im * im;
            average_dbfs[shifted] += power;
            if (window == 0 || power > maximum_dbfs[shifted])
                maximum_dbfs[shifted] = power;
        }
    }

    const float floor_power = powf(10.0f, SDR_DSP_DBFS_FLOOR / 10.0f);
    for (int k = 0; k < size; k++) {
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
