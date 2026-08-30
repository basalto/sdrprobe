#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "view.h"
#include "gsm_layout.h"
#include "sdrgui.h"

int adsb_tuned(const struct app *app) {
    long delta = (long)app->applied_frequency - (long)DEFAULT_FREQUENCY;
    if (delta < 0)
        delta = -delta;
    return delta < 200000 && app->applied_sample_rate >= 2000000U;
}

static Rectangle adsb_retune_button(void) {
    return (Rectangle){ 470.0f, 82.0f, 220.0f, 30.0f };
}

static Rectangle adsb_log_rect(void) {
    float width = (float)GetScreenWidth();
    float top = 124.0f;
    return (Rectangle){ 82.0f, top, width - 112.0f,
                        (float)GetScreenHeight() - top - 30.0f };
}

static void adsb_log_push(struct app *app, const struct adsb_log_entry *entry) {
    int keep = app->adsb.log_count < ADSB_LOG_CAPACITY ? app->adsb.log_count
                                                       : ADSB_LOG_CAPACITY - 1;
    memmove(&app->adsb.log[1], &app->adsb.log[0],
            (size_t)keep * sizeof(app->adsb.log[0]));
    app->adsb.log[0] = *entry;
    if (app->adsb.log_count < ADSB_LOG_CAPACITY)
        app->adsb.log_count++;
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

void update_adsb(struct app *app, double now) {
    if (!app->have_samples || app->pair_count == 0)
        return;
    size_t count = adsb_demod(&app->adsb.decoder, app->magnitudes,
                              app->pair_count, now, app->adsb.scratch,
                              sizeof(app->adsb.scratch) /
                                  sizeof(app->adsb.scratch[0]));
    size_t emitted = count;
    size_t capacity = sizeof(app->adsb.scratch) / sizeof(app->adsb.scratch[0]);
    if (emitted > capacity)
        emitted = capacity;
    /* Fade the previous rows' highlight before adding new ones. */
    for (int i = 0; i < app->adsb.log_count; i++)
        app->adsb.log[i].highlight = 0;
    for (size_t i = 0; i < emitted; i++) {
        struct adsb_log_entry entry;
        adsb_format(&app->adsb.scratch[i], &entry, now);
        adsb_log_push(app, &entry);
        app->adsb.frames_total++;
        if (app->adsb.scratch[i].has_position)
            app->adsb.positions_total++;
    }
}

void handle_adsb_input(struct app *app) {
    if (IsKeyPressed(KEY_ESCAPE)) {
        set_tab(app, TAB_SCOPE);
        return;
    }
    if (!adsb_tuned(app) && app->receiver_mode &&
        clicked(adsb_retune_button())) {
        retune_receiver(app, DEFAULT_FREQUENCY, app->applied_ppm);
    }
}

void draw_adsb(struct app *app) {
    char text[160];
    snprintf(text, sizeof(text),
             "1090 MHz extended squitter   frames decoded: %llu   positions: %llu",
             (unsigned long long)app->adsb.frames_total,
             (unsigned long long)app->adsb.positions_total);
    DrawText(text, 22, 88, 17, (Color){ 187, 205, 216, 255 });

    if (!adsb_tuned(app)) {
        if (app->receiver_mode) {
            draw_button(adsb_retune_button(), "Retune to 1090 MHz", 1);
        } else {
            DrawText("Capture is not 1090 MHz / 2 MS/s; no Mode S expected",
                     470, 88, 17, (Color){ 250, 190, 74, 255 });
        }
    }

    struct sdrgui_message_log_row rows[ADSB_LOG_CAPACITY];
    for (int i = 0; i < app->adsb.log_count; i++) {
        rows[i].time = app->adsb.log[i].stamp;
        rows[i].icao = app->adsb.log[i].icao;
        rows[i].label = app->adsb.log[i].label;
        rows[i].detail = app->adsb.log[i].detail;
        rows[i].raw = app->adsb.log[i].raw;
        rows[i].highlight = app->adsb.log[i].highlight;
    }
    struct sdrgui_message_log_params params = {
        adsb_log_rect(), rows, app->adsb.log_count,
        "Decoded messages (newest first)",
        app->have_samples ? "Listening for Mode S frames..."
                          : "Waiting for samples..."
    };
    sdrgui_message_log(&params);
}

