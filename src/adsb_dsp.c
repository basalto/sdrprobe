#include "adsb_dsp.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MODES_GENERATOR_POLY 0xFFF409U
#define CPR_SCALE 131072.0 /* 2^17 */

/* Read len bits (MSB-first) starting at absolute bit index start of bytes. */
static uint32_t bits_val(const uint8_t *bytes, int start, int len) {
    uint32_t value = 0;
    for (int i = 0; i < len; i++) {
        int bit = start + i;
        int byte = bit >> 3;
        int offset = 7 - (bit & 7);
        value = (value << 1) | ((bytes[byte] >> offset) & 1u);
    }
    return value;
}

uint32_t adsb_crc(const uint8_t *bytes, int byte_count) {
    uint32_t remainder = 0;
    for (int i = 0; i < byte_count; i++) {
        remainder ^= (uint32_t)bytes[i] << 16;
        for (int b = 0; b < 8; b++) {
            if (remainder & 0x800000u)
                remainder = (remainder << 1) ^ MODES_GENERATOR_POLY;
            else
                remainder <<= 1;
            remainder &= 0xFFFFFFu;
        }
    }
    return remainder;
}

int adsb_frame_length_bits(int downlink_format) {
    switch (downlink_format) {
    case 16: case 17: case 18: case 19: case 20: case 21: case 24:
        return ADSB_LONG_BITS;
    default:
        return ADSB_SHORT_BITS;
    }
}

static void decode_identification(const uint8_t *bytes, struct adsb_message *out) {
    static const char charset[65] =
        "#ABCDEFGHIJKLMNOPQRSTUVWXYZ##### ###############0123456789######";
    char text[9];
    for (int i = 0; i < 8; i++) {
        uint32_t code = bits_val(bytes, 40 + 6 * i, 6);
        text[i] = charset[code];
    }
    text[8] = '\0';
    /* Trim trailing spaces. */
    int end = 8;
    while (end > 0 && text[end - 1] == ' ')
        text[--end] = '\0';
    memcpy(out->callsign, text, sizeof(out->callsign));
    out->kind = ADSB_KIND_IDENTIFICATION;
}

static void decode_airborne_position(const uint8_t *bytes,
                                     struct adsb_message *out) {
    uint32_t alt12 = bits_val(bytes, 40, 12);
    int q_bit = (alt12 >> 4) & 1u;
    if (q_bit) {
        uint32_t n = ((alt12 >> 5) & 0x7Fu) << 4 | (alt12 & 0x0Fu);
        out->has_altitude = 1;
        out->altitude_ft = (int)n * 25 - 1000;
    }
    out->cpr_odd = (int)bits_val(bytes, 53, 1);
    out->cpr_lat = bits_val(bytes, 54, 17);
    out->cpr_lon = bits_val(bytes, 71, 17);
    out->kind = ADSB_KIND_AIRBORNE_POSITION;
}

static void decode_velocity(const uint8_t *bytes, struct adsb_message *out) {
    int subtype = (int)bits_val(bytes, 37, 3);
    if (subtype != 1 && subtype != 2)
        return; /* airspeed subtypes not decoded in this cut */
    int sign_ew = (int)bits_val(bytes, 45, 1);
    int vew = (int)bits_val(bytes, 46, 10);
    int sign_ns = (int)bits_val(bytes, 56, 1);
    int vns = (int)bits_val(bytes, 57, 10);
    if (vew == 0 || vns == 0)
        return; /* velocity unavailable */
    int mult = (subtype == 2) ? 4 : 1;
    double vx = (double)((vew - 1) * mult);
    double vy = (double)((vns - 1) * mult);
    if (sign_ew)
        vx = -vx;
    if (sign_ns)
        vy = -vy;
    out->has_velocity = 1;
    out->ground_speed_kt = sqrt(vx * vx + vy * vy);
    double heading = atan2(vx, vy) * 180.0 / M_PI;
    if (heading < 0.0)
        heading += 360.0;
    out->heading_deg = heading;
    out->kind = ADSB_KIND_VELOCITY;
}

