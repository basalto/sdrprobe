/*
 * Decoder-context side of the 430-440 MHz ISM SRD module.
 *
 * Links -lm only (ADR-0001, ADR-0012).
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "srd_dsp.h"
#include "srd_frame.h"

/*
 * One chip alignment's worth of generic Manchester frames.
 *
 * A stretch is maximal: it starts at the first legal chip pair and ends at
 * the first violation, because a violation is the only frame boundary an
 * unknown transmitter offers. Everything the caller has to decide about a
 * stretch -- is it long enough, is it all preamble, how many whole bytes does
 * it carry -- is decided here, once, so both the phase trial and the emitting
 * pass reach the same verdict about the same chips.
 *
 * Returns the number of frames written, and sets *total_bits_out to the data
 * bits behind them. That total is what picks the phase: counting *stretches*
 * would prefer the alignment that shatters one frame into four.
 */
static size_t srd_generic_scan(const uint8_t *chips, const double *chip_time,
                               size_t n_chips, int phase,
                               enum srd_manchester_polarity polarity,
                               struct srd_frame *out, size_t max_frames,
                               size_t *total_bits_out) {
    size_t c = (size_t)phase;
    size_t frames = 0;

    *total_bits_out = 0;
    if (!chips || !out || max_frames == 0)
        return 0;

    while (c + 1 < n_chips && frames < max_frames) {
        uint8_t bits[1024];
        size_t bit_count = 0, start_chip, data_start, data_bits, n_bytes, b;
        size_t preamble = 0;
        uint8_t packed[32];
        size_t trivial = 0;

        while (c + 1 < n_chips && chips[c] == chips[c + 1])
            c++;
        if (c + 1 >= n_chips)
            break;

        start_chip = c;
        while (c + 1 < n_chips && bit_count < sizeof(bits)) {
            uint8_t c1 = chips[c], c2 = chips[c + 1];
            if (c1 == 1 && c2 == 0)
                bits[bit_count++] = (polarity == SRD_MANCHESTER_THOMAS) ? 0 : 1;
            else if (c1 == 0 && c2 == 1)
                bits[bit_count++] = (polarity == SRD_MANCHESTER_THOMAS) ? 1 : 0;
            else
                break;
            c += 2;
        }

        if (bit_count < 24)
            continue;

        /*
         * A preamble decodes to a run of one bit value, so a leading run of
         * eight or more is dropped -- but only if something follows it. The
         * whole-stretch case is a wakeup burst and is refused below rather
         * than reported as a frame with no payload.
         */
        while (preamble < bit_count && bits[preamble] == bits[0])
            preamble++;
        data_start = (preamble >= 8) ? preamble : 0;
        data_bits = bit_count - data_start;
        if (data_bits < 24)
            continue;

        n_bytes = data_bits / 8;
        if (n_bytes > sizeof(packed))
            n_bytes = sizeof(packed);
        srd_pack_bits(bits + data_start, n_bytes * 8, packed, n_bytes);

        /*
         * An unmodulated carrier and a pure preamble both pack to 0x00 or
         * 0xFF whatever the chip period was. One such byte in a frame is
         * ordinary payload; a frame that is nothing else carries no
         * information and is not reported as a decode.
         */
        for (b = 0; b < n_bytes; b++)
            if (packed[b] == 0x00 || packed[b] == 0xFF)
                trivial++;
        if (trivial >= n_bytes - 1)
            continue;

        memset(&out[frames], 0, sizeof(out[frames]));
        out[frames].kind = SRD_FRAME_GENERIC;
        out[frames].byte_count = n_bytes;
        out[frames].bit_count = n_bytes * 8;
        out[frames].error_count = 0;
        out[frames].run_index = start_chip;
        out[frames].time_seconds = chip_time ? chip_time[start_chip] : 0.0;
        memcpy(out[frames].bytes, packed, n_bytes);
        frames++;
        *total_bits_out += n_bytes * 8;
    }

    return frames;
}

