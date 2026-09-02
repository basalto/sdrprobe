#ifndef LTE_DSP_H
#define LTE_DSP_H

#include <stddef.h>
#include <stdint.h>

/*
 * LTE (E-UTRA) technology plugin.
 *
 * Like the GSM plugin (gsm_dsp.h) it provides a channel -> frequency map and a
 * sync detector, and like it, it goes one step past calibration-grade
 * detection: the sync detector here is a full cell search, and it hands the
 * Decoder side (lte_mib.h) the soft bits of the broadcast channel.
 *
 * It depends on nothing from sdr_dsp.h and operates on raw centred I/Q.
 *
 * THE SAMPLE RATE IS NOT THE HOUSE RATE. Everything below is arithmetic on
 * LTE's own grid -- 128 subcarriers of 15 kHz, which is 1.92 MS/s exactly --
 * and the functions that take a sample rate reject anything else rather than
 * quietly resampling. A capture for this plugin is recorded at 1.92 MS/s. See
 * docs/adr/0014-lte-runs-on-lte-s-sample-grid.md.
 *
 * What the plugin can see is bounded by that rate, and the bound is the
 * standard's doing rather than a shortcoming here: PSS, SSS and PBCH all live
 * in the central 1.08 MHz of a carrier whatever its real bandwidth, precisely
 * so a handset can find a cell before it knows how wide it is. Everything
 * above the MIB -- SIB1 and the rest -- is spread across the full bandwidth
 * and is simply not in these samples.
 */

/* The grid. 128 * 15 kHz = 1.92 MHz, and every count below follows from it. */
#define LTE_FFT_SIZE 128
#define LTE_SUBCARRIER_SPACING_HZ 15000.0
#define LTE_SAMPLE_RATE_HZ (LTE_FFT_SIZE * LTE_SUBCARRIER_SPACING_HZ)

/* 0.5 ms, 1 ms, 10 ms in samples. A slot is seven symbols under the normal
   cyclic prefix and six under the extended one, and both fill it exactly:
   10 + 128 + 6 * (9 + 128) = 6 * (32 + 128) = 960. */
#define LTE_SLOT_SAMPLES 960
#define LTE_SUBFRAME_SAMPLES (2 * LTE_SLOT_SAMPLES)
#define LTE_HALF_FRAME_SAMPLES (5 * LTE_SUBFRAME_SAMPLES)
#define LTE_FRAME_SAMPLES (10 * LTE_SUBFRAME_SAMPLES)

/* Cyclic prefix lengths at this rate. */
#define LTE_CP_FIRST_SAMPLES 10
#define LTE_CP_REST_SAMPLES 9
#define LTE_CP_EXTENDED_SAMPLES 32

/* The synchronisation signals occupy 62 subcarriers, 31 either side of the
   unused DC. The last symbol of the slot carries PSS, the one before it SSS,
   which puts the useful part of PSS at this offset into the subframe -- the
   same offset under either cyclic prefix, since both fill the slot. */
#define LTE_SYNC_SUBCARRIERS 62
#define LTE_PSS_USEFUL_OFFSET 832
/* How far ahead of PSS the SSS symbol starts, which is the one measurement
   that tells the two cyclic prefixes apart. */
#define LTE_SSS_LEAD_NORMAL 137
#define LTE_SSS_LEAD_EXTENDED 160

/* The broadcast channel: 72 subcarriers, 36 either side of DC, over the first
   four symbols of the second slot of subframe 0. Two of those symbols give a
   third of their subcarriers to reference signals, leaving
   72 + 72 + 48 + 48 = 240 resource elements and, QPSK, 480 bits. */
#define LTE_PBCH_SUBCARRIERS 72
#define LTE_PBCH_SYMBOLS 4
#define LTE_PBCH_RESOURCE_ELEMENTS 240
#define LTE_PBCH_SOFT_BITS (2 * LTE_PBCH_RESOURCE_ELEMENTS)

/* The physical-layer cell identity is 3 * N_ID_1 + N_ID_2. */
#define LTE_N_ID_1_COUNT 168
#define LTE_N_ID_2_COUNT 3
#define LTE_PCI_COUNT (LTE_N_ID_1_COUNT * LTE_N_ID_2_COUNT)

/*
 * EARFCN <-> downlink frequency.
 *
 * F = F_low + 0.1 MHz * (earfcn - offset), the E-UTRA raster. The table holds
 * the FDD bands an RTL-SDR can actually tune, plus the three common ones just
 * above its reach: a caller holding a channel number deserves the frequency it
 * names, and whether this receiver can hear it is a separate question.
 */
struct lte_band {
    int band;             /* the 3GPP band number */
    unsigned int earfcn_low;
    unsigned int earfcn_high;
    double downlink_low_hz;
    const char *name;     /* how the band is spoken of: "800 MHz" */
};

int lte_band_count(void);
const struct lte_band *lte_band_at(int index);
const struct lte_band *lte_band_for_earfcn(unsigned int earfcn);

/* Downlink centre frequency of an EARFCN. Returns 0 for one no band claims. */
int lte_earfcn_downlink_hz(unsigned int earfcn, uint32_t *frequency_hz);
/* The EARFCN whose carrier is nearest `hz`, or 0 when no band covers it. The
   raster is 100 kHz, so "nearest" is never more than 50 kHz away. */
int lte_earfcn_for_hz(double hz);

/*
 * The sequences, exposed because they are worth checking on their own: a
 * detector that generates a wrong sequence correlates against nothing, and
 * says only that it found no cell.
 *
 * Both write LTE_SYNC_SUBCARRIERS values in the standard's d(n) order, n
 * running from the lowest subcarrier upwards. PSS is complex; SSS is +-1 and
 * so is written as reals.
 */
