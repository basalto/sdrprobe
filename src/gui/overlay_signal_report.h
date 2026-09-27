#ifndef OVERLAY_SIGNAL_REPORT_H
#define OVERLAY_SIGNAL_REPORT_H

#include <raylib.h>

struct app;

/*
 * Right-click context menu and signal report popup for waterfall charts.
 */

void waterfall_context_menu_open(struct app *app, Vector2 mouse,
                                 double freq_hz, double age_seconds,
                                 const char *technology);

int handle_waterfall_context_input(struct app *app);

void draw_waterfall_context(struct app *app);

void waterfall_context_close(struct app *app);

#endif /* OVERLAY_SIGNAL_REPORT_H */
