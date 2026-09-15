#ifndef SIGNAL_ANALYSIS_H
#define SIGNAL_ANALYSIS_H

#include <stddef.h>
#include <stdint.h>

/*
 * Retrospective signal analysis of an extracted I/Q slice.
 *
 * Composes signal_probe and signal_findings measurements to produce immutable
 * numeric evidence and evidence-backed findings.
 *
 * Links -lm only (ADR-0001, ADR-0012). No raylib, no struct app, no acquisition.
 */

struct signal_analysis_result {
    int valid;
    double sample_rate;
    double duration_seconds;
    double target_hz;

    /* Carrier measurement */
    int carrier_found;
    double carrier_offset_hz;
    double carrier_over_noise_db;
    double carrier_power_fraction;
    char carrier_finding[96];
    char standing_sentence[96];

    /* Envelope measurement */
    int envelope_valid;
    double envelope_variation;
    double peak_over_mean_db;
    char envelope_finding[96];

    /* Overall verdict summary */
    char verdict_summary[64];

    /* Technology-specific evidence (if requested via hint) */
    int has_technology_evidence;
    char technology_finding[96];
};

/*
 * Run retrospective signal analysis over an I/Q sample slice.
 *
 * Returns 0 on success, -1 on invalid inputs.
 */
int signal_analysis_run(const float *i_samples, const float *q_samples,
                        size_t pair_count, double sample_rate,
                        double target_hz, float full_scale,
                        const char *technology_hint,
                        struct signal_analysis_result *out);

#endif /* SIGNAL_ANALYSIS_H */
