#include "fm_dsp.h"

#include <math.h>
#include <string.h>

/*
 * FM broadcast front end. See fm_dsp.h for why every rate here is an integer
 * multiple of the pilot, which is what removes the blind loops this would
 * otherwise need.
 */

size_t fm_discriminate_f(const float *i, const float *q, size_t pairs,
                         float *out, size_t capacity) {
    size_t n = 0;
    double pi_ = 0.0, pq = 0.0;

    if (!i || !q || !out || pairs < 2)
        return 0;
    pi_ = i[0];
    pq = q[0];
    for (size_t s = 1; s < pairs && n < capacity; s++) {
        /*
         * x[s] * conj(x[s-1]): the angle of the product is the angle turned
         * between them, which for an FM carrier is the instantaneous
         * frequency and so is the multiplex. Doing it as one atan2 of a
         * product rather than a difference of two atan2s is not an
         * optimisation -- it is what makes the wrap at +-pi come out right
         * without an unwrapping step to get wrong.
         */
        double ci = i[s], cq = q[s];
        double real = ci * pi_ + cq * pq;
        double imag = cq * pi_ - ci * pq;
        out[n++] = (float)atan2(imag, real);
        pi_ = ci;
        pq = cq;
    }
    return n;
}

size_t fm_discriminate(const uint8_t *iq, size_t pairs, float *out,
                       size_t capacity) {
    size_t n = 0;
    double pi_ = 0.0, pq = 0.0;

    if (!iq || !out || pairs < 2)
        return 0;
    pi_ = (double)iq[0] - 127.5;
    pq = (double)iq[1] - 127.5;
    for (size_t s = 1; s < pairs && n < capacity; s++) {
        double ci = (double)iq[2 * s] - 127.5;
        double cq = (double)iq[2 * s + 1] - 127.5;
        double real = ci * pi_ + cq * pq;
        double imag = cq * pi_ - ci * pq;
        out[n++] = (float)atan2(imag, real);
        pi_ = ci;
        pq = cq;
    }
    return n;
}

void fm_pilot_init(struct fm_pilot *pilot, double sample_rate) {
    double bw, denom;

    if (!pilot)
        return;
    memset(pilot, 0, sizeof(*pilot));
    pilot->sample_rate = sample_rate > 0.0 ? sample_rate : 1.0;
    pilot->nominal = 2.0 * M_PI * FM_PILOT_HZ / pilot->sample_rate;
    pilot->frequency = pilot->nominal;
    pilot->frequency_average = pilot->nominal;
    pilot->phase = 0.0;

    /*
     * A second-order loop, gains from the bandwidth and damping rather than
     * chosen by feel: the same standard form the calibration tracker uses,
     * and the reason the loop's behaviour can be reasoned about from
     * FM_PILOT_LOOP_BW_HZ alone.
     */
    bw = 2.0 * M_PI * FM_PILOT_LOOP_BW_HZ / pilot->sample_rate;
    denom = 1.0 + 2.0 * FM_PILOT_DAMPING * bw + bw * bw;
    pilot->alpha = 4.0 * FM_PILOT_DAMPING * bw / denom;
    pilot->beta = 4.0 * bw * bw / denom;

    /*
     * A resonator on the way in, and the reason it is here rather than in a
     * comment about future work.
     *
     * The correlator multiplies the whole multiplex by the loop's oscillator
     * and smooths the result. That rejects most things, and not enough: a
     * station broadcasting in stereo puts a subcarrier at 38 kHz carrying
     * sidebands down to 23, and the loop came out tens of parts per million
     * off. Measured on this receiver, same correction and minutes apart, a
     * mono station read -12.9 ppm and a stereo one -59.2 -- a 46 ppm swing
     * from nothing but what the station was broadcasting. Synthesised, the
     * error goes from a couple of ppm to twenty-five.
     *
     * Two poles at the pilot, about 400 Hz wide. Wide enough for a receiver a
     * hundred ppm out (which is two hertz at 19 kHz) and narrow enough that
     * the nearest thing a broadcast multiplex carries is four kilohertz away.
     *
     * A two-pole resonator turns its input by about ninety degrees at
     * resonance, and that phase is not this filter's business to keep -- it
     * is tripled for the RDS subcarrier and doubled for the stereo
     * difference, so ninety degrees here is a hundred and eighty there and
     * the two audio channels come out swapped. They did. So the turn is
     * worked out once and taken back off the phase the loop hands out, while
     * the loop itself goes on tracking the filtered signal it can actually
     * see.
     */
    {
        double w = 2.0 * M_PI * FM_PILOT_HZ / pilot->sample_rate;
        double r = 1.0 - M_PI * FM_PILOT_BAND_HZ / pilot->sample_rate;

        if (r < 0.0)
            r = 0.0;
        pilot->band_a1 = 2.0 * r * cos(w);
        pilot->band_a2 = -r * r;
        pilot->band_b0 = 1.0 - r;
        {
            /* arg of 1 - a1 z^-1 - a2 z^-2 at z = e^jw; the filter turns its
               input by minus that. */
            double re = 1.0 - pilot->band_a1 * cos(w) -
                        pilot->band_a2 * cos(2.0 * w);
            double im = pilot->band_a1 * sin(w) +
                        pilot->band_a2 * sin(2.0 * w);
            pilot->band_phase = -atan2(im, re);
        }
    }

    pilot->average_k = 1.0 - exp(-1.0 / (FM_PILOT_AVERAGE_SECONDS *
                                         pilot->sample_rate));
    pilot->report_k = 1.0 - exp(-1.0 / (FM_PILOT_REPORT_SECONDS *
                                        pilot->sample_rate));
    pilot->lock_k = 1.0 - exp(-1.0 / (FM_PILOT_LOCK_SECONDS *
                                      pilot->sample_rate));
}

