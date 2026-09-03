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
}

double fm_pilot_feed(struct fm_pilot *pilot, float sample) {
    double used, ci, cq, error;

    if (!pilot)
        return 0.0;
    used = pilot->phase;

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
        double a = 1.0 - exp(-1.0 / (FM_PILOT_AVERAGE_SECONDS *
                                     pilot->sample_rate));
        pilot->i_average += a * (ci - pilot->i_average);
        pilot->q_average += a * (cq - pilot->q_average);
        pilot->amplitude = sqrt(pilot->i_average * pilot->i_average +
                                pilot->q_average * pilot->q_average);
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
        double b = 1.0 - exp(-1.0 / (FM_PILOT_LOCK_SECONDS *
                                     pilot->sample_rate));
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
        double a = 1.0 - exp(-1.0 / (FM_PILOT_REPORT_SECONDS *
                                     pilot->sample_rate));
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
    return used;
}

int fm_pilot_locked(const struct fm_pilot *pilot) {
    if (!pilot || pilot->incoherent <= 0.0)
        return 0;
    if (pilot->settled <
        (long)(FM_PILOT_SETTLE_SECONDS * pilot->sample_rate))
        return 0;
    return pilot->coherence >= FM_PILOT_MIN_COHERENCE;
}

double fm_pilot_hz(const struct fm_pilot *pilot) {
    if (!pilot)
        return 0.0;
    return pilot->frequency_average * pilot->sample_rate / (2.0 * M_PI);
}

/*
 * What the pilot says about this receiver's crystal.
 *
 * A broadcast pilot is a laboratory-grade reference -- the standard holds it
 * to +-2 Hz, which at 19 kHz is 105 ppm of the *pilot* but the pilot is not
 * what is being measured. What is measured is the ratio between the pilot the
 * transmitter sent and the one this receiver counted, and that ratio carries
 * the receiver's error whatever carrier it rode in on.
 *
 * The catch, and the reason this is reported rather than fed to the
 * calibration gate: the error is measured at *baseband*, after the
 * discriminator, so it says how wrong the sample clock is and says nothing
 * about the tuner's local oscillator. The GSM FCCH and the LTE cell search
 * both measure an offset at the tuned frequency and so catch both. Two
 * quantities that are usually equal on an RTL-SDR, because one crystal feeds
 * both -- usually, and a calibration gate is not the place for usually
 * (ADR-0004).
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
 * The biphase matched filter at one timing offset: eight samples of +1 then
 * eight of -1, which is the shape of the symbol itself.
 */
static void fm_rds_correlate(const float *bb_i, const float *bb_q,
                             size_t samples, int offset, size_t index,
                             double *ri, double *rq) {
    const int half = FM_RDS_SAMPLES_PER_SYMBOL / 2;
    size_t base = (size_t)offset + index * FM_RDS_SAMPLES_PER_SYMBOL;
    double ai = 0.0, aq = 0.0;

    (void)samples;
    for (int k = 0; k < FM_RDS_SAMPLES_PER_SYMBOL; k++) {
        double sign = k < half ? 1.0 : -1.0;
        ai += sign * bb_i[base + (size_t)k];
        aq += sign * bb_q[base + (size_t)k];
    }
    *ri = ai;
    *rq = aq;
}

size_t fm_rds_soft_bits(const float *bb_i, const float *bb_q, size_t samples,
                        float *soft, size_t capacity, int *timing_offset,
                        double *axis_radians) {
    const int span = FM_RDS_SAMPLES_PER_SYMBOL;
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
            fm_rds_correlate(bb_i, bb_q, samples, offset, k, &ri, &rq);
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
        fm_rds_correlate(bb_i, bb_q, samples, best_offset, k, &ri, &rq);
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

            fm_rds_correlate(bb_i, bb_q, samples, best_offset, k, &ri, &rq);
            projected = ri * ca + rq * sa;
            if (have_previous)
                soft[made++] = (float)(projected * previous);
            previous = projected;
            have_previous = 1;
        }
    }
    return made;
}
