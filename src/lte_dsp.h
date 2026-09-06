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

/*
 * The bands an RTL-SDR can actually reach, in the order a picker should offer
 * them. The table above holds bands 1, 3 and 7 as well, because a caller
 * holding an EARFCN deserves the frequency it names -- but offering them in a
 * scan picker wastes minutes tuning where an R820T cannot hear, which is what
 * the calibration's band buttons did when they took the table's first three.
 */
#define LTE_REACHABLE_BANDS 3
int lte_reachable_band(int index);
/* The table entry for a band number, or NULL. */
const struct lte_band *lte_band_for_number(int number);
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
 * What the search saw on its way to an answer, for a view that wants to draw
 * it. Every field is a by-product: nothing here is needed to find a cell, and
 * passing NULL for the trace skips the work of collecting it.
 *
 * It is the same arrangement as gsm_sch_symbols, and for the same reason. A
 * detection is a number, and a number cannot say whether it was a clean lock
 * or a coin toss -- the correlation profile shows how sharp the peak was, and
 * the candidate scores show by how much the winner beat the field, which is
 * the gate the whole cell search turns on.
 */
#define LTE_TRACE_PROFILE 193   /* an odd count, so the peak sits in the middle */

struct lte_trace {
    int valid;

    /* The primary sequence's correlation either side of where it peaked. */
    float profile[LTE_TRACE_PROFILE];
    int profile_count;
    int profile_peak;            /* index into profile[] */

    /* Every N_ID_1's score, for the half-frame that won. */
    float candidate[LTE_N_ID_1_COUNT];
    int candidate_count;
    int candidate_best;

    /* The channel the reference signals measured across the broadcast
       channel's 72 subcarriers, in dB relative to its own mean. */
    float channel_db[LTE_PBCH_SUBCARRIERS];
    int channel_count;

    /* And the elements themselves, after the channel and the space-frequency
       block code were undone. */
    float element_i[LTE_PBCH_RESOURCE_ELEMENTS];
    float element_q[LTE_PBCH_RESOURCE_ELEMENTS];
    unsigned char element_bit[LTE_PBCH_RESOURCE_ELEMENTS];
    int element_count;
};

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
 * gives the offset, and a phase can only say so much: the answer wraps every
 * full turn, which is one subcarrier, so what comes back is the offset
 * MODULO 15 kHz and nothing more.
 *
 * That is not a rounding error, it is half the measurement missing. An
 * uncalibrated dongle is tens of parts per million out, which at 800 MHz is
 * tens of kilohertz -- two subcarriers on the captures here -- and this
 * function reports the leftover 1.8 kHz of it with every appearance of
 * confidence. The whole-subcarrier part has to be found somewhere else, and
 * lte_cell_search finds it by trying them: see LTE_INTEGER_OFFSETS.
 *
 * Only the first half-frame is searched, because PSS repeats every half-frame
 * and one occurrence is all a search needs; `pair_count` must cover that plus
 * a symbol. Searching further would find the same cell again at ten times the
 * cost.
 */
int lte_pss_detect(const float *i_samples, const float *q_samples,
                   size_t pair_count, double sample_rate,
                   struct lte_pss_result *result, struct lte_trace *trace);

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
    /* How many whole subcarriers of the tuning error the primary sequence
       could not see. frequency_offset_hz already includes it. */
    int integer_offset;
    float sss_correlation;      /* best candidate, normalised to [0, 1] */
    float sss_runner_up;        /* second best, so the margin can be judged */
    /*
     * What the two cyclic-prefix hypotheses actually scored, and whether each
     * was tried at all.
     *
     * `extended_cp` above is a verdict, and until this was here it was a
     * verdict with nothing behind it: the search stops as soon as one
     * hypothesis clears LTE_SSS_CONFIDENT, so a cell whose secondary sequence
     * reads 0.83 under the normal prefix has the extended one *never
     * measured*. The chain then printed "normal CP" as a fact about a
     * comparison it had not made. `.scratch/lte-band8-mib/` is that cell.
     *
     * Filled only when a trace is asked for -- the extra hypothesis costs one
     * more secondary-sequence read, which the live path should not pay for a
     * number nobody is looking at. `cp_measured[k]` is 0 where nothing was
     * tried, and a score of 0 is otherwise a real reading.
     */
    float cp_score[2];          /* [0] normal, [1] extended */
    int cp_measured[2];
    /*
     * How far pss_refine_timing moved the primary sequence's peak once the
     * whole tuning error was known, and what the best correlation was outside
     * LTE_TIMING_GUARD samples of where it settled.
     *
     * A frequency error moves a Zadoff-Chu peak as well as weakening it, so a
     * large shift is ordinary. A *sidelobe* close to the peak is not: it means
     * the sample the whole frame is measured from was chosen on a thin
     * margin, and four symbols of broadcast channel are read from it.
     */
    int timing_shift;
    float timing_sidelobe;
};

