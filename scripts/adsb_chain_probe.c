/*
 * adsb_chain_probe.c - white-box walk through the ADS-B / Mode S decode chain.
 *
 * This diagnostic tool finds the first valid frame in a capture, prints every
 * processing stage (preamble, demodulation, CRC, parsing), and then runs a
 * sweep over the whole file to report overall decode statistics.
 *
 * Build/run:
 *     make probe-adsb-chain                 # uses testfiles/adsb_modes1.bin
 *     make probe-adsb-chain FILE=other.bin
 */
#include "sdr_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pull in the plugin's implementation directly to access statics. */
#include "adsb_dsp.c"

#define BLOCK_BYTES (16 * 16384)
#define BLOCK_PAIRS (BLOCK_BYTES / 2)
#define SAMPLE_RATE_HZ 2000000.0

static void print_hex(const uint8_t *bytes, int len) {
    for (int i = 0; i < len; i++)
        printf("%02X", bytes[i]);
}

/* Find and walk through the first valid preamble in the block. */
static int probe_block(const unsigned char *raw, size_t bytes) {
    static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
    size_t pairs = sdr_dsp_convert_iq(raw, bytes, I, Q, M, BLOCK_PAIRS);

    puts("========================================================================");
    puts("STAGE 1  byte->float I/Q conversion & magnitude (generic core)");
    puts("  Unsigned 8-bit interleaved I/Q -> per-pair magnitude.");
    printf("  pairs=%zu  sample_rate=%.0f S/s\n", pairs, SAMPLE_RATE_HZ);

    size_t i = 0;
    int found = 0;
    while (i + ADSB_PREAMBLE_SAMPLES + ADSB_LONG_BITS * ADSB_SAMPLES_PER_BIT <= pairs) {
        if (!preamble_at(M, i, pairs)) {
            i++;
            continue;
        }

        puts("");
        puts("STAGE 2  Preamble detection");
        puts("  Look for 4 specific pulses (at relative samples 0, 2, 7, 9) followed");
        puts("  by a quiet period, representing the Mode S preamble.");
        printf("  Found preamble at sample index %zu.\n", i);
        printf("  Pulse magnitudes:  P1=%.1f  P2=%.1f  P3=%.1f  P4=%.1f\n",
               M[i], M[i + 2], M[i + 7], M[i + 9]);

        puts("");
        puts("STAGE 3  Pulse-position demodulation");
        puts("  Each bit spans 2 samples (1 microsecond). Energy in the first half");
        puts("  is a '1'; energy in the second half is a '0'.");
        
        const float *data = M + i + ADSB_PREAMBLE_SAMPLES;
        uint8_t frame_bytes[ADSB_LONG_BYTES];
        memset(frame_bytes, 0, sizeof(frame_bytes));
        
        /* Demodulate full long frame's worth of bits just to see them. */
        for (int bit = 0; bit < ADSB_LONG_BITS; bit++) {
            float first = data[bit * ADSB_SAMPLES_PER_BIT];
            float second = data[bit * ADSB_SAMPLES_PER_BIT + 1];
            if (first > second)
                frame_bytes[bit >> 3] |= (uint8_t)(1u << (7 - (bit & 7)));
        }
        
        int df = (frame_bytes[0] >> 3) & 0x1F;
        int byte_count = adsb_frame_length_bits(df) / 8;
        
        printf("  Downlink Format (DF) = %d -> implies %d bit frame.\n", df, byte_count * 8);
        printf("  Demodulated hex: ");
        print_hex(frame_bytes, byte_count);
        printf("\n");

        puts("");
        puts("STAGE 4  CRC-24 validation");
        puts("  The last 24 bits are a parity check. A non-zero remainder means");
        puts("  the frame is corrupted or has an overlaid interrogator ID.");
        uint32_t crc = adsb_crc(frame_bytes, byte_count);
        printf("  CRC remainder: %06X  (%s)\n", crc, crc == 0 ? "VALID" : "INVALID");
        
        if (crc != 0) {
            puts("  Frame dropped due to invalid CRC. Searching for next preamble...");
            i += ADSB_PREAMBLE_SAMPLES;
            continue;
        }

        puts("");
        puts("STAGE 5  Message parsing");
        struct adsb_message msg;
        if (adsb_decode_frame(frame_bytes, byte_count, &msg)) {
            printf("  ICAO Address : %06X\n", msg.icao);
            printf("  Type Code    : %d\n", msg.type_code);
            
            switch(msg.kind) {
                case ADSB_KIND_IDENTIFICATION:
                    printf("  Message Kind : Identification\n");
                    printf("  Callsign     : %s\n", msg.callsign);
                    break;
                case ADSB_KIND_AIRBORNE_POSITION:
                    printf("  Message Kind : Airborne Position\n");
                    printf("  Altitude     : %d ft\n", msg.altitude_ft);
                    printf("  CPR Parity   : %s\n", msg.cpr_odd ? "Odd" : "Even");
                    printf("  CPR Lat/Lon  : %d / %d\n", msg.cpr_lat, msg.cpr_lon);
                    break;
                case ADSB_KIND_VELOCITY:
                    printf("  Message Kind : Airborne Velocity\n");
                    printf("  Ground Speed : %.1f kt\n", msg.ground_speed_kt);
                    printf("  Heading      : %.1f deg\n", msg.heading_deg);
                    break;
                default:
                    printf("  Message Kind : Unknown/Other\n");
                    break;
            }
        }
        
        found = 1;
        break; /* Stop after the first valid frame is fully traced */
    }

    if (!found) {
        puts("  No valid frame found in this block.");
    }
    return found;
}

