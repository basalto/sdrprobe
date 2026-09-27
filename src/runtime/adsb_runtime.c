/*
 * ADS-B's runtime: the tuning check, entering and leaving the view, one
 * block of Mode S, and the row formatting the log and the Viewer both read.
 *
 * Out of `view_adsb.c` by `.scratch/layer-boundaries/issues/02-*`. The
 * drawing and the input stayed behind. No raylib here, and it must not gain
 * any.
 */

#include <stdio.h>
#include <time.h>
#include <string.h>

#include "app.h"
#include "debug_log.h"
#include "runtime.h"

int adsb_tuned(const struct app *app) {
    return adsb_receiver_ready(app->applied.frequency_hz,
                               app->applied.sample_rate_hz, DEFAULT_FREQUENCY);
}

void view_adsb_defaults(struct app *app) {
    if (!app)
        return;
    memset(&app->adsb, 0, sizeof(app->adsb));
    app->adsb.selected_log = -1;
}

void enter_adsb(struct app *app) {
    if (!app->receiver_mode)
        return;
    if (adsb_tuned(app)) {
        receiver_borrow(app, &app->adsb.lease_token);
        return;
    }
    receiver_borrow_at(app, &app->adsb.lease_token, DEFAULT_FREQUENCY, 0);
}

void leave_adsb(struct app *app) {
    receiver_return(app, &app->adsb.lease_token);
}

int adsb_analysis_showing(const struct app *app) {
    return adsb_analysis_visible(app->adsb.analysis_mode, adsb_tuned(app));
}

static void adsb_format(const struct adsb_message *msg,
                        struct adsb_log_entry *entry, double now) {
    memset(entry, 0, sizeof(*entry));
    time_t wall = time(NULL);
    struct tm local;
    localtime_r(&wall, &local);
    strftime(entry->stamp, sizeof(entry->stamp), "%H:%M:%S", &local);
    snprintf(entry->icao, sizeof(entry->icao), "%06X", msg->icao);
    int pos = 0;
    for (int b = 0; b < msg->byte_count &&
                    pos + 2 < (int)sizeof(entry->raw); b++)
        pos += snprintf(entry->raw + pos, sizeof(entry->raw) - (size_t)pos,
                        "%02X", msg->bytes[b]);
    entry->time = now;
    entry->highlight = 1;
    switch (msg->kind) {
    case ADSB_KIND_IDENTIFICATION:
        snprintf(entry->label, sizeof(entry->label), "ID");
        snprintf(entry->detail, sizeof(entry->detail), "callsign %s",
                 msg->callsign);
        break;
    case ADSB_KIND_AIRBORNE_POSITION:
        snprintf(entry->label, sizeof(entry->label), "POS");
        if (msg->has_position)
            snprintf(entry->detail, sizeof(entry->detail),
                     "lat %.4f  lon %.4f  alt %d ft", msg->latitude_deg,
                     msg->longitude_deg, msg->altitude_ft);
        else if (msg->has_altitude)
            snprintf(entry->detail, sizeof(entry->detail),
                     "alt %d ft  (awaiting %s frame)", msg->altitude_ft,
                     msg->cpr_odd ? "even" : "odd");
        else
            snprintf(entry->detail, sizeof(entry->detail),
                     "position (awaiting pair)");
        break;
    case ADSB_KIND_VELOCITY:
        snprintf(entry->label, sizeof(entry->label), "VEL");
        snprintf(entry->detail, sizeof(entry->detail),
                 "%.0f kt  heading %.0f deg", msg->ground_speed_kt,
                 msg->heading_deg);
        break;
    default:
        snprintf(entry->label, sizeof(entry->label), "MSG");
        snprintf(entry->detail, sizeof(entry->detail), "DF%d TC%d",
                 msg->downlink_format, msg->type_code);
        break;
    }
}

/*
 * Feed the latest block to the decode, then log what it produced.
 *
 * Everything before the loop is `adsb_session.h`'s. The log is this view's:
 * formatting a message into a row, and fading the previous rows' highlight,
 * are about looking at frames rather than reading them.
 */
void update_adsb(struct app *app, double now) {
    int count, i;

    if (!app->frame.have_samples || app->frame.pair_count == 0)
        return;
    count = adsb_session_feed(&app->adsb.session, app->frame.magnitudes,
                              app->frame.pair_count, now);

    /* Fade the previous rows' highlight before adding new ones. */
    adsb_log_fade(app->adsb.log, app->adsb.log_count);
    for (i = 0; i < count; i++) {
        struct adsb_log_entry entry;

        adsb_format(&app->adsb.session.messages[i], &entry, now);
        adsb_log_push(app->adsb.log, &app->adsb.log_count, &entry);
        debug_log_write("adsb", "icao %s %s %s", entry.icao, entry.label, entry.detail);
    }
}
