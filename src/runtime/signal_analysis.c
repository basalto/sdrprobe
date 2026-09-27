#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "signal_analysis.h"
#include "signal_findings.h"
#include "signal_probe.h"
#include "srd_dsp.h"

int signal_analysis_run(const float *i_samples, const float *q_samples,
                        size_t pair_count, double sample_rate,
                        double target_hz, float full_scale,
                        const char *technology_hint,
                        struct signal_analysis_result *out) {
    if (!out)
        return -1;
    memset(out, 0, sizeof(*out));

    if (!i_samples || !q_samples || pair_count < 1024 || !(sample_rate > 0.0))
        return -1;

    out->valid = 1;
    out->sample_rate = sample_rate;
    out->duration_seconds = (double)pair_count / sample_rate;
    out->target_hz = target_hz;

    /* 1. Carrier measurement */
    struct signal_carrier carrier;
    int carrier_ok = signal_find_carrier(i_samples, q_samples, pair_count,
                                         sample_rate,
                                         target_hz - 25000.0, target_hz + 25000.0,
                                         2000.0, 50000.0, &carrier);

    out->carrier_found = carrier_ok && carrier.found;
    if (out->carrier_found) {
        out->carrier_offset_hz = carrier.offset_hz;
        out->carrier_over_noise_db = carrier.carrier_over_noise_db;
        out->carrier_power_fraction = carrier.carrier_power_fraction;

        enum signal_verdict v = signal_carrier_verdict(&carrier);
        if (v == SIGNAL_BARE) {
            snprintf(out->carrier_finding, sizeof(out->carrier_finding),
                     "a bare carrier, %.0f dB over its floor",
                     carrier.carrier_over_noise_db);
            snprintf(out->standing_sentence, sizeof(out->standing_sentence),
                     "%.0f%% of the channel stands still",
                     carrier.carrier_power_fraction * 100.0);
            snprintf(out->verdict_summary, sizeof(out->verdict_summary),
                     "Bare carrier");
        } else if (v == SIGNAL_MODULATED) {
            snprintf(out->carrier_finding, sizeof(out->carrier_finding),
                     "a modulated carrier, %.0f dB over its floor",
                     carrier.carrier_over_noise_db);
            snprintf(out->standing_sentence, sizeof(out->standing_sentence),
                     "only %.0f%% stands still; information rides it",
                     carrier.carrier_power_fraction * 100.0);
            snprintf(out->verdict_summary, sizeof(out->verdict_summary),
                     "Modulated carrier");
        } else {
            snprintf(out->carrier_finding, sizeof(out->carrier_finding),
                     "a line at %+.1f kHz, but %.0f dB is below 15 dB bar",
                     carrier.offset_hz / 1e3, carrier.carrier_over_noise_db);
            snprintf(out->standing_sentence, sizeof(out->standing_sentence),
                     "no carrier: energy does not stand still");
            snprintf(out->verdict_summary, sizeof(out->verdict_summary),
                     "No standing carrier");
        }
    } else {
        snprintf(out->carrier_finding, sizeof(out->carrier_finding),
                 "no line found within +/- 25 kHz");
        snprintf(out->standing_sentence, sizeof(out->standing_sentence),
                 "no carrier: energy does not stand still");
        snprintf(out->verdict_summary, sizeof(out->verdict_summary),
                 "No standing carrier");
    }

    /* 2. Envelope measurement */
    struct signal_envelope env;
    int env_ok = signal_envelope_stats(i_samples, q_samples, pair_count,
                                       sample_rate, target_hz, 50000.0,
                                       full_scale, &env);
    out->envelope_valid = env_ok && env.found;
    if (out->envelope_valid) {
        out->envelope_variation = env.variation;
        out->peak_over_mean_db = env.peak_over_mean_db;

        if (env.variation < SIGNAL_ENVELOPE_CONTAINED) {
            snprintf(out->envelope_finding, sizeof(out->envelope_finding),
                     "envelope hardly varies: %.2f (noise %.2f)",
                     env.variation, SIGNAL_ENVELOPE_RAYLEIGH);
        } else if (env.variation > SIGNAL_ENVELOPE_RESTLESS) {
            snprintf(out->envelope_finding, sizeof(out->envelope_finding),
                     "envelope varies past noise: %.2f vs %.2f",
                     env.variation, SIGNAL_ENVELOPE_RAYLEIGH);
            if (!out->carrier_found || signal_carrier_verdict(&carrier) == SIGNAL_NOTHING) {
                snprintf(out->verdict_summary, sizeof(out->verdict_summary),
                         "Pulsed / No standing carrier");
            }
        } else {
            snprintf(out->envelope_finding, sizeof(out->envelope_finding),
                     "envelope variation %.2f matches Rayleigh noise (%.2f)",
                     env.variation, SIGNAL_ENVELOPE_RAYLEIGH);
            if (!out->carrier_found) {
                snprintf(out->verdict_summary, sizeof(out->verdict_summary),
                         "Noise / Unsupported");
            }
        }
    } else {
        snprintf(out->envelope_finding, sizeof(out->envelope_finding),
                 "envelope too weak or unmeasurable");
    }

    /* 3. Technology-specific evidence (only when requested by technology_hint) */
    if (technology_hint && strcmp(technology_hint, "srd") == 0) {
        enum srd_modulation mod = srd_classify_modulation(i_samples, q_samples,
                                                          pair_count, sample_rate,
                                                          target_hz, full_scale);
        out->has_technology_evidence = 1;
        if (mod == SRD_MOD_FSK2) {
            snprintf(out->technology_finding, sizeof(out->technology_finding),
                     "SRD evidence: 2-FSK frequency shift keying");
        } else {
            snprintf(out->technology_finding, sizeof(out->technology_finding),
                     "SRD evidence: OOK on-off keying");
        }
    }

    return 0;
}