double fm_pilot_feed(struct fm_pilot *pilot, float sample) {
    double used, ci, cq, error;

    if (!pilot)
        return 0.0;
    used = pilot->phase;

    /* Through the resonator first: what reaches the correlator is the pilot
       and its immediate neighbourhood, not the whole multiplex. */
    {
        double filtered = pilot->band_b0 * (double)sample +
                          pilot->band_a1 * pilot->band[0] +
                          pilot->band_a2 * pilot->band[1];
        pilot->band[1] = pilot->band[0];
        pilot->band[0] = filtered;
        pilot->band[2] = (double)sample;   /* kept for the floor estimate */
        sample = (float)filtered;
    }

    /* Correlate this sample against the loop's own oscillator. The multiplex
       is real, so this is the usual two-quadrant mix: the in-phase arm is the
       amplitude and the quadrature arm is the error. */
    ci = sample * cos(used);
    cq = -sample * sin(used);

    /*
     * atan2 rather than the small-angle shortcut cq/ci.
     *
     * The pilot arrives buried in audio and the shortcut's error grows with
     * the angle, so a loop that has not pulled in yet gets a phase error that
     * is wrong in exactly the regime where it matters most.
     */
    error = atan2(cq, ci);

    /*
     * Smooth the arms, then take the magnitude -- not the other way round.
     *
     * Magnitude first is an incoherent measurement, and an incoherent
     * measurement inside a coherent loop is worth nothing: noise has a
     * magnitude as large as a pilot's, so the ratio below sat near 0.8 on a
     * band with nothing on it and the loop cheerfully reported a lock. Doing
     * it this way, the pilot's arms are steady and survive the smoother while
     * noise averages towards zero, which is the entire difference between
     * "there is energy here" and "there is a tone here".
     */
    {
        double a = pilot->average_k;
        pilot->i_average += a * (ci - pilot->i_average);
        pilot->q_average += a * (cq - pilot->q_average);
        pilot->amplitude = sqrt(pilot->i_average * pilot->i_average +
                                pilot->q_average * pilot->q_average);
        /* How big the multiplex is, so the pilot can be compared against
           what it is sitting in. */
        /* Against the multiplex as it arrived, not as the resonator left it:
           the presence test asks how big the pilot is compared with what it
           is sitting in, and the resonator has already removed most of that. */
        pilot->floor_estimate += a * (fabs(pilot->band[2]) -
                                      pilot->floor_estimate);
    }
    /*
     * Lock, on two time scales.
     *
     * The fast smoother above turns the correlator arms into a phasor: the
     * pilot, narrowbanded, with the audio averaged off it. Whether that
     * phasor is a *tone* is a question about how still it stands, and one
     * time scale cannot ask it -- at any instant noise has a phasor too,
     * pointing somewhere.
     *
     * So smooth it again, slowly, two ways: the phasor itself and its
     * length. A pilot the loop is locked to stands still, so averaging it
     * keeps its length and the ratio goes to one. Noise wanders, so the
     * vector average collapses while the average length does not, and the
     * ratio goes to zero. Modulation depth divides out of both, which the
     * first attempt at this did not manage: it read 0.020 on a station whose
     * pilot stands 48 dB over its noise and 0.024 on one that manages 29,
     * because the loud station has more audio underneath. Ranking them
     * backwards is worse than not ranking them.
     */
    {
        double b = pilot->lock_k;
        double len = pilot->amplitude;

        pilot->lock_i += b * (pilot->i_average - pilot->lock_i);
        pilot->lock_q += b * (pilot->q_average - pilot->lock_q);
        pilot->incoherent += b * (len - pilot->incoherent);
        pilot->coherence =
            pilot->incoherent > 1e-12
                ? sqrt(pilot->lock_i * pilot->lock_i +
                       pilot->lock_q * pilot->lock_q) / pilot->incoherent
                : 0.0;
    }

    pilot->error = error;
    pilot->frequency += pilot->beta * error;
    /*
     * The reported frequency is a slow average of the loop's, not the loop's
     * own state.
     *
     * A 10 Hz loop tracks everything slower than 10 Hz, including the shove
     * the audio gives it every cycle, so its instantaneous frequency jitters
     * by about a hertz -- which at 19 kHz is 50 ppm, and 50 ppm is the whole
     * quantity this is supposed to measure. The loop wants to be quick; the
     * number wants to be still.
     */
    {
        double a = pilot->report_k;
        pilot->frequency_average += a * (pilot->frequency -
                                         pilot->frequency_average);
    }
    pilot->phase += pilot->frequency + pilot->alpha * error;
    while (pilot->phase >= 2.0 * M_PI)
        pilot->phase -= 2.0 * M_PI;
    while (pilot->phase < 0.0)
        pilot->phase += 2.0 * M_PI;
    if (pilot->settled < (long)(pilot->sample_rate * 10.0))
        pilot->settled++;
    /*
     * The phase of the pilot as it arrived, not as the resonator left it.
     * Callers triple this for the RDS subcarrier and double it for the stereo
     * difference, so the filter's own turn has to come back off here or it is
     * multiplied along with everything else.
     */
    used -= pilot->band_phase;
    while (used >= 2.0 * M_PI)
        used -= 2.0 * M_PI;
    while (used < 0.0)
        used += 2.0 * M_PI;
    return used;
}