const char *srd_device_type_name(enum srd_device_type type) {
    switch (type) {
    case SRD_DEVICE_REMOTE_CONTROL:
        return "SRD remote control";
    case SRD_DEVICE_REMOTE_FSK:
        return "SRD remote control (2-FSK)";
    case SRD_DEVICE_UNKNOWN:
    default:
        return "unknown";
    }
}

enum srd_device_type srd_device_type_of(enum srd_frame_kind kind,
                                        const uint8_t *bytes,
                                        size_t byte_count) {
    uint32_t prefix;

    /*
     * A full or repeat frame is the delimited OOK protocol by construction:
     * srd_extract_frames() only emits either after matching the header and
     * the trailer for the polarity it decoded with.
     */
    if (kind == SRD_FRAME_FULL || kind == SRD_FRAME_REPEAT)
        return SRD_DEVICE_REMOTE_CONTROL;

    if (kind != SRD_FRAME_GENERIC || !bytes || byte_count < 14)
        return SRD_DEVICE_UNKNOWN;

    prefix = ((uint32_t)bytes[0] << 16) | ((uint32_t)bytes[1] << 8) |
             (uint32_t)bytes[2];
    if (prefix == SRD_REMOTE_FSK_PREFIX_A || prefix == SRD_REMOTE_FSK_PREFIX_B)
        return SRD_DEVICE_REMOTE_FSK;

    return SRD_DEVICE_UNKNOWN;
}

