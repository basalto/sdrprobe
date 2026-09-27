#ifndef BAND_PLAN_VIEW_H
#define BAND_PLAN_VIEW_H

#include <stddef.h>

#include "band_plan.h"

/*
 * Where the survey sends a reader who presses Inspect.
 *
 * A lookup from an allocation's decoder to what the button should say, and so
 * to whether there is anywhere to go at all. Here rather than inside the click
 * handler because it is a decision -- and because the click handler knew two
 * of the four technologies this program decodes, offering nothing on an LTE
 * carrier and nothing on an FM station, and no check could see that a case was
 * simply absent.
 */

/* What the button reads, or NULL when there is nowhere to go. */
static inline const char *band_plan_inspect_label(enum band_plan_decoder d) {
    switch (d) {
    case BAND_PLAN_GSM:  return "Inspect in Decode > GSM";
    case BAND_PLAN_ADSB: return "Inspect in Decode > ADS-B";
    case BAND_PLAN_LTE:  return "Inspect in Decode > LTE";
    /* "Listen", because that is what it does that the others do not. */
    case BAND_PLAN_FM:   return "Listen in Decode > FM";
    case BAND_PLAN_NONE: break;
    }
    return NULL;
}

/* Whether Inspect can do anything with this allocation. */
static inline int band_plan_can_inspect(enum band_plan_decoder d) {
    return band_plan_inspect_label(d) != NULL;
}

#endif