int fm_pilot_locked(const struct fm_pilot *pilot) {
    if (!pilot || pilot->incoherent <= 0.0)
        return 0;
    if (pilot->settled <
        (long)(FM_PILOT_SETTLE_SECONDS * pilot->sample_rate))
        return 0;
    /*
     * Both, and neither is enough alone.
     *
     * Coherence says the thing being tracked is a tone rather than noise. It
     * does not say the tone is *there*: given a clean signal with no pilot in
     * it, the loop settles on whatever coherent scrap sits near 19 kHz and
     * reports 0.74, which is a lock by any reading of the ratio. The size of
     * the pilot against the multiplex around it says the rest -- 0.020 on a
     * real station, 0.002 on a signal carrying no pilot at all.
     *
     * The size alone was tried as the whole test and ranks stations
     * backwards, because a loud station has more audio underneath it. It is a
     * poor comparison and a perfectly good presence check, which is all it is
     * asked for here.
     */
    if (pilot->floor_estimate <= 0.0)
        return 0;
    if (pilot->amplitude / pilot->floor_estimate < FM_PILOT_MIN_PRESENCE)
        return 0;
    return pilot->coherence >= FM_PILOT_MIN_COHERENCE;
}

double fm_pilot_hz(const struct fm_pilot *pilot) {
    if (!pilot)
        return 0.0;
    return pilot->frequency_average * pilot->sample_rate / (2.0 * M_PI);
}

/*
 * How far the pilot is from 19 kHz -- which is mostly a fact about the
 * transmitter, not about this receiver.
 *
 * This used to claim it measured the receiver's crystal, on the reasoning
 * that the ratio between the pilot sent and the pilot counted carries the
 * receiver's error whatever it rode in on. That reasoning is wrong, and the
 * numbers say so plainly. Five stations, one receiver, one sample clock, each
 * recorded twice within minutes:
 *
 *     89.5   +1.95 ppm        94.4   -57.3 ppm
 *     93.2  -18.3            97.4   -16.0
 *    100.3  -46.7
 *
 * Repeatable to about a ppm within a station and spread over 59 between them.
 * One clock cannot be five different amounts wrong, so the spread belongs to
 * the transmitters -- and it is well within their rights: IEC 60244 holds a
 * pilot to +-2 Hz, which at 19 kHz is +-105 ppm. The ratio carries *both*
 * errors and cannot separate them.
 *
 * So this is the pilot's offset and not the receiver's. It is worth reporting
 * -- a pilot hundreds of ppm out is a decode that has gone wrong rather than
 * a transmitter that has -- and it is worth nothing at all as a frequency
 * reference. The GSM FCCH and the LTE cell search measure a carrier whose
 * frequency is held to a part in ten million, which is why they are what the
 * calibration gate takes (ADR-0004).
 *
 * Two other things were learned finding that out, both worth keeping.
 *
 * Applying a correction moves the *sample clock* as well as the tuner: 30 ppm
 * of movement for a 35 ppm change, because librtlsdr's set_freq_correction
 * writes the RTL2832's resampler ratio as well as the tuner's divider.
 *
 * And the loop had a real bias of its own, on top of all this, which the
 * resonator in fm_pilot_init now removes -- see the comment there.
 */
