#ifndef BAND_PLAN_H
#define BAND_PLAN_H

/*
 * What a frequency is allocated to.
 *
 * A static table, a lookup, and nothing else: no DSP, no receiver, no GUI.
 * The distinction this file exists to hold is the one a later reader will be
 * tempted to blur -- it says what a band is *for*, never what a signal *is*.
 * 943.2 MHz falls inside the GSM 900 downlink allocation whether or not
 * anything is transmitting there, and a carrier found at that frequency has
 * not thereby been identified as GSM; nothing has been demodulated. Callers
 * must present the result as a lookup. See
 * docs/adr/0015-band-plan-is-a-lookup.md.
 *
 * The table holds Portugal's allocations -- ITU Region 1 as ANACOM applies it,
 * which is what most of Europe looks like too. A frequency allocated
 * differently elsewhere is simply absent rather than wrong: band_plan_lookup
 * returns NULL outside anything it knows, which is an honest "no comment".
 */

/* A decode view that can be pointed at this allocation, for a caller offering
   the operator somewhere to go next. Named here so the table needs no
   knowledge of what the views are called. */
enum band_plan_decoder {
    BAND_PLAN_NONE = 0,
    BAND_PLAN_GSM,
    BAND_PLAN_ADSB,
    BAND_PLAN_LTE,
    BAND_PLAN_FM
};

/* How many there are, so a check can walk them. */
#define BAND_PLAN_DECODER_COUNT 5

struct band_plan_entry {
    double lower_hz;
    double upper_hz;
    const char *name;
    const char *note;   /* may be NULL */
    enum band_plan_decoder decoder;
};

/* The allocation containing `hz`, or NULL when the table says nothing about
   it. Entries never overlap, which band_plan_entry_count() and
   band_plan_entry_at() let a check verify over the whole table. */
const struct band_plan_entry *band_plan_lookup(double hz);

/*
 * The grid an allocation's channels sit on: a base frequency and a spacing.
 *
 * Says where a transmitter *could* be, never that one is there -- the same
 * refusal the rest of this table makes (ADR-0015). A caller testing what a
 * reading could be needs a nominal frequency to test against, and for a
 * service that is a channel; `reading_origin.h` is the consumer.
 *
 * **A second table rather than two more fields on `band_plan_entry`**, and
 * the reason is ordinary: eighty curated rows would each have to carry two
 * zeroes to stay `-Wall -W` clean, and the four or five that matter would be
 * invisible among them. Here the whole list of allocations with a known
 * raster is one glance. The coupling that buys is checked rather than
 * assumed -- `check-band-plan` asserts every row below names a real
 * allocation by its exact lower edge.
 *
 * **Only allocations with no decoder may have one**, also checked
 * (`.scratch/reading-origin/issues/02-*`). An allocation with a decoder can
 * be asked directly -- `gsm_arfcn_hz()` and `fm_scan.h` own those grids --
 * and a second statement of a channel map here would be a table that can
 * disagree with the module that decodes it, which is worse than one that says
 * nothing.
 *
 * Regional, like the rest of the table: Portugal as ANACOM applies it. 8.33
 * kHz airband channelling is European and this does not imply the grid
 * travels.
 */
struct band_plan_raster {
    double lower_hz;      /* the allocation's own lower edge, exactly */
    double raster_hz;
    double base_hz;
};

int band_plan_raster_count(void);
const struct band_plan_raster *band_plan_raster_at(int index);

/*
 * The channel of `hz`'s allocation nearest `hz`, or 0 when the table knows no
 * raster there -- which is most of the spectrum and is an honest "no comment"
 * rather than a failure.
 */
double band_plan_channel_hz(double hz);

/*
 * The grid itself for `hz`'s allocation: spacing and base, both left 0 when
 * the table knows none. Returns 1 when it filled them in.
 *
 * The lookup lives with the table rather than beside its caller so that a
 * consumer needs `band_plan.c` and nothing else -- `survey_session.c` reaches
 * it without pulling in the survey's record layer, and the raster list has one
 * reader.
 */
int band_plan_channel_grid(double hz, double *spacing_hz, double *base_hz);

int band_plan_entry_count(void);
const struct band_plan_entry *band_plan_entry_at(int index);

#endif