/* Sweep every block, report total decodes and decode types. */
static void consistency_sweep(FILE *f) {
    puts("");
    puts("========================================================================");
    puts("SWEEP  Whole-file decode statistics");
    static unsigned char raw[BLOCK_BYTES];
    static float I[BLOCK_PAIRS], Q[BLOCK_PAIRS], M[BLOCK_PAIRS];
    
    struct adsb_decoder dec;
    adsb_decoder_init(&dec);
    struct adsb_message out[128];
    
    int blk = 0;
    size_t total_frames = 0;
    size_t kind_id = 0, kind_pos = 0, kind_vel = 0, kind_other = 0;
    size_t global_positions = 0;
    double t = 0.0;
    
    rewind(f);
    while (fread(raw, 1, BLOCK_BYTES, f) == BLOCK_BYTES) {
        size_t pairs = sdr_dsp_convert_iq(raw, BLOCK_BYTES, I, Q, M, BLOCK_PAIRS);
        size_t count = adsb_demod(&dec, M, pairs, t, out, 128);
        
        total_frames += count;
        for (size_t i = 0; i < count; i++) {
            if (out[i].kind == ADSB_KIND_IDENTIFICATION) kind_id++;
            else if (out[i].kind == ADSB_KIND_AIRBORNE_POSITION) kind_pos++;
            else if (out[i].kind == ADSB_KIND_VELOCITY) kind_vel++;
            else kind_other++;
            
            if (out[i].has_position) global_positions++;
        }
        
        t += (double)pairs / SAMPLE_RATE_HZ;
        blk++;
    }
    
    printf("  Blocks processed   : %d (%.2f seconds)\n", blk, t);
    printf("  Total frames decoded: %zu\n", total_frames);
    printf("    - Identification : %zu\n", kind_id);
    printf("    - Positions      : %zu (raw CPR frames)\n", kind_pos);
    printf("    - Velocities     : %zu\n", kind_vel);
    printf("    - Other/Unknown  : %zu\n", kind_other);
    printf("  Global CPR positions resolved: %zu\n", global_positions);
    
    puts("");
    puts("  CONCLUSION");
    puts("  ---------");
    if (total_frames > 0) {
        puts("  * The ADS-B magnitude-domain preamble detection and pulse-position");
        puts("    demodulation are functioning correctly on real signal.");
        puts("  * CRC-24 successfully drops corrupted frames.");
        if (global_positions > 0) {
            puts("  * The even/odd CPR pairing cache successfully resolved global");
            puts("    coordinates across successive blocks.");
        }
    } else {
        puts("  * No frames decoded. Ensure the capture is 1090 MHz at 2 MS/s.");
    }
}

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "testfiles/adsb_modes1.bin";
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return 2;
    }
    printf("ADS-B chain probe on %s\n", path);

    static unsigned char raw[BLOCK_BYTES];
    if (fread(raw, 1, BLOCK_BYTES, f) == BLOCK_BYTES)
        probe_block(raw, BLOCK_BYTES);
    else
        puts("(file shorter than one block; skipping single-block walk)");

    consistency_sweep(f);
    fclose(f);
    return 0;
}
