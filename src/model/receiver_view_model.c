#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "model/receiver_view_model.h"
#include "runtime/app.h"
#include "core/device_profile.h"

/*
 * The screen names, in `enum decode_kind`'s own order -- the table is
 * indexed by that enum, so the two agree or nothing does.
 */
static const char *const decode_screens[] = {
    "fm", "adsb", "gsm", "lte", "tetra", "srd"
};

#define DECODE_SCREEN_COUNT \
    ((int)(sizeof(decode_screens) / sizeof(decode_screens[0])))

/* Extending or reordering `enum decode_kind` without touching the table is
   a build error, not a screen that comes back named as its neighbour --
   the same device sdrprobe.c uses for input_route.h's mirrored value. */
typedef char decode_screens_match_the_enum
    [(DECODE_SRD + 1 == DECODE_SCREEN_COUNT) ? 1 : -1];

/*
 * Tab plus decode kind, as the one name a reader needs. On the Decode tab
 * the screen *is* the technology; elsewhere it is the tab. Kept here rather
 * than in `input_route.h` because it is what crosses the wire, and the two
 * enums it spells are `app.h`'s and `input_route.h`'s respectively -- this
 * is the only place both are already in hand.
 */
void receiver_screen_name(char *out, size_t size, int tab, int decode) {
    const char *name = "scope";

    if (tab == TAB_SURVEY)
        name = "survey";
    else if (tab == TAB_DECODE)
        name = (decode >= 0 && decode < DECODE_SCREEN_COUNT)
                   ? decode_screens[decode] : "decode";
    snprintf(out, size, "%s", name);
}

void receiver_view_model_build(const struct receiver_applied *applied,
                               const struct device_profile *device,
                               int tab, int decode,
                               struct receiver_view_model *out) {
    memset(out, 0, sizeof(*out));

    receiver_screen_name(out->screen, sizeof(out->screen), tab, decode);

    out->center_hz = applied->frequency_hz;
    out->sample_rate_hz = applied->sample_rate_hz;
    out->ppm = applied->ppm;
    out->tuning_generation = applied->generation;

    out->full_scale = device->full_scale;
}