int adsb_decode_frame(const uint8_t *bytes, int byte_count,
                      struct adsb_message *out) {
    if (byte_count != ADSB_SHORT_BYTES && byte_count != ADSB_LONG_BYTES)
        return 0;
    int df = (bytes[0] >> 3) & 0x1F;
    if (adsb_frame_length_bits(df) / 8 != byte_count)
        return 0;
    /* This cut decodes only ADS-B extended squitters with a clean CRC. */
    if (df != 17 && df != 18)
        return 0;
    if (adsb_crc(bytes, byte_count) != 0)
        return 0;

    memset(out, 0, sizeof(*out));
    memcpy(out->bytes, bytes, (size_t)byte_count);
    out->byte_count = byte_count;
    out->downlink_format = df;
    out->icao = ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
    int tc = (int)bits_val(bytes, 32, 5);
    out->type_code = tc;
    out->kind = ADSB_KIND_UNKNOWN;

    if (tc >= 1 && tc <= 4)
        decode_identification(bytes, out);
    else if (tc >= 9 && tc <= 18)
        decode_airborne_position(bytes, out);
    else if (tc == 19)
        decode_velocity(bytes, out);
    return 1;
}

static double positive_fmod(double a, double b) {
    double r = fmod(a, b);
    if (r < 0.0)
        r += b;
    return r;
}

static int cpr_nl(double lat) {
    if (lat < 0.0)
        lat = -lat;
    if (lat < 1e-9)
        return 59;
    if (fabs(lat - 87.0) < 1e-9)
        return 2;
    if (lat > 87.0)
        return 1;
    double numerator = 1.0 - cos(M_PI / (2.0 * 15.0));
    double denominator = cos(M_PI / 180.0 * lat);
    denominator *= denominator;
    double nl = 2.0 * M_PI / acos(1.0 - numerator / denominator);
    return (int)floor(nl);
}

int adsb_cpr_global(uint32_t even_lat, uint32_t even_lon,
                    uint32_t odd_lat, uint32_t odd_lon,
                    int use_odd, double *latitude_deg, double *longitude_deg) {
    const double dlat_even = 360.0 / 60.0;
    const double dlat_odd = 360.0 / 59.0;
    double lat_cpr_even = even_lat / CPR_SCALE;
    double lat_cpr_odd = odd_lat / CPR_SCALE;
    double lon_cpr_even = even_lon / CPR_SCALE;
    double lon_cpr_odd = odd_lon / CPR_SCALE;

    double j = floor(59.0 * lat_cpr_even - 60.0 * lat_cpr_odd + 0.5);
    double lat_e = dlat_even * (positive_fmod(j, 60.0) + lat_cpr_even);
    double lat_o = dlat_odd * (positive_fmod(j, 59.0) + lat_cpr_odd);
    if (lat_e >= 270.0)
        lat_e -= 360.0;
    if (lat_o >= 270.0)
        lat_o -= 360.0;
    if (cpr_nl(lat_e) != cpr_nl(lat_o))
        return 0;

    double lat = use_odd ? lat_o : lat_e;
    int nl = cpr_nl(lat);
    double m = floor(lon_cpr_even * (nl - 1) - lon_cpr_odd * nl + 0.5);
    double lon;
    if (use_odd) {
        int ni = (nl - 1) > 1 ? (nl - 1) : 1;
        lon = (360.0 / ni) * (positive_fmod(m, ni) + lon_cpr_odd);
    } else {
        int ni = nl > 1 ? nl : 1;
        lon = (360.0 / ni) * (positive_fmod(m, ni) + lon_cpr_even);
    }
    if (lon >= 180.0)
        lon -= 360.0;

    *latitude_deg = lat;
    *longitude_deg = lon;
    return 1;
}

void adsb_decoder_init(struct adsb_decoder *dec) {
    memset(dec, 0, sizeof(*dec));
}

static struct adsb_cpr_entry *cache_entry(struct adsb_decoder *dec,
                                          uint32_t icao) {
    struct adsb_cpr_entry *oldest = &dec->cache[0];
    double oldest_time = INFINITY;
    for (int i = 0; i < ADSB_CPR_CACHE; i++) {
        struct adsb_cpr_entry *e = &dec->cache[i];
        if ((e->has_even || e->has_odd) && e->icao == icao)
            return e;
        double recent = e->even_time > e->odd_time ? e->even_time : e->odd_time;
        if (!e->has_even && !e->has_odd) {
            oldest = e;
            oldest_time = -INFINITY;
        } else if (recent < oldest_time) {
            oldest = e;
            oldest_time = recent;
        }
    }
    memset(oldest, 0, sizeof(*oldest));
    oldest->icao = icao;
    return oldest;
}

