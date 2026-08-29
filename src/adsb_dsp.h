#ifndef ADSB_DSP_H
#define ADSB_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * ADS-B / Mode S technology plugin (the Decoder context).
 *
 * Unlike the calibration-grade plugins described in sdr_dsp.h/gsm_dsp.h, this
 * plugin decodes transmitted messages. Mode S demodulation is magnitude-domain
 * preamble correlation plus pulse-position bit-slicing, so it reuses none of the
 * generic FFT / centroid / channel-power primitives; it consumes only the
 * per-pair magnitude that sdr_dsp_convert_iq already produces. See
 * docs/adr/0009-mode-s-decode-plugin.md.
 *
 * Signal conventions match the app defaults: 1090 MHz, 2 MS/s, so one Mode S
 * bit spans ADSB_SAMPLES_PER_BIT (2) magnitude samples.
 */

#define ADSB_SAMPLES_PER_BIT 2
#define ADSB_PREAMBLE_SAMPLES 16
#define ADSB_LONG_BITS 112
#define ADSB_SHORT_BITS 56
#define ADSB_LONG_BYTES 14
#define ADSB_SHORT_BYTES 7

enum adsb_message_kind {
    ADSB_KIND_UNKNOWN,
    ADSB_KIND_IDENTIFICATION,   /* callsign */
    ADSB_KIND_AIRBORNE_POSITION,
    ADSB_KIND_VELOCITY
};

struct adsb_message {
    uint8_t  bytes[ADSB_LONG_BYTES];
    int      byte_count;         /* ADSB_SHORT_BYTES or ADSB_LONG_BYTES */
    int      downlink_format;    /* DF, 0-31 */
    uint32_t icao;               /* 24-bit ICAO address */
    int      type_code;          /* ADS-B TC for DF17/18, else -1 */
    enum adsb_message_kind kind;

    char     callsign[9];        /* NUL-terminated when kind==IDENTIFICATION */

    int      has_altitude;       /* barometric */
    int      altitude_ft;

    int      has_velocity;
    double   ground_speed_kt;
    double   heading_deg;        /* 0-360, clockwise from north */

    /* Raw Compact Position Reporting fields (kind==AIRBORNE_POSITION). */
    int      cpr_odd;            /* 0 even frame, 1 odd frame */
    uint32_t cpr_lat;            /* 17-bit encoded latitude */
    uint32_t cpr_lon;            /* 17-bit encoded longitude */

    /* Filled once an even/odd pair yields a global position. */
    int      has_position;
    double   latitude_deg;
    double   longitude_deg;
};

/* Mode S CRC-24 remainder over byte_count bytes (data + parity). For an
   uncorrupted DF17/18 extended squitter the remainder is zero. */
uint32_t adsb_crc(const uint8_t *bytes, int byte_count);

/* Frame length implied by a downlink format: ADSB_LONG_BITS or
   ADSB_SHORT_BITS. */
int adsb_frame_length_bits(int downlink_format);

/* Parse one raw Mode S frame. Returns 1 with *out filled when the frame is a
   DF17/18 extended squitter whose CRC is valid; 0 otherwise (frame dropped).
   Airborne-position frames leave has_position 0: a global fix needs an even/odd
   pair, resolved by the decoder below. */
int adsb_decode_frame(const uint8_t *bytes, int byte_count,
                      struct adsb_message *out);

/* Globally decode a latitude/longitude from a raw even/odd CPR pair. use_odd
   selects which frame is the position reference (the more recent one). Returns
   1 on success, 0 when the latitude-zone check fails. */
int adsb_cpr_global(uint32_t even_lat, uint32_t even_lon,
                    uint32_t odd_lat, uint32_t odd_lon,
                    int use_odd, double *latitude_deg, double *longitude_deg);

/* Per-ICAO even/odd position pairing cache: the minimal decode state kept only
   to resolve CPR positions. Not a tracked-aircraft table (see ADR-0009). */
#define ADSB_CPR_CACHE 64
#define ADSB_CPR_MAX_PAIR_SECONDS 10.0

struct adsb_cpr_entry {
    uint32_t icao;
    int      has_even, has_odd;
    uint32_t even_lat, even_lon;
    uint32_t odd_lat, odd_lon;
    double   even_time, odd_time;
};

struct adsb_decoder {
    struct adsb_cpr_entry cache[ADSB_CPR_CACHE];
};

void adsb_decoder_init(struct adsb_decoder *dec);

/* Scan a magnitude buffer for Mode S frames, decode the DF17/18 ones, resolve
   positions via the decoder's pairing cache, and write decoded messages into
   out (up to out_capacity). time_seconds stamps every frame found in this
   buffer, used only for even/odd pairing. Returns the number written. */
size_t adsb_demod(struct adsb_decoder *dec, const float *magnitudes,
                  size_t pair_count, double time_seconds,
                  struct adsb_message *out, size_t out_capacity);

#endif
