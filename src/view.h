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

/* GSM band-analysis view. */
void draw_gsm(struct app *app);
void handle_gsm_input(struct app *app);
void update_gsm_sch(struct app *app, double now);
void enter_gsm(struct app *app);
void leave_gsm(struct app *app);
void view_gsm_defaults(struct app *app);
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


/* Scope tab: the four signal views, and the GPU resources two of them keep
   between frames. render_waterfall and update_scatter are here because the
   frame loop drives them; waterfall_color and view_name stay private. */
Rectangle calculate_plot(void);
void clear_scatter(struct app *app);
int recreate_scatter(struct app *app, Rectangle plot);
int recreate_waterfall(struct app *app, Rectangle plot, int clear_history);
void render_waterfall(struct app *app);
void update_waterfall(struct app *app);
void update_scatter(struct app *app, double now, int insert);
void draw_waterfall_rect(const struct app *app, int calibration_mode,
                         Rectangle rect, double zoom_center_hz);
void draw_waterfall(const struct app *app, int calibration_mode);
void draw_base_hud(const struct app *app, const struct slot_snapshot *snapshot);
void draw_magnitude(const struct app *app);
void draw_spectrum(const struct app *app);
void draw_scatter(const struct app *app);
void view_scope_defaults(struct app *app);
int view_scope_resize_if_needed(struct app *app, Rectangle plot);
void view_scope_release(struct app *app);
void recompute_magnitude_bins(struct app *app);
void decay_spectrum_peak(struct app *app, double now);
void adjust_active_scale(struct app *app, int zoom_in);


/* Calibration overlay: the GSM 900 channel calibration, its band scan, and the
   periodic drift re-check. Drawn over whichever tab is active. */
void open_calibration(struct app *app);
void close_calibration(struct app *app);
void update_calibration_measurement(struct app *app);
void handle_calibration_input(struct app *app);
void draw_calibration(struct app *app);
void update_scan(struct app *app);
void draw_scan(struct app *app);
void calibration_select_channel(struct app *app, int arfcn);
int start_calibration(struct app *app);
void robust_center_spread(const double *values, int count,
                          double *center, double *spread);
void handle_scan_input(struct app *app);
void update_drift_check(struct app *app, int have_block);
void draw_health_indicator(const struct app *app);


/* Settings panel, and the two buttons that open it and the calibration
   overlay. */
void open_settings(struct app *app);
int apply_settings(struct app *app);
void handle_settings_input(struct app *app);
void draw_settings(const struct app *app);
Rectangle settings_button(void);
Rectangle calibration_button(void);


/* Acquisition lifecycle, in sdrprobe.c: applying settings can retune or
   restart the receiver. */
int stop_acquisition(struct app *app);
int start_acquisition(struct app *app);
int set_frequency_correction(rtlsdr_dev_t *dev, int ppm);

#endif