/* Resolve a global position for a freshly decoded airborne-position frame,
   pairing it against the opposite parity in the cache. */
static void resolve_position(struct adsb_decoder *dec, double time_seconds,
                             struct adsb_message *msg) {
    struct adsb_cpr_entry *e = cache_entry(dec, msg->icao);
    if (msg->cpr_odd) {
        e->has_odd = 1;
        e->odd_lat = msg->cpr_lat;
        e->odd_lon = msg->cpr_lon;
        e->odd_time = time_seconds;
    } else {
        e->has_even = 1;
        e->even_lat = msg->cpr_lat;
        e->even_lon = msg->cpr_lon;
        e->even_time = time_seconds;
    }
    if (!e->has_even || !e->has_odd)
        return;
    if (fabs(e->even_time - e->odd_time) > ADSB_CPR_MAX_PAIR_SECONDS)
        return;
    double lat, lon;
    if (adsb_cpr_global(e->even_lat, e->even_lon, e->odd_lat, e->odd_lon,
                        msg->cpr_odd, &lat, &lon)) {
        msg->has_position = 1;
        msg->latitude_deg = lat;
        msg->longitude_deg = lon;
    }
}

/* dump1090-style magnitude preamble check at sample index i. */
static int preamble_at(const float *m, size_t i, size_t pair_count) {
    if (i + ADSB_PREAMBLE_SAMPLES >= pair_count)
        return 0;
    if (!(m[i] > m[i + 1] && m[i + 1] < m[i + 2] && m[i + 2] > m[i + 3] &&
          m[i + 3] < m[i] && m[i + 4] < m[i] && m[i + 5] < m[i] &&
          m[i + 6] < m[i] && m[i + 7] > m[i + 8] && m[i + 8] < m[i + 9] &&
          m[i + 9] > m[i + 6]))
        return 0;
    /* The four preamble pulses must clear the quiet samples that follow. */
    float high = (m[i] + m[i + 2] + m[i + 7] + m[i + 9]) / 4.0f;
    for (size_t k = 10; k < ADSB_PREAMBLE_SAMPLES; k++)
        if (m[i + k] >= high * 0.75f)
            return 0;
    return 1;
}

size_t adsb_demod(struct adsb_decoder *dec, const float *magnitudes,
                  size_t pair_count, double time_seconds,
                  struct adsb_message *out, size_t out_capacity) {
    size_t written = 0;
    size_t i = 0;
    while (i + ADSB_PREAMBLE_SAMPLES + ADSB_LONG_BITS * ADSB_SAMPLES_PER_BIT
           <= pair_count) {
        if (!preamble_at(magnitudes, i, pair_count)) {
            i++;
            continue;
        }
        const float *data = magnitudes + i + ADSB_PREAMBLE_SAMPLES;
        uint8_t bytes[ADSB_LONG_BYTES];
        memset(bytes, 0, sizeof(bytes));
        /* Demodulate a full long frame's worth of bits; the DF fixes length. */
        for (int bit = 0; bit < ADSB_LONG_BITS; bit++) {
            float first = data[bit * ADSB_SAMPLES_PER_BIT];
            float second = data[bit * ADSB_SAMPLES_PER_BIT + 1];
            if (first > second)
                bytes[bit >> 3] |= (uint8_t)(1u << (7 - (bit & 7)));
        }
        int df = (bytes[0] >> 3) & 0x1F;
        int byte_count = adsb_frame_length_bits(df) / 8;

        struct adsb_message msg;
        if (adsb_decode_frame(bytes, byte_count, &msg)) {
            if (msg.kind == ADSB_KIND_AIRBORNE_POSITION)
                resolve_position(dec, time_seconds, &msg);
            if (written < out_capacity)
                out[written] = msg;
            written++;
            i += ADSB_PREAMBLE_SAMPLES +
                 (size_t)byte_count * 8 * ADSB_SAMPLES_PER_BIT;
            continue;
        }
        i++;
    }
    return written;
}
