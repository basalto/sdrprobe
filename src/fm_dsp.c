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