double fm_pilot_ppm(const struct fm_pilot *pilot) {
    double measured;

    if (!fm_pilot_locked(pilot))
        return 0.0;
    measured = fm_pilot_hz(pilot);
    return (measured - FM_PILOT_HZ) / FM_PILOT_HZ * 1e6;
}

int fm_rds_front_init(struct fm_rds_front *front, double sample_rate) {
    if (!front)
        return -1;
    if (sample_rate < FM_MIN_SAMPLE_RATE)
        return -1;
    memset(front, 0, sizeof(*front));
    fm_pilot_init(&front->pilot, sample_rate);
    front->sample_rate = sample_rate;
    front->previous_phase = 0.0;
    front->turned = 0.0;
    /* One emission per pilot cycle. */
    front->next_emit = 2.0 * M_PI;
    front->lowpass_k = 1.0 - exp(-2.0 * M_PI * FM_RDS_LOWPASS_HZ /
                                 sample_rate);
    return 0;
}

size_t fm_rds_front_feed(struct fm_rds_front *front, const float *mpx,
                         size_t n, float *out_i, float *out_q,
                         size_t capacity) {
    size_t made = 0;

    if (!front || !mpx || !out_i || !out_q)
        return 0;

    for (size_t s = 0; s < n && made < capacity; s++) {
        double phase = fm_pilot_feed(&front->pilot, mpx[s]);
        double step = phase - front->previous_phase;
        double sub, ci, cq;

        /* Unwrap. The loop keeps its phase in [0, 2*pi) and a step back of
           nearly a whole turn is a wrap forward, not the pilot reversing. */
        if (step < -M_PI)
            step += 2.0 * M_PI;
        else if (step > M_PI)
            step -= 2.0 * M_PI;
        front->previous_phase = phase;
        front->turned += step;

        /*
         * Mix by three times the pilot phase, which *is* the subcarrier --
         * the station transmits the pilot so that this multiplication needs
         * no search. Down to DC, in one step, with the phase already right up
         * to whatever constant the channel added.
         */
        sub = 3.0 * phase;
        ci = mpx[s] * cos(sub);
        cq = -mpx[s] * sin(sub);

        /*
         * Low-pass before decimating, because the integrate-and-dump is not
         * an anti-alias filter and pretending it is costs real bits.
         *
         * Averaging over one pilot cycle is a boxcar whose first null is at
         * 19 kHz, so at the fold edge -- half of that -- it manages 3.9 dB.
         * Everything from 9.5 to 28.5 kHz of baseband folds in on top of the
         * data, and that range is multiplex 28.5 to 47.5 kHz, which is where
         * the stereo subcarrier and its sidebands live. Folding the loudest
         * thing in the multiplex onto a subcarrier 40 dB below it is not a
         * detail.
         *
         * Three one-pole sections rather than anything cleverer: the data is
         * +-2.4 kHz and the fold edge is 9.5, which is two octaves, so 6 dB
         * an octave three times over is 36 dB and the passband droop at
         * 2.4 kHz is under a decibel. A designed filter would buy a decibel
         * and cost a coefficient table.
         */
        {
            double k = front->lowpass_k;
            front->lp_i[0] += k * (ci - front->lp_i[0]);
            front->lp_i[1] += k * (front->lp_i[0] - front->lp_i[1]);
            front->lp_i[2] += k * (front->lp_i[1] - front->lp_i[2]);
            front->lp_q[0] += k * (cq - front->lp_q[0]);
            front->lp_q[1] += k * (front->lp_q[0] - front->lp_q[1]);
            front->lp_q[2] += k * (front->lp_q[1] - front->lp_q[2]);
            ci = front->lp_i[2];
            cq = front->lp_q[2];
        }

        front->acc_i += ci;
        front->acc_q += cq;
        front->acc_n++;

        if (front->turned >= front->next_emit) {
            if (front->acc_n > 0 && fm_pilot_locked(&front->pilot)) {
                out_i[made] = (float)(front->acc_i / (double)front->acc_n);
                out_q[made] = (float)(front->acc_q / (double)front->acc_n);
                made++;
            }
            front->acc_i = 0.0;
            front->acc_q = 0.0;
            front->acc_n = 0;
            front->next_emit += 2.0 * M_PI;
        }
    }
    return made;
}