size_t srd_extract_frames(const struct srd_run *runs, size_t run_count,
                          double chip_period, enum srd_manchester_polarity polarity,
                          struct srd_frame *frames_out, size_t max_frames) {
    size_t frames_count = 0;
    double current_time = 0.0;
    size_t i = 0;

    if (!runs || run_count < SRD_DELIMITER_RUNS || !(chip_period > 0.0) ||
        !frames_out || max_frames == 0)
        return 0;

    while (i + SRD_DELIMITER_RUNS <= run_count && frames_count < max_frames) {
        int is_del = 1;
        for (size_t d = 0; d < SRD_DELIMITER_RUNS; d++) {
            double us = runs[i + d].duration_seconds * 1e6;
            if (us < SRD_DELIMITER_MIN_US || us > SRD_DELIMITER_MAX_US) {
                is_del = 0;
                break;
            }
        }

        if (is_del) {
            size_t data_start = i + SRD_DELIMITER_RUNS;
            uint8_t chips[256];
            /* 10 bytes = 80 bits = 160 chips max */
            size_t n_chips = srd_runs_to_chips(runs + data_start,
                                               run_count - data_start,
                                               chip_period, chips, 160);

            if (n_chips >= 48) { /* at least 24 bits for a repeat frame */
                struct srd_manchester_decode dec;
                if (srd_manchester_decode(chips, n_chips, polarity, &dec) &&
                    dec.bit_count >= 24) {
                    struct srd_frame *f = &frames_out[frames_count];
                    memset(f, 0, sizeof(*f));
                    f->run_index = i;
                    f->time_seconds = current_time;
                    f->error_count = dec.error_count;

                    uint8_t raw_bytes[16];
                    size_t n_bytes = srd_pack_bits(dec.bits, dec.bit_count,
                                                  raw_bytes, sizeof(raw_bytes));

                    uint8_t header_full = (polarity == SRD_MANCHESTER_THOMAS)
                                              ? SRD_HEADER_FULL_THOMAS
                                              : SRD_HEADER_FULL_IEEE;
                    uint8_t header_repeat = (polarity == SRD_MANCHESTER_THOMAS)
                                                ? SRD_HEADER_REPEAT_THOMAS
                                                : SRD_HEADER_REPEAT_IEEE;
                    uint8_t trailer = (polarity == SRD_MANCHESTER_THOMAS)
                                          ? SRD_TRAILER_THOMAS
                                          : SRD_TRAILER_IEEE;

                    if (n_bytes >= SRD_FULL_FRAME_BYTES &&
                        raw_bytes[0] == header_full &&
                        raw_bytes[SRD_FULL_FRAME_BYTES - 1] == trailer) {
                        f->kind = SRD_FRAME_FULL;
                        f->byte_count = SRD_FULL_FRAME_BYTES;
                        f->bit_count = SRD_FULL_FRAME_BITS;
                        memcpy(f->bytes, raw_bytes, SRD_FULL_FRAME_BYTES);
                        frames_count++;
                    } else if (n_bytes >= SRD_REPEAT_FRAME_BYTES &&
                               raw_bytes[0] == header_repeat &&
                               raw_bytes[SRD_REPEAT_FRAME_BYTES - 1] == trailer) {
                        f->kind = SRD_FRAME_REPEAT;
                        f->byte_count = SRD_REPEAT_FRAME_BYTES;
                        f->bit_count = SRD_REPEAT_FRAME_BITS;
                        memcpy(f->bytes, raw_bytes, SRD_REPEAT_FRAME_BYTES);
                        frames_count++;
                    }
                }
            }

            for (size_t d = 0; d < SRD_DELIMITER_RUNS; d++)
                current_time += runs[i + d].duration_seconds;
            i += SRD_DELIMITER_RUNS;
        } else {
            current_time += runs[i].duration_seconds;
            i++;
        }
    }

    /*
     * 2. Generic Manchester frame extraction.
     *
     * For transmitters without the 6-run delimiter -- 2-FSK remotes, sensors,
     * other fobs -- there is nothing to synchronise on but the Manchester
     * code itself, so a frame is a maximal stretch of chips that violates it
     * nowhere, and the stream may hold several.
     *
     * **Every such stretch is emitted, not the longest one.** Keeping only
     * the best was measured to be the whole of the decode gap on
    * The 2-FSK protocol capture shows eight
     * of that capture's transmissions each carrying a 14-byte frame at a
     * 64.2 us chip, and the caller saw one. The session layer compounded it
     * -- srd_session_feed() emits an event only when the frame count *grows*,
     * and a path that can never return more than one frame pins that count at
     * one for the whole busy period.
     *
     * The phase is chosen once for the stream rather than per stretch: both
     * chip alignments are scanned, and the one that decodes more bits in
     * total wins. Emitting from both would report every frame twice, once
     * correctly and once off by a chip.
     */
    if (frames_count == 0 && run_count >= 8) {
        uint8_t chips[2048];
        double chip_time[2048];
        size_t n_chips = srd_runs_to_chips(runs, run_count, chip_period,
                                           chips, 2048);

        if (n_chips >= 48) {
            struct srd_frame scratch[SRD_MAX_FRAMES_PER_PRESS];
            size_t bits_phase[2] = { 0, 0 };
            size_t room = max_frames - frames_count;
            int phase, best_phase;

            if (room > SRD_MAX_FRAMES_PER_PRESS)
                room = SRD_MAX_FRAMES_PER_PRESS;

            /*
             * When each chip starts, mirroring srd_runs_to_chips()'s own
             * expansion. A generic frame used to report time 0.0 whatever it
             * was, which left the caller stamping it with the wall clock of
             * whichever block happened to notice -- so two frames 100 ms
             * apart in one buffer were indistinguishable in the log.
             */
            {
                size_t w = 0;
                double at = 0.0;
                for (size_t r = 0; r < run_count && w < n_chips; r++) {
                    double dur = runs[r].duration_seconds;
                    int n = (int)round(dur / chip_period);
                    if (n < 1)
                        n = 1;
                    for (int k = 0; k < n && w < n_chips; k++)
                        chip_time[w++] = at + (double)k * chip_period;
                    at += dur;
                }
                while (w < n_chips)
                    chip_time[w++] = at;
            }

            for (phase = 0; phase < 2; phase++)
                (void)srd_generic_scan(chips, chip_time, n_chips, phase,
                                       polarity, scratch, room,
                                       &bits_phase[phase]);

            best_phase = bits_phase[1] > bits_phase[0] ? 1 : 0;
            frames_count += srd_generic_scan(chips, chip_time, n_chips,
                                             best_phase, polarity,
                                             frames_out + frames_count, room,
                                             &bits_phase[best_phase]);
        }
    }

    return frames_count;
}
