#include "check.h"

#include "runtime/app.h"
#include "core/device_profile.h"
#include "model/receiver_view_model.h"

#include <string.h>

/*
 * The shell's half of what a Viewer is told: which screen is showing, and
 * what the receiver is doing. It was six fields on the Scope's chart model
 * and is a type of its own (`.scratch/layer-boundaries/issues/03-*`, item
 * 4), so this suite needs neither a sample block nor a scatter history --
 * two plain structs in, one out.
 */

static void test_the_applied_state_passes_through(void) {
    struct receiver_applied applied;
    struct device_profile device;
    struct receiver_view_model rvm;

    memset(&applied, 0, sizeof(applied));
    memset(&device, 0, sizeof(device));
    applied.frequency_hz = 948400000;
    applied.sample_rate_hz = 2000000;
    applied.ppm = 7;
    applied.generation = 3;
    device.full_scale = 127.5f;

    receiver_view_model_build(&applied, &device, TAB_SCOPE, DECODE_FM, &rvm);

    check_int("center_hz is the applied frequency", (long)rvm.center_hz,
              948400000);
    check_int("sample_rate_hz is the applied rate", (long)rvm.sample_rate_hz,
              2000000);
    check_int("ppm passes through", rvm.ppm, 7);
    check_int("tuning generation passes through",
              (long)rvm.tuning_generation, 3);
    check_close("full_scale passes through", rvm.full_scale, 127.5, 1e-6);
    check_str("the screen is the tab's name", rvm.screen, "scope");
}

/*
 * Zero in, zero out -- and the screen still named. A Viewer connecting
 * before the receiver has been opened gets `receiver_state` all the same,
 * and a blank `screen` there is a message no browser view matches.
 */
static void test_nothing_applied_yet(void) {
    struct receiver_applied applied;
    struct device_profile device;
    struct receiver_view_model rvm;

    memset(&applied, 0, sizeof(applied));
    memset(&device, 0, sizeof(device));
    memset(&rvm, 0xff, sizeof(rvm));

    receiver_view_model_build(&applied, &device, TAB_SURVEY, 0, &rvm);

    check_int("center_hz is zero", (long)rvm.center_hz, 0);
    check_int("sample_rate_hz is zero", (long)rvm.sample_rate_hz, 0);
    check_int("tuning generation is zero", (long)rvm.tuning_generation, 0);
    check_str("the screen is named anyway", rvm.screen, "survey");
}

/*
 * Every screen this program has, spelled the way the wire spells it.
 *
 * `receiver_state` carries one `screen` name instead of the `tab` and
 * `decode` integers it used to, because a browser matching `tab === 2 &&
 * decode === 0` re-declares two of this program's enums in a second
 * language -- the shape that drew the survey's marks swapped for months
 * (`web-visualization/15`). The names are each browser view's own `id`, so
 * a wrong one here is a view that never matches and a panel that never
 * shows.
 *
 * Walked exhaustively rather than sampled: a decode kind added to the enum
 * without a name here falls through to "decode", and this is what says so.
 * The table's own length is asserted against the enum at compile time in
 * receiver_view_model.c, so the two failures are told apart -- a build
 * error for a kind nobody named, this for one named wrongly.
 */
static void test_every_screen_has_a_name(void) {
    struct named { int tab, decode; const char *want; };
    static const struct named cases[] = {
        { TAB_SURVEY, 0,             "survey" },
        { TAB_SCOPE,  0,             "scope"  },
        { TAB_DECODE, DECODE_FM,     "fm"     },
        { TAB_DECODE, DECODE_ADSB,   "adsb"   },
        { TAB_DECODE, DECODE_GSM,    "gsm"    },
        { TAB_DECODE, DECODE_LTE,    "lte"    },
        { TAB_DECODE, DECODE_TETRA,  "tetra"  },
        { TAB_DECODE, DECODE_SRD,    "srd"    },
    };
    size_t i;
    char name[12];

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        receiver_screen_name(name, sizeof(name), cases[i].tab,
                             cases[i].decode);
        check_msg(strcmp(name, cases[i].want) == 0,
                  "tab %d decode %d spells \"%s\", expected \"%s\"\n",
                  cases[i].tab, cases[i].decode, name, cases[i].want);
    }
    /* The tab decides on the Decode tab and nowhere else: the Survey's name
       must not change with whatever decode kind was last chosen. */
    receiver_screen_name(name, sizeof(name), TAB_SURVEY, DECODE_SRD);
    check_str("the survey is the survey whatever decode is remembered",
             name, "survey");
    /* And a decode kind past the end of the table is named, not indexed. */
    receiver_screen_name(name, sizeof(name), TAB_DECODE, 99);
    check_str("an unknown decode kind falls back rather than reading past "
             "the table", name, "decode");
}

int main(void) {
    test_the_applied_state_passes_through();
    test_nothing_applied_yet();
    test_every_screen_has_a_name();
    return check_report("the receiver's view model: the screen's name and "
                        "the applied tuning");
}