/*
 * The biphase filter's taps, at one of two shapes.
 *
 * Rectangular is eight samples of +1 then eight of -1 -- the symbol as the
 * standard defines it. Shaped weights each half by a half-sine, which is the
 * matched filter for the band-limited pulse that is actually transmitted.
 *
 * Built once per call rather than per symbol: sixteen sines against tens of
 * thousands of symbols is not worth a table, but doing it inside the inner
 * loop would be.
 */
static void fm_rds_taps(enum fm_rds_filter filter,
                        double taps[FM_RDS_SAMPLES_PER_SYMBOL]) {
    const int half = FM_RDS_SAMPLES_PER_SYMBOL / 2;
    int k;

    for (k = 0; k < FM_RDS_SAMPLES_PER_SYMBOL; k++) {
        double sign = k < half ? 1.0 : -1.0;
        if (filter == FM_RDS_FILTER_SHAPED) {
            /*
             * Half a sine across each half, so the taps rise and fall rather
             * than switching. Scaled by root two so the two filters carry the
             * same energy: a rectangular tap set sums to sixteen, and sin
             * squared averages a half, so the shaped one would otherwise sum
             * to eight and read uniformly quieter. Comparing them at
             * different gains would measure the scaling, not the shape.
             */
            double phase = M_PI * ((double)(k % half) + 0.5) / (double)half;
            taps[k] = sign * sin(phase) * 1.4142135623730951;
        } else {
            taps[k] = sign;
        }
    }
}

/* The filter at one timing offset. */
static void fm_rds_correlate(const float *bb_i, const float *bb_q,
                             const double *taps, int offset, size_t index,
                             double *ri, double *rq) {
    size_t base = (size_t)offset + index * FM_RDS_SAMPLES_PER_SYMBOL;
    double ai = 0.0, aq = 0.0;

    for (int k = 0; k < FM_RDS_SAMPLES_PER_SYMBOL; k++) {
        ai += taps[k] * bb_i[base + (size_t)k];
        aq += taps[k] * bb_q[base + (size_t)k];
    }
    *ri = ai;
    *rq = aq;
}

size_t fm_rds_soft_bits_with(const float *bb_i, const float *bb_q,
                             size_t samples, enum fm_rds_filter filter,
                             float *soft, size_t capacity, int *timing_offset,
                             double *axis_radians) {
    const int span = FM_RDS_SAMPLES_PER_SYMBOL;
    double taps[FM_RDS_SAMPLES_PER_SYMBOL];

    fm_rds_taps(filter, taps);
    int best_offset = 0;
    double best_energy = -1.0;
    size_t symbols, made = 0;
    double sum_i = 0.0, sum_q = 0.0, axis;

    if (timing_offset)
        *timing_offset = 0;
    if (axis_radians)
        *axis_radians = 0.0;
    if (!bb_i || !bb_q || !soft || samples < (size_t)(4 * span))
        return 0;

    /*
     * Timing. Every offset is tried and the strongest wins -- a search with a
     * right answer rather than a loop, which is what having the clock from
     * the pilot buys. The measure is mean square rather than mean, because a
     * BPSK symbol is as often negative as positive and the mean of the right
     * offset is zero, the same as the mean of the wrong one.
     */
    for (int offset = 0; offset < span; offset++) {
        size_t count = (samples - (size_t)offset) / (size_t)span;
        double energy = 0.0;

        if (count < 4)
            continue;
        for (size_t k = 0; k < count; k++) {
            double ri, rq;
            fm_rds_correlate(bb_i, bb_q, taps, offset, k, &ri, &rq);
            energy += ri * ri + rq * rq;
        }
        energy /= (double)count;
        if (energy > best_energy) {
            best_energy = energy;
            best_offset = offset;
        }
    }
    if (best_energy < 0.0)
        return 0;
    if (timing_offset)
        *timing_offset = best_offset;

    symbols = (samples - (size_t)best_offset) / (size_t)span;

    /*
     * The axis. Squaring collapses the two BPSK points onto one, so the sum
     * of the squares points along twice the axis and half its angle is the
     * axis -- modulo 180 degrees, which the differential decoding below is
     * indifferent to.
     */
    for (size_t k = 0; k < symbols; k++) {
        double ri, rq;
        fm_rds_correlate(bb_i, bb_q, taps, best_offset, k, &ri, &rq);
        sum_i += ri * ri - rq * rq;
        sum_q += 2.0 * ri * rq;
    }
    axis = 0.5 * atan2(sum_q, sum_i);
    if (axis_radians)
        *axis_radians = axis;

    /*
     * Project onto that axis, then differentially decode: the data bit is
     * whether the channel bit changed, so the product of consecutive symbols
     * carries it, positive for no change. Positive means more likely zero,
     * the convention gsm_bcch.h and lte_mib.h both use.
     *
     * One symbol goes in and does not come out: the first has nothing before
     * it to have changed from.
     */
    {
        double previous = 0.0;
        int have_previous = 0;
        double ca = cos(axis), sa = sin(axis);

        for (size_t k = 0; k < symbols && made < capacity; k++) {
            double ri, rq, projected;

            fm_rds_correlate(bb_i, bb_q, taps, best_offset, k, &ri, &rq);
            projected = ri * ca + rq * sa;
            if (have_previous)
                soft[made++] = (float)(projected * previous);
            previous = projected;
            have_previous = 1;
        }
    }
    return made;
}

