#ifndef SIGNAL_FINDINGS_H
#define SIGNAL_FINDINGS_H

#include <stdio.h>
#include <string.h>

#include "signal_probe.h"

/*
 * What the candidate panel's numbers amount to, in words -- and, more often
 * here than anywhere else in this program, what they do not.
 *
 * The panel above this says peak power, height above the local floor,
 * occupied bandwidth, duty, frequency stability and which allocation the
 * frequency falls in. Every one of those is about *presence*. A reader who
 * clicks Scan learns that something is there and is left to guess what.
 *
 * These are the conclusions the measurements support. `lte_findings.h` is the
 * pattern and three of its habits are copied deliberately, each learned the
 * hard way: **every sentence names the measurement it rests on**, because a
 * sentence without its number is an assertion; a caveat shares a line with
 * the claim it qualifies rather than trailing after it; and refusals are kept
 * rather than omitted, because a reader who is not told cannot know the
 * question was asked.
 *
 * For a signal nobody has identified the refusals are most of the value. The
 * band plan says what a frequency is *allocated* to and can say nothing about
 * what occupies it (ADR-0015) -- band 28 is labelled an LTE downlink and
 * carries 5G NR, and 75.000 MHz is labelled ILS markers and carries a clock
 * harmonic. Saying "no symbol rate was looked for, and here is why" is worth
 * more than silence, which reads as "looked and found nothing".
 *
 * **This must not become a verdict.** "Probably TETRA" is a claim this
 * program has no way to stand behind; "18 kBd, 25 kHz wide, continuous" is a
 * set of measurements that lets a reader reach for the TETRA view themselves.
 * Every technology here was identified by decoding something the transmitter
 * said.
 *
 * Pure arithmetic over the measurements -- no window, no receiver, no samples
 * (ADR-0012).
 */

/*
 * Six lines and 48 characters of each are what the panel can show.
 *
 * Both numbers are measured rather than chosen: at the default window the
 * detail panel is about 430 px wide and the font runs about 8.3 px to the
 * character, so a longer sentence is truncated by sdrgui_text_fit() and the
 * first draft lost exactly the wrong halves -- "only 1% of the ch..." and
 * "no symbol rate was looked for: that needs the chann...". A caveat cut off
 * before its qualifier is worse than no caveat, so the sentences are short
 * enough to survive.
 */
#define SIGNAL_FINDING_LINES 8
#define SIGNAL_FINDING_FIT 48
#define SIGNAL_FINDING_TEXT 128

struct signal_findings {
    char line[SIGNAL_FINDING_LINES][SIGNAL_FINDING_TEXT];
    int count;
};

static inline void signal_finding_text(struct signal_findings *f,
                                       const char *text) {
    if (!f || f->count >= SIGNAL_FINDING_LINES)
        return;
    snprintf(f->line[f->count], SIGNAL_FINDING_TEXT, "%s", text);
    f->count++;
}

static inline void signal_finding_add(struct signal_findings *f,
                                      const char *fmt, double a, double b) {
    if (!f || f->count >= SIGNAL_FINDING_LINES)
        return;
    snprintf(f->line[f->count], SIGNAL_FINDING_TEXT, fmt, a, b);
    f->count++;
}

/*
 * A duty this low means one sweep step is as likely to miss the signal as to
 * catch it, which is the finding `.scratch/bursty-signals/` was raised for:
 * on 1550-1766 MHz five identical sweeps found fifteen frequencies and no
 * frequency twice, and the confirmation pass refuted nine of ten.
 */
#define SIGNAL_DUTY_UNRELIABLE 0.35

/*
 * How steady a carrier has to be before its own drift is worth remarking on.
 * A telescopic whip on an uncalibrated R820T is tens of ppm out, so a few
 * kilohertz at UHF is the receiver rather than the transmitter -- which is
 * why this is a threshold on the *scatter between looks* and not on any
 * absolute offset.
 */
#define SIGNAL_STEADY_HZ 2000.0

/*
 * `carrier` may be NULL, which is the case before a block has been measured;
 * `duty_blocks` of 0 means the duty was never measured. Both produce a
 * refusal rather than nothing at all.
 *
 * Returns how many lines were written.
 */
