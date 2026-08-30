#ifndef VIEW_H
#define VIEW_H

#include <raylib.h>

#include "app.h"

/*
 * The Decode tab's screens, one file each, plus the few helpers they share
 * with the rest of the application.
 *
 * A view is state-to-pixels and clicks-to-state: it reads `struct app`, draws,
 * and handles its own input. It owns no state of its own, which is the honest
 * limit of this split -- see the note in app.h.
 */

/* Shared widgets and actions, defined in sdrprobe.c. */
int clicked(Rectangle rectangle);
void draw_button(Rectangle rectangle, const char *label, int primary);
int retune_receiver(struct app *app, uint32_t frequency, int ppm);
void set_tab(struct app *app, int new_tab);
void adjust_waterfall_scale(struct app *app, int zoom_in);
int scan_strongest_arfcn(const struct app *app);
int scan_strongest_bcch(const struct app *app);
int start_scan(struct app *app);
int compare_double(const void *left, const void *right);
void draw_waterfall_rect(const struct app *app, int calibration_mode,
                                Rectangle rect, double zoom_center_hz);

/* GSM band-analysis view. */
void draw_gsm(struct app *app);
void handle_gsm_input(struct app *app);
void update_gsm_sch(struct app *app, double now);
void enter_gsm(struct app *app);
void leave_gsm(struct app *app);
void start_record(struct app *app);
void gsm_tune_selected(struct app *app, int arfcn);
Rectangle gsm_scan_rect(void);
Rectangle gsm_waterfall_rect(void);
Rectangle gsm_burst_rect(void);

/* Mode S / ADS-B view. */
void draw_adsb(struct app *app);
void handle_adsb_input(struct app *app);
void update_adsb(struct app *app, double now);
int adsb_tuned(const struct app *app);

#endif
