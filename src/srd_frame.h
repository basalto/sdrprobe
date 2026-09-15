#ifndef SRD_FRAME_H
#define SRD_FRAME_H

#include <stddef.h>
#include <stdint.h>
#include "srd_dsp.h"

/*
 * Decoder-context side of the 430-440 MHz ISM SRD module.
 *
 * Slices runs and chips into delimited frames:
 *   - Delimiter: 6 alternations of ~750 us (1.5 chips = 4.5 ms).
 *   - Full frame (SRD_FRAME_FULL):
 *       80 bits (10 bytes = 160 chips = 80 ms).
 *       Byte 0: Header (0x3F in Thomas, 0xC0 in IEEE).
 *       Bytes 1..8: 64-bit opaque payload.
 *       Byte 9: Trailer (0xD4 in Thomas, 0x2B in IEEE).
 *   - Repeat / keepalive frame (SRD_FRAME_REPEAT):
 *       24 bits (3 bytes = 48 chips = 24 ms), sent when button is held down.
 *       Byte 0: Header (0x1F in Thomas, 0xE0 in IEEE).
 *       Byte 1: Tag matching Byte 1 of the preceding full frame.
 *       Byte 2: Trailer (0xD4 in Thomas, 0x2B in IEEE).
 *
 * Links -lm only (ADR-0001, ADR-0012).
 */

#define SRD_DELIMITER_RUNS 6
#define SRD_DELIMITER_MIN_US 650.0
#define SRD_DELIMITER_MAX_US 850.0

#define SRD_FULL_FRAME_BYTES 10
#define SRD_FULL_FRAME_BITS 80
#define SRD_REPEAT_FRAME_BYTES 3
#define SRD_REPEAT_FRAME_BITS 24

#define SRD_HEADER_FULL_THOMAS 0x3F
#define SRD_HEADER_REPEAT_THOMAS 0x1F
#define SRD_TRAILER_THOMAS 0xD4

#define SRD_HEADER_FULL_IEEE 0xC0
#define SRD_HEADER_REPEAT_IEEE 0xE0
#define SRD_TRAILER_IEEE 0x2B

#define SRD_MAX_FRAMES_PER_PRESS 32

enum srd_frame_kind {
    SRD_FRAME_UNKNOWN = 0,
    SRD_FRAME_FULL    = 1, /* 10 bytes: Header 0x3F, 8 bytes payload, Trailer 0xD4 */
    SRD_FRAME_REPEAT  = 2, /* 3 bytes: Header 0x1F, 1 byte tag, Trailer 0xD4 */
    SRD_FRAME_FSK_DETECTED = 3,
    SRD_FRAME_GENERIC = 4, /* Generic decoded Manchester frame */
    SRD_FRAME_UNDECODED = 5 /* Detected burst, but no frame decoded */
};

struct srd_frame {
    enum srd_frame_kind kind;
    enum srd_modulation modulation;
    uint8_t bytes[32];
    size_t byte_count;
    size_t bit_count;
    size_t error_count;
    size_t run_index;
    double time_seconds;
};

/*
 * Scans `runs` for delimiters (6 runs of ~750 us) and extracts frames into
 * `frames_out` decoded according to `polarity`.
 *
 * Returns number of frames extracted and written into `frames_out`.
 */
/*
 * What kind of device sent a frame -- named **only where the frame's own
 * structure identifies it**, and "unknown" everywhere else.
 *
 * This is a verdict, and verdicts are the thing this repository is most
 * careful about: `signal_findings.h` refuses to turn a measurement into one
 * because "18 kBd, 25 kHz wide, continuous" lets a reader reach for the
 * TETRA view and "probably TETRA" is a claim nothing here can stand behind.
 * The difference is the evidence. A device type here rests on a **decoded
 * frame** -- a header and a trailer at known offsets in a Manchester stream
 * that was sliced, chipped and decoded without a violation -- which is not
 * something a modulation and a burst length can manufacture.
 *
 * So nothing is named from signal shape. A 2-FSK burst at 64 us chips with
 * no frame is `SRD_DEVICE_UNKNOWN`, not "likely remote": the chip period and
 * the modulation are in their own columns already, and a reader who wants to
 * draw that conclusion has what they need to draw it.
 */
enum srd_device_type {
    SRD_DEVICE_UNKNOWN = 0,
    /*
     * The delimited OOK protocol in this file's header: a 10-byte frame
     * opening 0x3F and closing 0xD4, with 3-byte repeats opening 0x1F while
     * a button is held. Read off testfiles/srd_remote_control_ook_a.bin, where the
     * repeats' tag byte matches the tag of the full frame before them -- a
     * relationship no chance decode reproduces.
     */
    SRD_DEVICE_REMOTE_CONTROL = 1,
    /*
     * The 2-FSK protocol of testfiles/srd_remote_control_fsk.bin: 14-byte
     * frames at a 64.2 us chip, every one of them opening with the same
    * three bytes across twenty transmissions and four button presses. The
    * prefix identifies the protocol only.
     */
    SRD_DEVICE_REMOTE_FSK = 2
};

/*
 * The fixed prefixes of the 2-FSK remote protocol above. Protocol
 * identifiers shared by every transmitter that speaks it.
 */
#define SRD_REMOTE_FSK_PREFIX_A 0x27E57BU
#define SRD_REMOTE_FSK_PREFIX_B 0xD81A84U

/* "SRD remote control", "SRD remote control (2-FSK)", "unknown". */
const char *srd_device_type_name(enum srd_device_type type);

/*
 * What a decoded frame says about the device that sent it. Structure only;
 * returns SRD_DEVICE_UNKNOWN for anything it cannot recognise, including
 * every frame that carries no decode at all.
 *
 * Takes the three facts it reads rather than a struct, because the window
 * keeps its log rows in a type of its own and building a `struct srd_frame`
 * to ask a question about three fields is how a decision ends up copied
 * instead of called.
 */
enum srd_device_type srd_device_type_of(enum srd_frame_kind kind,
                                        const uint8_t *bytes,
                                        size_t byte_count);

size_t srd_extract_frames(const struct srd_run *runs, size_t run_count,
                          double chip_period, enum srd_manchester_polarity polarity,
                          struct srd_frame *frames_out, size_t max_frames);

#endif /* SRD_FRAME_H */
