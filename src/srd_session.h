#ifndef SRD_SESSION_H
#define SRD_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "srd_dsp.h"
#include "srd_frame.h"

/*
 * An SRD (Short Range Device) decode session, block by block, with no window
 * and no receiver.
 *
 * Owns cross-block run assembly, quiet/busy transitions, frame deduplication,
 * and decode counters.
 *
 * The window draws session state; the headless report prints it. Both are
 * adapters, and neither owns the decode machine.
 *
 * Links -lm only (ADR-0001, ADR-0012, ADR-0023).
 */

#define SRD_SESSION_FRAMES_MAX 16
#define SRD_SESSION_UNDECODED_MAX 16
#define SRD_SESSION_STREAM_RUNS_MAX 4096

/*
 * How many chips a 2-FSK burst has to yield before it is reported as a
 * wakeup preamble rather than simply an undecoded burst. Thirty-two is four
 * bytes of Manchester, which is the least that makes "preamble N chips"
 * worth printing; below it there is nothing to say about the burst except
 * that it happened.
 */
#define SRD_WAKEUP_MIN_CHIPS 32

/*
 * How far a transmission's carrier may move and still be the same
 * transmission across a block boundary.
 *
 * Whether two bursts are one signal is not answerable from the clock alone,
 * and a tolerance on *when* they start cannot do it -- measured by sweeping
 * one across all three captures, the two things it has to get right pull in
 * opposite directions and no value gives both:
 *
 *   tolerance   2-FSK wakeups (13 real)   OOK FULL frames (11 real)
 *   0.5-4 ms            12                         8-9
 *   8 ms                12                          8
 *   16 ms               11                          8
 *   33-50 ms            10                         11
 *
 * The OOK protocol needs the loose end because **on-off keying is silent between
 * frames**: the carrier is off, srd_find_transmissions() sees chunks that are
 * not busy, and a block can legitimately open several milliseconds after its
 * transmission resumes. The 2-FSK remote needs the tight end because its
 * presses are 100 ms apart in 65.5 ms blocks, so a loose rule chains one
 * press to the next.
 *
 * The carrier answers both. Its presses alternate between -118.8 kHz and
 * +401.2 kHz -- 520 kHz apart, and no gap tolerance can see that -- while the
 * its silences do not move the carrier at all. SRD_CHANNEL_HZ_DEFAULT is
 * the width this module already isolates a transmission to, so two bursts
 * inside one of those are plausibly one device and two outside it are not.
 */
#define SRD_CONTINUATION_CARRIER_HZ SRD_CHANNEL_HZ_DEFAULT

struct srd_session_frame_event {
    struct srd_frame frame;
    double carrier_hz;
    double chip_us;
    double at;
};

struct srd_session_undecoded_event {
    /*
     * What this burst is, decided once.
     *
     * SRD_FRAME_FSK_DETECTED is a wakeup -- a 2-FSK burst that yielded
     * enough chips for the preamble to be worth reporting -- and
     * SRD_FRAME_UNDECODED is a burst that could not be read at all,
     * including every burst whose chip period srd_chip_period() refused.
     *
     * It is here because the two adapters had reached different verdicts
     * about the same event: the window applied the chip threshold and said
     * UNDECODED, while the headless report called every 2-FSK burst a WAKEUP
     * unconditionally and printed "preamble 0 chips" for a burst with no
     * chip period at all. A verdict in two places is a verdict that can
     * disagree with itself.
     */
    enum srd_frame_kind kind;
    double carrier_hz;
    double chip_us;
    size_t chip_count;
    uint8_t raw_chips[512];
    double at;
    size_t offset_pairs;
    size_t pair_count;
    enum srd_modulation modulation;
};

struct srd_session_event {
    int quiet_reset;
    int frame_count;
    struct srd_session_frame_event frames[SRD_SESSION_FRAMES_MAX];
    int undecoded_count;
    struct srd_session_undecoded_event undecoded[SRD_SESSION_UNDECODED_MAX];
};

struct srd_session {
    /*
     * The run stream currently being decoded, and how many frames have
     * already been reported out of it.
     *
     * It holds **one transmission**, not a block's worth. A transmission can
     * straddle a block boundary and must be joined across it; two
     * transmissions in one block are two signals, separated by a gap, often
     * on different carriers, and gluing them cost this decoder most of its
     * frames -- srd_extract_frames() reads a run stream as one continuous
     * Manchester signal, so a junction between two of them is a violation
     * that truncates whichever frame was in progress.
     */
    struct srd_run stream_runs[SRD_SESSION_STREAM_RUNS_MAX];
    size_t stream_run_count;
    size_t carried_frame_count;

    /*
     * Whether this stream has already been reported as an undecoded burst.
     *
     * A transmission is reported once, not once per sample block. A 1.2 s
     * signal measured live at 434.35 MHz spans about eighteen blocks and said
     * "detected burst (no frame)" in every one of them -- 91 lines over six
     * seconds, for what the receiver had grouped into 18 transmissions. The
     * count carried the block rate rather than anything about the air.
     *
     * It is cleared wherever `carried_frame_count` is, and for the same
     * reason: both are facts about the stream in hand, and a stream that has
     * been forgotten has not been reported.
     */
    int carried_undecoded_reported;

    /* The carrier of the stream being carried, so a continuation can be
       told from the next transmitter to speak. */
    double carried_carrier_hz;
    int quiet_blocks;
    int transmissions_found;
    int frames_decoded;
    double last_busy_time;

    /* Latched telemetry for analysis charts and cursor readouts */
    double last_carrier_hz;
    double last_chip_us;
    double last_over_floor_db;
    float last_envelope[1024];
    int last_envelope_count;
    uint8_t last_chips[512];
    int last_chips_count;
};

void srd_session_reset(struct srd_session *s);

int srd_session_feed(struct srd_session *s,
                     const float *i_samples, const float *q_samples,
                     size_t pair_count, double sample_rate,
                     float full_scale, enum srd_manchester_polarity polarity,
                     double now, struct srd_session_event *out);

#endif /* SRD_SESSION_H */
