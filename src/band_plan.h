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
    BAND_PLAN_LTE
};

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

int band_plan_entry_count(void);
const struct band_plan_entry *band_plan_entry_at(int index);

#endif