/*
 * What a cell measures to: reference signal received power, the carrier's
 * total received power, and the ratio between them (36.214 sections 5.1.1
 * to 5.1.3).
 *
 * **RSRP here is dBFS and not dBm, and the difference is not pedantry.** The
 * standard's quantity is an absolute power at the antenna connector, and
 * reaching it needs the antenna's gain, the cable's loss and the receiver's
 * gain in known units -- none of which this program has. What is measured is
 * the power in the converter's full-scale units, which compares one cell with
 * another *on this receiver at this gain* and means nothing across
 * installations. The Probe context may not claim a decibel-milliwatt it did
 * not measure.
 *
 * The scale is decibels below a full-scale subcarrier, so every reading is
 * negative and 0 would be a signal filling the converter on its own.
 *
 * RSRQ is the exception and it is worth having for that reason alone: it is
 * N * RSRP / RSSI, a ratio of two powers measured through the same chain, so
 * every unknown gain in front of it cancels. It is directly comparable with a
 * handset's reading, and check-lte-dsp pins that by scaling a buffer and
 * asserting RSRP moves while RSRQ does not.
 */
struct lte_reference_power {
    float rsrp_dbfs;        /* mean power in one reference resource element */
    float rssi_dbfs;        /* total power across the measured blocks */
    float rsrq_db;          /* N * RSRP / RSSI -- free of any fixed gain */
    /*
     * RS-SINR (36.214): the reference elements' signal power over the noise
     * and interference on those same elements, using port 0's references as
     * the standard requires. Like RSRQ it is a ratio through one chain, so it
     * carries no calibration and is comparable with a handset's.
     *
     * The noise is measured **where nothing is transmitted**: 36.211 clause
     * 6.11.1.2 reserves the five resource elements either side of the primary
     * and secondary sequences, so their power is noise and interference with
     * no channel in the way. srsRAN offers the same measurement as
     * SRSRAN_NOISE_ALG_EMPTY_SC.
     *
     * The obvious approach is to difference each reference against its
     * neighbours -- srsRAN's estimate_noise_pilots -- and at this spacing it
     * does not work. Two versions were built and measured before this one,
     * and both are in `.scratch/lte-more-per-carrier/issues/02`: a plain
     * second difference read **-17.8 dB** for a cell decoding nine tenths of
     * its messages, and de-rotating the mean delay first improved it only to
     * **+3.2 dB**. References sit 90 kHz apart and a channel between them is
     * a rotation rather than a straight line, so what the difference removes
     * is the slope and what is left is the curvature -- the channel, not the
     * noise. On the reserved elements the same cells read 24 to 34 dB, in the
     * order their reference powers predict.
     *
     * The signal is the reference power with the noise taken out, because a
     * reference element carries both. srsRAN reports rsrp/noise, which is
     * (S+N)/N and cannot read below 0 dB.
     */
    float noise_dbfs;       /* mean noise-plus-interference per reference */
    float sinr_db;          /* RS-SINR: (RSRP - noise) over noise */
    int resource_blocks;    /* N: what the powers above were measured over */
    int references;         /* how many reference elements were averaged */
};

/*
 * Measured over the six central resource blocks, which is what a receiver at
 * LTE's own 1.92 MS/s can see (ADR-0014) -- the standard allows any bandwidth
 * and requires the count be reported with the answer, which is what
 * `resource_blocks` is for.
 *
 * Averaged over every frame the block holds, on the symbol carrying port 0's
 * references. Returns 1 when `out` is filled.
 */
int lte_reference_power(const float *i_samples, const float *q_samples,
                        size_t pair_count, double sample_rate,
                        const struct lte_cell *cell,
                        struct lte_reference_power *out);

/*
 * PSS, then SSS, then the frame boundary. Returns 1 when a cell is found.
 *
 * How many whole subcarriers either way the search will look for the part of
 * the tuning error the primary sequence cannot report. Five is +-75 kHz,
 * which at 800 MHz is +-94 parts per million -- past anything a working
 * dongle does, and cheap, since each one costs only a re-read of one symbol
 * and a walk through the candidates.
 */
#define LTE_INTEGER_OFFSETS 5

/* A score this good, with this much daylight under it, ends the integer
   search early. The offsets are tried outward from zero, so a receiver whose
   error is the usual two subcarriers pays for four hypotheses rather than
   eleven -- and a weak signal still gets the whole sweep, because nothing
   clears this. */
#define LTE_SSS_CONFIDENT 0.72f

/* And how far either side of the first peak the timing is looked for again
   once the offset is known. A cyclic prefix is nine samples, so anything
   beyond a few tens is a different symbol. */
#define LTE_TIMING_SEARCH 48

/* How far from the chosen peak a correlation has to be before it counts as a
   sidelobe rather than as the peak's own shoulder. A cyclic prefix is nine
   samples at this rate, so a competitor inside that costs the broadcast
   channel a phase ramp and no more; one beyond it is a different symbol
   boundary and reads a different frame. */
#define LTE_TIMING_GUARD 9