void lte_pss_sequence(int n_id_2, float *real, float *imag);
void lte_sss_sequence(int n_id_1, int n_id_2, int subframe5, float *values);

/*
 * Where PSS was found, and what its two halves say about the tuning.
 *
 * `useful_start` indexes the first sample of the symbol's useful part -- past
 * the cyclic prefix -- because that is where the FFT is taken, and every other
 * offset in this file is measured from it.
 */
struct lte_pss_result {
    int detected;
    int n_id_2;
    size_t useful_start;
    float peak;            /* correlation, normalised to [0, 1] */
    float runner_up;       /* the best of the other two roots */
    double frequency_offset_hz;
};

/*
 * Search a block for PSS. Returns 1 when the peak clears the candidate floor.
 *
 * "Candidate" is the word: a peak here is a place worth taking to the
 * secondary sequence, not a cell. The floor sits below what noise reaches over
 * this many alignments on purpose, because the secondary sequence is a far
 * harder test and is where a cell is actually claimed.
 *
 * The correlation is taken in two halves of 64 samples whose magnitudes are
 * added, which is what keeps it working under a frequency offset: a tuning
 * error rotates the second half relative to the first, and a single 128-sample
 * correlation would cancel itself long before the offset became implausible.
 * The rotation is then the measurement -- the phase between the two halves
 * gives the offset, unambiguously up to half a subcarrier either way, which is
 * +-7.5 kHz.
 *
 * Only the first half-frame is searched, because PSS repeats every half-frame
 * and one occurrence is all a search needs; `pair_count` must cover that plus
 * a symbol. Searching further would find the same cell again at ten times the
 * cost.
 */
int lte_pss_detect(const float *i_samples, const float *q_samples,
                   size_t pair_count, double sample_rate,
                   struct lte_pss_result *result);

/*
 * A cell, as far as the synchronisation signals describe it.
 *
 * `subframe0_start` is the first sample of subframe 0 -- the frame boundary --
 * which is what the broadcast channel is located from. It can be negative in
 * principle when the PSS found was subframe 5's and the frame began before the
 * block did; the search only reports a cell when a whole subframe 0 is present
 * in the samples given, so callers get an index they can use.
 */
struct lte_cell {
    int detected;
    int pci;
    int n_id_1;
    int n_id_2;
    int extended_cp;
    int half_frame;             /* 1 when the PSS found was subframe 5's */
    size_t subframe0_start;
    double frequency_offset_hz;
    float pss_correlation;
    float pss_runner_up;        /* the best either other root reached */
    float sss_correlation;      /* best candidate, normalised to [0, 1] */
    float sss_runner_up;        /* second best, so the margin can be judged */
};

/*
 * PSS, then SSS, then the frame boundary. Returns 1 when a cell is found.
 *
 * SSS is read differentially -- each subcarrier times the conjugate of its
 * neighbour -- and never against a channel estimate. Two neighbouring
 * subcarriers went through almost the same channel, so it cancels; and a
 * timing error is a phase ramp across the subcarriers, so that cancels too.
 * The obvious alternative, dividing out a channel measured from PSS one symbol
 * away, works on a synthesised frame and fails on air: live captures score
 * 0.44 that way, which is noise, and 0.75 this way. Every frame the block
 * holds is read and their scores added, since the sequence is the same in all
 * of them.
 */
int lte_cell_search(const float *i_samples, const float *q_samples,
                    size_t pair_count, double sample_rate,
                    struct lte_cell *cell);

/*
 * The broadcast channel's soft bits, ready for lte_mib.h.
 *
 * `antenna_ports` is a hypothesis, not a fact: nothing before the MIB's own
 * CRC says how many ports the cell transmits on, so a caller tries 1, 2 and 4
 * and lets the CRC decide. It changes how the resource elements are combined
 * -- one port is a plain equalisation, two and four are space-frequency block
 * codes -- which is why it belongs here and not in the decoder.
 *
 * Writes LTE_PBCH_SOFT_BITS values, positive for a zero bit. Returns 0 when
 * the block does not hold the whole of subframe 0 the cell points at.
 */
int lte_pbch_soft_bits(const float *i_samples, const float *q_samples,
                       size_t pair_count, double sample_rate,
                       const struct lte_cell *cell, size_t subframe0_start,
                       int antenna_ports, float *soft_bits);

/*
 * Cell-specific reference signals for one symbol of the central 72
 * subcarriers: 12 complex values per antenna port, in increasing subcarrier
 * order. Exposed for the checks, and because it is the one part of the
 * broadcast path whose correctness is independent of any received signal.
 *
 * `slot` is the slot within the frame (PBCH sits in slot 1) and `symbol` the
 * symbol within it. Returns 0 when that symbol carries no reference signal for
 * that port.
 */
int lte_crs_sequence(int pci, int slot, int symbol, int port, int extended_cp,
                     float *real, float *imag);
/* Which of the 72 subcarriers that port's reference signals sit on, lowest
   first. Writes 12 indices. Returns 0 when the symbol carries none. */
int lte_crs_subcarriers(int pci, int slot, int symbol, int port, int *indices);

/*
 * A 128-point FFT of one symbol's useful part, in natural bin order: bin 0 is
 * DC, which LTE does not use. Index it through lte_subcarrier_bin(), which is
 * where the skip over DC lives.
 */
void lte_symbol_fft(const float *i_samples, const float *q_samples,
                    float *real_out, float *imag_out);
/* The FFT bin a physical subcarrier lands in. DC (0) is unused by LTE, so
   subcarrier +1 is bin 1 and subcarrier -1 is bin LTE_FFT_SIZE - 1. */
int lte_subcarrier_bin(int subcarrier);

#endif
