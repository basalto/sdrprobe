#ifndef GSM_VIEW_MODEL_H
#define GSM_VIEW_MODEL_H

#include "core/sdr_dsp.h"
#include "tech/gsm_bcch.h"
#include "tech/gsm_session.h"

/*
 * What the GSM screen says, as plain data -- no `Rectangle`, no `Color`, no
 * raylib type anywhere (`web-visualization/07`).
 *
 * The two readouts at the centre of that screen are **sentences the drawing
 * chose**, not fields it printed: which of four things the SCH line says, and
 * which of three the BCCH line says, each with a different set of fields
 * attached. `view_gsm.c` decided that inside `DrawText` calls, so a second
 * reader would have had to decide it again -- and this repository has paid
 * for two readers deciding one thing twice often enough to make it a rule
 * (ADR-0012). The verdict is a value here; the drawing picks a colour for a
 * verdict it was handed.
 *
 * It takes the two structs it reads rather than a `const struct app *`, so
 * `check-gsm-view-model` links `-lm` alone and sets two plain objects where
 * the Scope's suite once filled nine megabytes
 * (`.scratch/layer-boundaries/issues/03-*`).
 */

/*
 * Which sentence the SCH line is. Sent by **name**, never as this integer:
 * the survey's mark crossed the wire as an ordinal, the browser re-declared
 * the order wrong, and receiver-like and empty candidates were drawn swapped
 * for months with every check green (`web-visualization/15`).
 */
enum gsm_sch_reading {
    GSM_SCH_IDLE = 0,       /* no channel chosen, or a scan is running */
    GSM_SCH_SEARCHING,      /* a channel is chosen and nothing has decoded */
    GSM_SCH_RECORDING,      /* raw I/Q is being written instead */
    GSM_SCH_DECODED
};

/*
 * And the BCCH line, which is the cell talking rather than a measurement.
 *
 * `MISSED` and `WAITING` are different facts and the window says so: the
 * broadcast channel occupies frames 2 to 5 of the 51-multiframe, so only the
 * SCH at frame 1 is followed by one. Four times in five there is nothing due
 * and nothing is wrong; the fifth is a block that should have survived.
 */
enum gsm_bcch_reading {
    GSM_BCCH_NONE = 0,      /* no SCH, so the question does not arise */
    GSM_BCCH_WAITING,       /* the multiframe has not come round */
    GSM_BCCH_MISSED,        /* a block was due here and did not survive */
    GSM_BCCH_READ
};

static inline const char *gsm_sch_reading_name(enum gsm_sch_reading r) {
    switch (r) {
    case GSM_SCH_SEARCHING: return "searching";
    case GSM_SCH_RECORDING: return "recording";
    case GSM_SCH_DECODED:   return "decoded";
    case GSM_SCH_IDLE:      break;
    }
    return "idle";
}

static inline const char *gsm_bcch_reading_name(enum gsm_bcch_reading r) {
    switch (r) {
    case GSM_BCCH_WAITING: return "waiting";
    case GSM_BCCH_MISSED:  return "missed";
    case GSM_BCCH_READ:    return "read";
    case GSM_BCCH_NONE:    break;
    }
    return "none";
}

/*
 * The band's channels, as the Channel Power Scan draws them. `power` is dBFS
 * per ARFCN, `SCAN_SENTINEL_DBFS` meaning "not visited"; `bcch_confidence` is
 * the FCCH-tone detector's, which colours a channel green.
 *
 * GSM 900 downlink is ARFCN 1 to 124, and the bound is **mirrored** from
 * `scan_plan.h` rather than included: that header is `runtime/` and a model
 * may not reach up into it (ADR-0028). This is `input_route.h`'s own device
 * for staying standalone, and `gsm_view_model.c` -- which is in `runtime/`
 * and can see both -- asserts at compile time that the two still agree, so a
 * band that grew would stop the build rather than truncate a chart.
 */
#define GSM_VIEW_MODEL_LAST_ARFCN 124
#define GSM_VIEW_MODEL_CHANNELS (GSM_VIEW_MODEL_LAST_ARFCN + 1)

struct gsm_view_model {
    /* Which channel is being inspected, and its carrier. 0 means none. */
    int selected_arfcn;
    double selected_hz;

    /* The SCH line. Fields below it are meaningful only when `sch` is
       GSM_SCH_DECODED -- zeroed otherwise rather than left stale, so a
       reader cannot show last minute's cell under this minute's heading. */
    enum gsm_sch_reading sch;
    int bsic, ncc, bcc;
    int frame_number, t1, t2, t3;
    float confidence;
    /* T1 advances once per 1326 frames, so a jump is a decode that cannot be
       right. The window appends "[T1 JUMPED]"; this is the fact behind it. */
    int implausible;

    /* The BCCH line. Each `have_*` gates its own group, exactly as the
       window's conditional `snprintf`s do. */
    enum gsm_bcch_reading bcch;
    int blocks;
    int have_lai;
    int mcc, mnc, mnc_digits, lac;
    int have_cell_id;
    int cell_id;
    int neighbour_count;
    int neighbours[GSM_SI_MAX_NEIGHBOURS];

    /* The header's signal statistics, from the block just measured. */
    int signal_stats_ready;
    struct sdr_signal_stats signal_stats;

    /* The Channel Power Scan. `scanning` is the sweep in progress, and
       `step`/`step_count` are what its caption counts. */
    int scanning;
    int step, step_count;
    int have_scan;          /* any channel has been visited */
    float power[GSM_VIEW_MODEL_CHANNELS];
    float bcch_confidence[GSM_VIEW_MODEL_CHANNELS];

    /* -- The analysis charts behind "View: Burst": the SCH burst the window's
          charts read -- the timing-correlation landscape, the soft symbol
          magnitudes and the differential phase trajectory (one array each) --
          and the SCH constellation (two arrays, x then y, projected onto the
          unit circle the way the window's default view draws it). All from
          `session.sch_symbols`, computed by the decode. Empty until a
          synchronisation burst. -- */
    int sch_valid;
    int sch_count;
    float corr[GSM_SCH_BURST_BITS];
    float soft_mag[GSM_SCH_BURST_BITS];
    float phase[GSM_SCH_BURST_BITS];
    int scatter_count;
    float scatter_x[GSM_SCH_BURST_BITS];
    float scatter_y[GSM_SCH_BURST_BITS];
};


/* Both live in `runtime/app.h`, which a model may not include (ADR-0028) --
   named here, defined where the builder can see them. */
struct gsm_view;
struct band_scan;

/*
 * Fills `out`. Reads plain fields only -- no I/O, no raylib call.
 *
 * `recording` is passed rather than read: whether raw I/Q is being written is
 * the acquisition layer's fact, and the GSM view only reports it. Passing it
 * keeps this function's inputs to the two things it is about.
 */
void gsm_view_model_build(const struct gsm_view *gsm,
                          const struct band_scan *scan,
                          const struct sdr_signal_stats *stats,
                          int stats_ready, int recording, int receiver_mode,
                          struct gsm_view_model *out);

#endif