/*
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
/*
 * How coherently each antenna port's reference signals read, which is how
 * many antennas the cell is actually transmitting on.
 *
 * A reference symbol has unit magnitude, so dividing the received value by the
 * expected one keeps the magnitude whatever sequence is used -- a channel
 * *level* therefore cannot tell a port that is transmitting from one that is
 * not, and it has to be the phase. Against the right sequence, neighbouring
 * per-reference estimates differ by one consistent rotation, the channel's
 * delay across the band; against noise they differ randomly. This returns the
 * coherence of that difference, port by port, exactly as the secondary
 * sequence is read differentially and for the same reason: it survives any
 * channel, and nothing but the real sequence can fake it.
 *
 * There are twelve references per port in the six central blocks, so eleven
 * differences, and **chance is about 0.30** -- LTE_PORT_COHERENCE_CHANCE.
 * A port well above it is transmitting; a port at it is not.
 *
 * This is the measurement that found the four-port cell on band 8, and it
 * says so before any message decodes and without sharing a line of code with
 * the antenna-port count in the broadcast's parity mask. Two independent
 * answers agreeing is the only kind of corroboration a transcription can get.
 *
 * Writes LTE_PORT_COUNT values. Returns 1 on success.
 */
#define LTE_PORT_COUNT 4
#define LTE_PORT_COHERENCE_CHANCE 0.30f

/*
 * Above this, a port is carrying references rather than noise.
 *
 * Chosen by measuring both sides rather than by taste. Ports that are
 * transmitting read 0.73 to 0.92 on the two real captures and above 0.9 on
 * synthetic buffers; ports that are not read 0.33 to 0.41, against a chance
 * level of 0.30 from eleven phase differences. The gap is wide and this sits
 * in the middle of it, so the constant would have to be wrong by a lot before
 * it decided anything.
 */
#define LTE_PORT_COHERENCE_PRESENT 0.55f

int lte_port_coherence(const float *i_samples, const float *q_samples,
                       size_t pair_count, double sample_rate,
                       const struct lte_cell *cell,
                       float coherence[LTE_PORT_COUNT]);

int lte_cell_search(const float *i_samples, const float *q_samples,
                    size_t pair_count, double sample_rate,
                    struct lte_cell *cell, struct lte_trace *trace);

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
/*
 * Every cell on the carrier, strongest first, rather than only the loudest.
 *
 * A carrier can hold several, and the single-cell search has one answer to
 * give: on EARFCN 3625 an 85-block run found PCI 190 in forty blocks and
 * PCI 402 in forty-two, which reads as one cell changing its mind. The two
 * differ in N_ID_2, which is the ordinary case for co-channel neighbours --
 * the primary sequence is a different Zadoff-Chu root for each, so both peaks
 * are already in the correlation and only the winner was kept.
 *
 * Each root is put through the same gates the single-cell path uses, at its
 * own alignment, so a cell reported here has cleared exactly what a cell
 * reported there clears. **Two cells sharing a root at different frame
 * timings are not found**: that needs the peak of the first suppressed and
 * the search re-run, and no such pair has been measured here.
 *
 * Ordering is by reference-signal power, which is the measurement that
 * separated 190 from 402 (-33.3 against -35.0 dBFS) -- a correlation score
 * says how well a sequence matched, not how strong a cell is, and the two
 * disagree. Cells whose power cannot be measured sort last, keeping their
 * search order.
 *
 * **A second cell is only found when it is within a couple of decibels of the
 * first**, and that is a property of the signal rather than of the gates.
 * Both cells transmit across the whole measured bandwidth, so the weaker
 * one's secondary sequence is read through the stronger one's transmission,
 * and no amount of integration removes an interferer. Measured on synthetic
 * carriers: both cells come back at 1.4 dB apart and only the stronger at
 * 6.9 dB, which check-lte-dsp pins at both ends. The real pair differ by
 * 1.7 dB. Reaching further would mean subtracting the stronger cell before
 * searching again, which this does not do.
 *
 * An identity reported here has also had to predict its own reference
 * signals. That gate is not decoration: the secondary sequences of different
 * N_ID_2 are the same two m-sequences at different shifts, so a strong cell
 * correlates well enough with another root's candidates to clear a
 * correlation and a margin, and a single-cell buffer duly reported two before
 * the gate was added.
 *
 * Writes at most `max` cells and returns how many. `trace` follows the first.
 */
#define LTE_MAX_CELLS_PER_CARRIER LTE_N_ID_2_COUNT

int lte_cell_search_all(const float *i_samples, const float *q_samples,
                        size_t pair_count, double sample_rate,
                        struct lte_cell *cells, int max,
                        struct lte_trace *trace);

int lte_pbch_soft_bits(const float *i_samples, const float *q_samples,
                       size_t pair_count, double sample_rate,
                       const struct lte_cell *cell, size_t subframe0_start,
                       int antenna_ports, float *soft_bits,
                       struct lte_trace *trace);

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
