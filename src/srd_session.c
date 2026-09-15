#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "srd_dsp.h"
#include "srd_frame.h"
#include "srd_session.h"

void srd_session_reset(struct srd_session *s) {
    if (!s)
        return;
    memset(s, 0, sizeof(*s));
}

int srd_session_feed(struct srd_session *s,
                     const float *i_samples, const float *q_samples,
                     size_t pair_count, double sample_rate,
                     float full_scale, enum srd_manchester_polarity polarity,
                     double now, struct srd_session_event *out) {
    struct srd_transmission txs[SRD_MAX_TRANSMISSIONS];

    if (out)
        memset(out, 0, sizeof(*out));

    if (!s || !i_samples || !q_samples || pair_count == 0 || !(sample_rate > 0.0))
        return 0;

    int count = srd_find_transmissions(i_samples, q_samples, pair_count,
                                       sample_rate, full_scale,
                                       SRD_BUSY_BAR_DB_DEFAULT,
                                       SRD_GAP_SECONDS_DEFAULT,
                                       txs, SRD_MAX_TRANSMISSIONS);
    if (count <= 0) {
        s->quiet_blocks++;
        if (s->quiet_blocks >= 4) {
            s->stream_run_count = 0;
            s->carried_frame_count = 0;
            s->carried_undecoded_reported = 0;
            if (out)
                out->quiet_reset = 1;
        }
        return 0;
    }

    if (s->quiet_blocks >= 4 || s->transmissions_found == 0) {
        s->stream_run_count = 0;
        s->carried_frame_count = 0;
        s->carried_undecoded_reported = 0;
        s->transmissions_found++;
    }
    s->quiet_blocks = 0;
    s->last_busy_time = now;

    /*
     * Whether the carried tail is the same transmission as this block's
     * first one. Two things have to hold: it began at the start of the block
     * rather than after a gap, and it is on the same carrier. The second is
     * what the clock alone cannot tell -- see
     * SRD_CONTINUATION_CARRIER_HZ.
     */
    if (s->stream_run_count > 0 &&
        ((double)txs[0].offset_pairs / sample_rate > SRD_GAP_SECONDS_DEFAULT ||
         fabs(txs[0].carrier_hz - s->carried_carrier_hz) >
             SRD_CONTINUATION_CARRIER_HZ)) {
        s->stream_run_count = 0;
        s->carried_frame_count = 0;
        s->carried_undecoded_reported = 0;
    }

    for (int t = 0; t < count; t++) {
        size_t sig_cap = txs[t].pair_count / 10 + 2000;
        float *sig = malloc(sig_cap * sizeof(float));
        struct srd_run runs[2048];
        double work_rate = 0.0;

        if (!sig)
            continue;

        size_t sig_n;
        double thresh;
        if (txs[t].modulation == SRD_MOD_FSK2) {
            sig_n = srd_demodulate_fsk(i_samples + txs[t].offset_pairs,
                                       q_samples + txs[t].offset_pairs,
                                       txs[t].pair_count, sample_rate,
                                       txs[t].carrier_hz, sig, sig_cap,
                                       &work_rate);
            thresh = srd_discriminator_threshold(sig, sig_n);
        } else {
            sig_n = srd_demodulate_envelope(i_samples + txs[t].offset_pairs,
                                            q_samples + txs[t].offset_pairs,
                                            txs[t].pair_count, sample_rate,
                                            txs[t].carrier_hz, sig, sig_cap,
                                            &work_rate);
            thresh = srd_envelope_threshold(sig, sig_n);
        }

        size_t r_n = srd_extract_runs(sig, sig_n, work_rate, thresh, runs, 2048);

        /*
         * Each transmission is decoded on its own runs. Only the first can
         * be a continuation of the carried tail; the rest start a stream.
         */
        if (t > 0) {
            s->stream_run_count = 0;
            s->carried_frame_count = 0;
            s->carried_undecoded_reported = 0;
        }
        s->carried_carrier_hz = txs[t].carrier_hz;

        if (r_n > 0) {
            /* Join runs across a block boundary, where one run was cut in two */
            size_t start_r = 0;
            if (s->stream_run_count > 0 &&
                s->stream_runs[s->stream_run_count - 1].state == runs[0].state) {
                s->stream_runs[s->stream_run_count - 1].duration_seconds += runs[0].duration_seconds;
                s->stream_runs[s->stream_run_count - 1].length_samples += runs[0].length_samples;
                start_r = 1;
            }

            for (size_t k = start_r;
                 k < r_n && s->stream_run_count < SRD_SESSION_STREAM_RUNS_MAX;
                 k++) {
                s->stream_runs[s->stream_run_count++] = runs[k];
            }

            double chip_s = srd_chip_period(s->stream_runs, s->stream_run_count);
            size_t n_frames = 0;
            uint8_t raw_chips[512];
            size_t n_chips = 0;

            if (chip_s > 0.0) {
                struct srd_frame frames[16];
                n_frames = srd_extract_frames(s->stream_runs, s->stream_run_count,
                                              chip_s, polarity, frames, 16);

                if (n_frames > s->carried_frame_count) {
                    for (size_t f = s->carried_frame_count;
                         f < n_frames; f++) {
                        frames[f].modulation = txs[t].modulation;
                        s->frames_decoded++;

                        if (out && out->frame_count < SRD_SESSION_FRAMES_MAX) {
                            struct srd_session_frame_event *fe =
                                &out->frames[out->frame_count++];
                            fe->frame = frames[f];
                            fe->carrier_hz = txs[t].carrier_hz;
                            fe->chip_us = chip_s * 1e6;
                            fe->at = now;
                        }
                    }
                    s->carried_frame_count = n_frames;
                }

                /* Latch diagnostic and analysis data */
                s->last_carrier_hz = txs[t].carrier_hz;
                s->last_chip_us = chip_s * 1e6;
                s->last_over_floor_db = txs[t].carrier_over_noise_db;

                int to_copy = (int)sig_n;
                if (to_copy > (int)(sizeof(s->last_envelope) / sizeof(s->last_envelope[0])))
                    to_copy = (int)(sizeof(s->last_envelope) / sizeof(s->last_envelope[0]));
                memcpy(s->last_envelope, sig, (size_t)to_copy * sizeof(float));
                s->last_envelope_count = to_copy;

                n_chips = srd_runs_to_chips(runs, r_n, chip_s, raw_chips, 512);
                to_copy = (int)n_chips;
                if (to_copy > (int)(sizeof(s->last_chips) / sizeof(s->last_chips[0])))
                    to_copy = (int)(sizeof(s->last_chips) / sizeof(s->last_chips[0]));
                memcpy(s->last_chips, raw_chips, (size_t)to_copy);
                s->last_chips_count = to_copy;
            }

            if (n_frames == 0) {
                if (txs[t].modulation == SRD_MOD_FSK2) {
                    s->last_carrier_hz = txs[t].carrier_hz;
                    s->last_over_floor_db = txs[t].carrier_over_noise_db;
                }

                /*
                 * Once per transmission. A signal that outlasts a sample
                 * block arrives again in the next one as a continuation of
                 * the same run stream, and saying "detected burst (no frame)"
                 * about it every 65.5 ms reports the block rate rather than
                 * the air. The latched telemetry above is deliberately
                 * outside this: the charts want the newest block's numbers
                 * whether or not the log has anything new to say.
                 */
                if (out && !s->carried_undecoded_reported &&
                    out->undecoded_count < SRD_SESSION_UNDECODED_MAX) {
                    struct srd_session_undecoded_event *ue =
                        &out->undecoded[out->undecoded_count++];
                    ue->kind = (txs[t].modulation == SRD_MOD_FSK2 &&
                                n_chips >= SRD_WAKEUP_MIN_CHIPS)
                                   ? SRD_FRAME_FSK_DETECTED
                                   : SRD_FRAME_UNDECODED;
                    ue->carrier_hz = txs[t].carrier_hz;
                    ue->chip_us = chip_s * 1e6;
                    ue->chip_count = n_chips;
                    if (n_chips > 0)
                        memcpy(ue->raw_chips, raw_chips,
                               n_chips > 512 ? 512 : n_chips);
                    ue->at = now;
                    ue->offset_pairs = txs[t].offset_pairs;
                    ue->pair_count = txs[t].pair_count;
                    ue->modulation = txs[t].modulation;
                    s->carried_undecoded_reported = 1;
                }
            }
        }

        free(sig);
    }

    /*
     * Keep the tail only when the last transmission was still going when the
     * samples ran out. Otherwise it finished inside this block, and carrying
     * it would glue it to whatever speaks next and report the two as one.
     */
    if (count > 0) {
        const struct srd_transmission *last = &txs[count - 1];
        size_t end_pairs = last->offset_pairs + last->pair_count;
        double tail = end_pairs < pair_count
                          ? (double)(pair_count - end_pairs) / sample_rate
                          : 0.0;

        if (tail > SRD_GAP_SECONDS_DEFAULT) {
            s->stream_run_count = 0;
            s->carried_frame_count = 0;
            s->carried_undecoded_reported = 0;
        }
    }

    return count;
}