/*
 * A radix-2 FFT, in place, self-contained.
 *
 * Hand-written like the rest of the DSP here (ADR-0003). sdr_dsp.c has one
 * too, and this does not call it: that one is built around the receiver's
 * complex I/Q and a two-sided spectrum with a peak hold, and borrowing it
 * would mean matching its bin layout for a chart that wants neither.
 */
static void fm_fft(double *re, double *im, int n) {
    int i, j, len;

    for (i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (len = 2; len <= n; len <<= 1) {
        double angle = -2.0 * M_PI / len;
        double wr = cos(angle), wi = sin(angle);
        for (i = 0; i < n; i += len) {
            double cr = 1.0, ci = 0.0;
            for (j = 0; j < len / 2; j++) {
                double ur = re[i + j], ui = im[i + j];
                double vr = re[i + j + len / 2] * cr -
                            im[i + j + len / 2] * ci;
                double vi = re[i + j + len / 2] * ci +
                            im[i + j + len / 2] * cr;
                double nr;
                re[i + j] = ur + vr;
                im[i + j] = ui + vi;
                re[i + j + len / 2] = ur - vr;
                im[i + j + len / 2] = ui - vi;
                nr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr;
                cr = nr;
            }
        }
    }
}

size_t fm_multiplex_spectrum(const float *mpx, size_t n, double sample_rate,
                             float *dbfs, size_t bins, double *bin_hz) {
    const int size = 2 * FM_MPX_SPECTRUM_BINS;
    static double re[2 * FM_MPX_SPECTRUM_BINS];
    static double im[2 * FM_MPX_SPECTRUM_BINS];
    static double power[FM_MPX_SPECTRUM_BINS];
    int decimate, k;
    size_t used = 0, windows = 0, at = 0;
    double rate;

    if (bin_hz)
        *bin_hz = 0.0;
    if (!mpx || !dbfs || bins == 0 || sample_rate <= 0.0)
        return 0;
    if (bins > FM_MPX_SPECTRUM_BINS)
        bins = FM_MPX_SPECTRUM_BINS;

    decimate = (int)(sample_rate / FM_MPX_SPECTRUM_TARGET_RATE + 0.5);
    if (decimate < 1)
        decimate = 1;
    rate = sample_rate / decimate;
    if (bin_hz)
        *bin_hz = rate / size;
    if (n < (size_t)(size * decimate))
        return 0;

    for (k = 0; k < FM_MPX_SPECTRUM_BINS; k++)
        power[k] = 0.0;

    /* As many windows as the input holds, averaged. One window of a
       broadcast multiplex is mostly audio and the subcarriers only stand out
       once the audio has been averaged across a few of them. */
    while (at + (size_t)(size * decimate) <= n && windows < 32) {
        for (k = 0; k < size; k++) {
            double sum = 0.0;
            int d;
            double window = 0.5 - 0.5 * cos(2.0 * M_PI * k / (size - 1));

            for (d = 0; d < decimate; d++)
                sum += mpx[at + (size_t)(k * decimate + d)];
            re[k] = window * sum / decimate;
            im[k] = 0.0;
        }
        fm_fft(re, im, size);
        for (k = 0; k < FM_MPX_SPECTRUM_BINS; k++)
            power[k] += re[k] * re[k] + im[k] * im[k];
        windows++;
        at += (size_t)(size * decimate);
    }
    if (windows == 0)
        return 0;

    for (used = 0; used < bins; used++) {
        double p = power[used] / (double)windows;
        /* Referenced to the strongest bin, since a discriminator's output is
           radians a sample and an absolute dBFS would mean nothing. */
        dbfs[used] = (float)(10.0 * log10(p > 1e-30 ? p : 1e-30));
    }
    {
        float top = dbfs[0];
        size_t i;
        for (i = 1; i < used; i++)
            if (dbfs[i] > top)
                top = dbfs[i];
        for (i = 0; i < used; i++)
            dbfs[i] -= top;
    }
    return used;
}

void fm_rds_timing_scores(const float *bb_i, const float *bb_q, size_t samples,
                          float scores[FM_RDS_SAMPLES_PER_SYMBOL]) {
    const int span = FM_RDS_SAMPLES_PER_SYMBOL;
    double taps[FM_RDS_SAMPLES_PER_SYMBOL];
    double best = 0.0;
    int offset;

    /* The chart shows what the decode is doing, so it uses the decode's
       filter rather than one of its own. */
    fm_rds_taps(FM_RDS_FILTER_RECTANGULAR, taps);

    if (!scores)
        return;
    for (offset = 0; offset < span; offset++)
        scores[offset] = 0.0f;
    if (!bb_i || !bb_q || samples < (size_t)(4 * span))
        return;

    for (offset = 0; offset < span; offset++) {
        size_t count = (samples - (size_t)offset) / (size_t)span;
        double energy = 0.0;
        size_t k;

        if (count < 4)
            continue;
        for (k = 0; k < count; k++) {
            double ri, rq;
            fm_rds_correlate(bb_i, bb_q, taps, offset, k, &ri, &rq);
            energy += ri * ri + rq * rq;
        }
        energy /= (double)count;
        scores[offset] = (float)energy;
        if (energy > best)
            best = energy;
    }
    if (best <= 0.0)
        return;
    for (offset = 0; offset < span; offset++)
        scores[offset] = (float)(scores[offset] / best);
}

size_t fm_rds_soft_bits(const float *bb_i, const float *bb_q, size_t samples,
                        float *soft, size_t capacity, int *timing_offset,
                        double *axis_radians) {
    /* Rectangular, and measured rather than assumed: see
       docs/rds-matched-filter.md. The shaped filter is the theoretically
       correct one and is never worse, but the two are identical above about
       13 dB and the window where they differ at all is three decibels wide. */
    return fm_rds_soft_bits_with(bb_i, bb_q, samples,
                                 FM_RDS_FILTER_RECTANGULAR, soft, capacity,
                                 timing_offset, axis_radians);
}

size_t fm_rds_symbols(const float *bb_i, const float *bb_q, size_t samples,
                      int timing_offset, float *out_i, float *out_q,
                      size_t capacity) {
    const int span = FM_RDS_SAMPLES_PER_SYMBOL;
    double taps[FM_RDS_SAMPLES_PER_SYMBOL];
    size_t count, k, made = 0;
    double scale = 0.0;

    if (!bb_i || !bb_q || !out_i || !out_q || capacity == 0)
        return 0;
    /* The scatter shows what the decode is doing, so it uses the decode's
       filter. */
    fm_rds_taps(FM_RDS_FILTER_RECTANGULAR, taps);
    if (timing_offset < 0 || timing_offset >= span)
        return 0;
    if (samples < (size_t)(4 * span))
        return 0;

    count = (samples - (size_t)timing_offset) / (size_t)span;
    if (count > capacity)
        count = capacity;

    for (k = 0; k < count; k++) {
        double ri, rq, magnitude;

        fm_rds_correlate(bb_i, bb_q, taps, timing_offset, k, &ri, &rq);
        out_i[made] = (float)ri;
        out_q[made] = (float)rq;
        magnitude = sqrt(ri * ri + rq * rq);
        if (magnitude > scale)
            scale = magnitude;
        made++;
    }
    if (scale > 0.0)
        for (k = 0; k < made; k++) {
            out_i[k] = (float)(out_i[k] / scale);
            out_q[k] = (float)(out_q[k] / scale);
        }
    return made;
}

int fm_audio_init(struct fm_audio *audio, double sample_rate) {
    if (!audio || sample_rate < FM_MIN_SAMPLE_RATE)
        return -1;
    memset(audio, 0, sizeof(*audio));
    audio->sample_rate = sample_rate;
    audio->decimate = (int)(sample_rate / FM_AUDIO_TARGET_RATE + 0.5);
    if (audio->decimate < 1)
        audio->decimate = 1;
    audio->audio_rate = sample_rate / audio->decimate;
    audio->lowpass_k = 1.0 - exp(-2.0 * M_PI * FM_AUDIO_TOP_HZ / sample_rate);
    /* De-emphasis runs after the decimation, so its corner is relative to the
       audio rate rather than the capture rate. */
    audio->deemphasis_k = 1.0 - exp(-1.0 / (FM_DEEMPHASIS_SECONDS *
                                            audio->audio_rate));
    audio->level = 0.0;
    fm_pilot_init(&audio->pilot, sample_rate);
    return 0;
}

double fm_audio_rate(const struct fm_audio *audio) {
    return audio ? audio->audio_rate : 0.0;
}

size_t fm_audio_mono(struct fm_audio *audio, const float *mpx, size_t n,
                     int16_t *out, size_t capacity) {
    return fm_audio_decode(audio, mpx, n, out, NULL, capacity);
}

int fm_audio_is_stereo(const struct fm_audio *audio) {
    return audio ? audio->stereo : 0;
}

size_t fm_audio_decode(struct fm_audio *audio, const float *mpx, size_t n,
                       int16_t *mono, int16_t *stereo, size_t capacity) {
    size_t made = 0, s;
    double decay;

    if (!audio || !mpx || audio->decimate < 1)
        return 0;
    if (!mono && !stereo)
        return 0;
    decay = 1.0 - exp(-(double)audio->decimate /
                      (FM_AUDIO_AGC_SECONDS * audio->sample_rate));

    for (s = 0; s < n && made < capacity; s++) {
        double value = mpx[s];
        double theta = fm_pilot_feed(&audio->pilot, (float)value);
        double difference;

        /*
         * Low-pass to 14 kHz before decimating. Three sections, which puts
         * the 19 kHz pilot about 17 dB down -- audible to nobody and, once
         * decimated, folded to 31 kHz where it is audible to nothing.
         */
        audio->lowpass[0] += audio->lowpass_k * (value - audio->lowpass[0]);
        audio->lowpass[1] += audio->lowpass_k * (audio->lowpass[0] -
                                                 audio->lowpass[1]);
        audio->lowpass[2] += audio->lowpass_k * (audio->lowpass[1] -
                                                 audio->lowpass[2]);
        audio->accumulator += audio->lowpass[2];

        /*
         * And the difference, brought down from 38 kHz by twice the pilot's
         * phase. Twice because a suppressed-carrier product halves the
         * amplitude, and the same filter chain because it has to arrive with
         * the same delay as the sum or the stereo image smears.
         */
        difference = 2.0 * value * cos(2.0 * theta);
        audio->difference_lowpass[0] += audio->lowpass_k *
            (difference - audio->difference_lowpass[0]);
        audio->difference_lowpass[1] += audio->lowpass_k *
            (audio->difference_lowpass[0] - audio->difference_lowpass[1]);
        audio->difference_lowpass[2] += audio->lowpass_k *
            (audio->difference_lowpass[1] - audio->difference_lowpass[2]);
        audio->difference_accumulator += audio->difference_lowpass[2];

        audio->accumulated++;
        if (audio->accumulated < audio->decimate)
            continue;
        {
            double sum = audio->accumulator / audio->accumulated;
            double diff = audio->difference_accumulator / audio->accumulated;
            double left, right, gain;

            audio->accumulator = 0.0;
            audio->difference_accumulator = 0.0;
            audio->accumulated = 0;

            /* De-emphasis: a broadcaster lifts the treble before
               transmitting and this puts it back, which is most of the
               difference between "harsh" and "a radio". Both channels, or
               the image shifts with frequency. */
            audio->deemphasis += audio->deemphasis_k * (sum -
                                                        audio->deemphasis);
            sum = audio->deemphasis;
            audio->difference_deemphasis += audio->deemphasis_k *
                (diff - audio->difference_deemphasis);
            diff = audio->difference_deemphasis;

            audio->stereo = fm_pilot_locked(&audio->pilot);
            if (!audio->stereo)
                diff = 0.0;

            /* A slow peak follower on the sum, so a weak station is audible
               at the same setting as a strong one -- and on the sum rather
               than on either channel, or a passage panned hard to one side
               would pull the other one down with it. */
            if (fabs(sum) > audio->level)
                audio->level = fabs(sum);
            else
                audio->level += decay * (fabs(sum) - audio->level);

            gain = audio->level > 1e-6 ? 0.7 / audio->level : 0.0;
            left = (sum + diff) * 0.5 * gain;
            right = (sum - diff) * 0.5 * gain;
            if (left > 1.0) left = 1.0;
            if (left < -1.0) left = -1.0;
            if (right > 1.0) right = 1.0;
            if (right < -1.0) right = -1.0;

            if (mono) {
                double m = sum * gain;
                if (m > 1.0) m = 1.0;
                if (m < -1.0) m = -1.0;
                mono[made] = (int16_t)(m * 32000.0);
            }
            if (stereo) {
                stereo[made * 2] = (int16_t)(left * 32000.0);
                stereo[made * 2 + 1] = (int16_t)(right * 32000.0);
            }
            made++;
        }
    }
    return made;
}