static inline int signal_findings_from(const struct signal_carrier *carrier,
                                       const struct signal_bursts *bursts,
                                       const struct signal_envelope *envelope,
                                       double duty, int duty_hits,
                                       int duty_blocks, double spread_hz,
                                       struct signal_findings *out) {
    enum signal_verdict verdict;

    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));

    if (!carrier || !carrier->found) {
        signal_finding_text(out,
            "not looked at yet: needs a live receiver");
        return out->count;
    }

    verdict = signal_carrier_verdict(carrier);
    switch (verdict) {
    case SIGNAL_BARE:
        signal_finding_add(out, "a bare carrier, %.0f dB over its floor",
                           carrier->carrier_over_noise_db, 0.0);
        signal_finding_add(out, "%.0f%% of the channel stands still",
                           carrier->carrier_power_fraction * 100.0, 0.0);
        signal_finding_text(out,
            "nothing rides it: nothing here to decode");
        break;
    case SIGNAL_MODULATED:
        signal_finding_add(out, "a modulated carrier, %.0f dB over its floor",
                           carrier->carrier_over_noise_db, 0.0);
        /* "only 0% stands still" is what a percentage rounds a real
           measurement down to, and it reads as a missing number rather than
           a small one. Below half a percent the words are the honest form. */
        if (carrier->carrier_power_fraction < 0.005)
            signal_finding_text(out,
                "almost none of the channel stands still");
        else
            signal_finding_add(out, "only %.0f%% of the channel stands still",
                               carrier->carrier_power_fraction * 100.0, 0.0);
        signal_finding_text(out,
            "something rides it; what, this cannot say");
        break;
    default:
        /*
         * The two cases that share this answer are worth separating in the
         * sentence, because they are not the same thing and a reader who
         * takes one for the other looks in the wrong place. A search over
         * thousands of frequencies takes the largest of thousands of noise
         * samples, so pure noise reliably reaches 8 to 14 dB over its own
         * median -- and a *pulsed* transmission has real energy and no
         * standing carrier at all, which is why adsb_cpr_pair.bin reads
         * -2.0 dB here and is correctly not called a bare tone.
         */
        signal_finding_add(out,
            "no standing carrier: best line %.0f dB up",
            carrier->carrier_over_noise_db, 0.0);
        /*
         * Two things read this way and they are different findings. Before
         * the envelope was measured this had to name both and leave the
         * reader to choose; now the burst measurement decides it, so the
         * speculative half is only printed when nothing settles it.
         */
        if (!bursts || bursts->verdict != SIGNAL_BURST_SEPARABLE) {
            signal_finding_add(out, "a search on noise alone reaches %.0f dB",
                               SIGNAL_CARRIER_PRESENT_DB, 0.0);
            signal_finding_text(out,
                "and a pulsed transmission reads the same");
        }
        break;
    }

    if (duty_blocks > 0 && duty < SIGNAL_DUTY_UNRELIABLE)
        signal_finding_add(out,
            "comes and goes: up in %.0f of %.0f looks",
            (double)duty_hits, (double)duty_blocks);
    else if (duty_blocks > 0 && spread_hz > SIGNAL_STEADY_HZ)
        signal_finding_add(out,
            "steady in time, +/- %.1f kHz in frequency",
            spread_hz / 1e3, 0.0);
    (void)duty;

    /*
     * The refusal, and it is the most useful line on the panel for anything
     * that turns out to be modulated. A symbol rate is measurable
     * (signal_symbol_line), and measuring it needs the channel mixed down and
     * filtered to its own width first -- this looks at the whole 2 MHz span,
     * where the line is buried. Saying so is the difference between "there is
     * no symbol rate" and "nobody asked".
     */
    /*
     * What the envelope says about time, which no amount of spectrum can:
     * `survey_measure_duty()` folds one 65.5 ms block at a time, so a 120
     * microsecond squitter and a continuous carrier are the same measurement
     * to it -- a factor of five hundred, and the difference between a
     * transmitter and a transmission.
     */
    if (bursts && bursts->verdict == SIGNAL_BURST_SEPARABLE) {
        signal_finding_add(out, "it transmits in bursts, %.0f us long",
                           bursts->median_seconds * 1e6, 0.0);
        if (bursts->median_gap_seconds > 0.0)
            signal_finding_add(out, "one every %.1f ms, busy %.2f%% of it",
                               bursts->median_gap_seconds * 1e3,
                               bursts->occupancy * 100.0);
    } else if (bursts && bursts->verdict == SIGNAL_BURST_BUSY) {
        signal_finding_add(out, "and it does not stop: busy %.0f%% of the look",
                           bursts->occupancy * 100.0, 0.0);
    }

    /*
     * What the envelope's shape says, in the only two places it says
     * something this program can stand behind.
     *
     * The band between the two thresholds is left unnamed on purpose: it is
     * where an empty channel sits, at Rayleigh's 0.52, so a sentence about it
     * would be a sentence about nothing being there.
     *
     * And what is named is named weakly for a reason. A bare carrier in noise
     * reads 0.248 and filtered pi/4-DQPSK reads 0.252 -- the same number --
     * so "hardly varies" is true of both and separates neither.
     * `carrier_power_fraction` above is what tells those two apart.
     *
     * And a low reading means "constant envelope" only for something that
     * does not stop -- GSM is constant-envelope by construction and reads up
     * to 0.79, because it is also TDMA. Which is why this is printed after
     * the burst lines rather than before them.
     */
    if (envelope && envelope->found) {
        if (envelope->variation < SIGNAL_ENVELOPE_CONTAINED)
            signal_finding_add(out,
                "envelope hardly varies: %.2f, noise %.2f",
                envelope->variation, SIGNAL_ENVELOPE_RAYLEIGH);
        else if (envelope->variation > SIGNAL_ENVELOPE_RESTLESS)
            signal_finding_add(out,
                "envelope varies past noise: %.2f vs %.2f",
                envelope->variation, SIGNAL_ENVELOPE_RAYLEIGH);
    }

    if (verdict == SIGNAL_MODULATED)
        signal_finding_text(out,
            "no symbol rate looked for: needs one channel");

    return out->count;
}

#endif
