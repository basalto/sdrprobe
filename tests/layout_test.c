#include "adsb_layout.h"
#include "lte_layout.h"
#include "calibration_layout.h"
#include "fm_layout.h"
#include "chrome_layout.h"
#include "gsm_layout.h"
#include "survey_layout.h"

#include "check.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Golden-rect check for the layouts: the GSM decode view, the ADS-B decode
 * view, the band survey, and the window chrome shared by every screen.
 *
 * Every rectangle, at several window sizes, pinned to the value it had when
 * the layout was consolidated into gsm_layout_for(). Layout had no test before
 * this and was tuned by eye, which is how panels ended up overlapping: a
 * change that moved one of them moved others silently.
 *
 * A failure here is not automatically a bug. It means geometry moved. If the
 * move was intended, re-bless the numbers below in the same commit that makes
 * the change, so the diff shows exactly what shifted and by how much.
 */

#define RECTS 12

struct expected {
    const char *name;
    float x, y, w, h;
};

struct window_case {
    float width, height;
    struct expected rect[RECTS];
};

static const struct window_case cases[] = {
    { 1100.0f, 720.0f, {
        { "scan_button", 22.00f, 84.00f, 150.00f, 30.00f },
        { "waterfall", 82.00f, 196.00f, 988.00f, 208.56f },
        { "scan", 82.00f, 524.56f, 818.56f, 145.44f },
        { "burst", 82.00f, 196.00f, 988.00f, 208.56f },
        { "view_toggle", 950.00f, 100.00f, 130.00f, 26.00f },
        { "constellation", 924.56f, 524.56f, 145.44f, 145.44f },
        { "const_amp_button", 936.00f, 528.56f, 62.00f, 22.00f },
        { "const_derot_button", 1002.00f, 528.56f, 64.00f, 22.00f },
        { "record_button", 182.00f, 84.00f, 130.00f, 30.00f },
        { "opt_button[0]", 396.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[1]", 468.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[2]", 540.00f, 134.00f, 66.00f, 20.00f },
    } },
    { 1280.0f, 800.0f, {
        { "scan_button", 22.00f, 84.00f, 150.00f, 30.00f },
        { "waterfall", 82.00f, 196.00f, 1168.00f, 243.76f },
        { "scan", 82.00f, 559.76f, 953.76f, 190.24f },
        { "burst", 82.00f, 196.00f, 1168.00f, 243.76f },
        { "view_toggle", 1130.00f, 100.00f, 130.00f, 26.00f },
        { "constellation", 1059.76f, 559.76f, 190.24f, 190.24f },
        { "const_amp_button", 1116.00f, 563.76f, 62.00f, 22.00f },
        { "const_derot_button", 1182.00f, 563.76f, 64.00f, 22.00f },
        { "record_button", 182.00f, 84.00f, 130.00f, 30.00f },
        { "opt_button[0]", 396.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[1]", 468.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[2]", 540.00f, 134.00f, 66.00f, 20.00f },
    } },
    { 1920.0f, 1080.0f, {
        { "scan_button", 22.00f, 84.00f, 150.00f, 30.00f },
        { "waterfall", 82.00f, 196.00f, 1808.00f, 366.96f },
        { "scan", 82.00f, 682.96f, 1436.96f, 347.04f },
        { "burst", 82.00f, 196.00f, 1808.00f, 366.96f },
        { "view_toggle", 1770.00f, 100.00f, 130.00f, 26.00f },
        { "constellation", 1542.96f, 682.96f, 347.04f, 347.04f },
        { "const_amp_button", 1756.00f, 686.96f, 62.00f, 22.00f },
        { "const_derot_button", 1822.00f, 686.96f, 64.00f, 22.00f },
        { "record_button", 182.00f, 84.00f, 130.00f, 30.00f },
        { "opt_button[0]", 396.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[1]", 468.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[2]", 540.00f, 134.00f, 66.00f, 20.00f },
    } },
    { 2560.0f, 1440.0f, {
        { "scan_button", 22.00f, 84.00f, 150.00f, 30.00f },
        { "waterfall", 82.00f, 196.00f, 2448.00f, 525.36f },
        { "scan", 82.00f, 841.36f, 1875.36f, 548.64f },
        { "burst", 82.00f, 196.00f, 2448.00f, 525.36f },
        { "view_toggle", 2410.00f, 100.00f, 130.00f, 26.00f },
        { "constellation", 1981.36f, 841.36f, 548.64f, 548.64f },
        { "const_amp_button", 2396.00f, 845.36f, 62.00f, 22.00f },
        { "const_derot_button", 2462.00f, 845.36f, 64.00f, 22.00f },
        { "record_button", 182.00f, 84.00f, 130.00f, 30.00f },
        { "opt_button[0]", 396.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[1]", 468.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[2]", 540.00f, 134.00f, 66.00f, 20.00f },
    } },
    { 1000.0f, 540.0f, {
        { "scan_button", 22.00f, 84.00f, 150.00f, 30.00f },
        { "waterfall", 82.00f, 196.00f, 888.00f, 129.36f },
        { "scan", 82.00f, 445.36f, 819.36f, 44.64f },
        { "burst", 82.00f, 196.00f, 888.00f, 129.36f },
        { "view_toggle", 850.00f, 100.00f, 130.00f, 26.00f },
        { "constellation", 925.36f, 445.36f, 44.64f, 44.64f },
        { "const_amp_button", 925.36f, 449.36f, 62.00f, 22.00f },
        { "const_derot_button", 925.36f, 449.36f, 64.00f, 22.00f },
        { "record_button", 182.00f, 84.00f, 130.00f, 30.00f },
        { "opt_button[0]", 396.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[1]", 468.00f, 134.00f, 66.00f, 20.00f },
        { "opt_button[2]", 540.00f, 134.00f, 66.00f, 20.00f },
    } },
};

static void check(float width, float height, const char *name,
                  Rectangle got, const struct expected *want) {
    const float tol = 0.01f;
    check_msg(fabsf(got.x - want->x) <= tol && fabsf(got.y - want->y) <= tol &&
                  fabsf(got.width - want->w) <= tol &&
                  fabsf(got.height - want->h) <= tol,
              "%.0fx%.0f %s: got %.2f %.2f %.2f %.2f, expected "
              "%.2f %.2f %.2f %.2f\n",
              width, height, name, got.x, got.y, got.width, got.height, want->x,
              want->y, want->w, want->h);
    (void)name;
}

struct chrome_case {
    float width, height;
    struct expected settings, calibration, tab0, tab1;
    float status_left;
};

/* The chrome buttons sit at the window's right edge and the status text runs
   up to status_left. Pinned together because they must agree: text that
   assumes the wrong edge draws underneath a button. */
static const struct chrome_case chrome_cases[] = {
    { 1100.0f, 720.0f,
      { "settings_button", 970.00f, 16.00f, 108.00f, 34.00f },
      { "calibration_button", 970.00f, 58.00f, 108.00f, 34.00f },
      { "tab[0]", 588.00f, 14.00f, 118.00f, 36.00f },
      { "tab[1]", 716.00f, 14.00f, 118.00f, 36.00f },
      958.00f },
    { 1280.0f, 800.0f,
      { "settings_button", 1150.00f, 16.00f, 108.00f, 34.00f },
      { "calibration_button", 1150.00f, 58.00f, 108.00f, 34.00f },
      { "tab[0]", 768.00f, 14.00f, 118.00f, 36.00f },
      { "tab[1]", 896.00f, 14.00f, 118.00f, 36.00f },
      1138.00f },
    { 1920.0f, 1080.0f,
      { "settings_button", 1790.00f, 16.00f, 108.00f, 34.00f },
      { "calibration_button", 1790.00f, 58.00f, 108.00f, 34.00f },
      { "tab[0]", 1408.00f, 14.00f, 118.00f, 36.00f },
      { "tab[1]", 1536.00f, 14.00f, 118.00f, 36.00f },
      1778.00f },
    { 2560.0f, 1440.0f,
      { "settings_button", 2430.00f, 16.00f, 108.00f, 34.00f },
      { "calibration_button", 2430.00f, 58.00f, 108.00f, 34.00f },
      { "tab[0]", 2048.00f, 14.00f, 118.00f, 36.00f },
      { "tab[1]", 2176.00f, 14.00f, 118.00f, 36.00f },
      2418.00f },
    { 1000.0f, 540.0f,
      { "settings_button", 870.00f, 16.00f, 108.00f, 34.00f },
      { "calibration_button", 870.00f, 58.00f, 108.00f, 34.00f },
      { "tab[0]", 488.00f, 14.00f, 118.00f, 36.00f },
      { "tab[1]", 616.00f, 14.00f, 118.00f, 36.00f },
      858.00f },
};

/* raylib's own CheckCollisionRecs would do, but this check links no raylib --
   it compiles against the headers and nothing else, which is what lets it run
   with no window. */
static int overlaps(Rectangle a, Rectangle b) {
    return a.x < b.x + b.width && a.x + a.width > b.x &&
           a.y < b.y + b.height && a.y + a.height > b.y;
}

/*
 * The calibration overlay: everything in it, against everything else.
 *
 * The previous version of this checked the LTE controls against each other and
 * nothing else, because the rest of the overlay was still literals in the draw
 * call. It passed, and what shipped had the band buttons on the status line,
 * the band caption running under the Scan button, and the cell list over three
 * rows of text. A check that covers half a screen is not half a check -- it is
 * a false negative with a green tick next to it.
 *
 * Text widths need a font and cannot be measured without a window, so what is
 * compared is the *band* each line occupies. That is what a collision is made
 * of, and it needs no font.
 */
static void check_calibration_overlay(void) {
    const float sizes[][2] = { { 1100.0f, 720.0f }, { 1280.0f, 800.0f },
                               { 1000.0f, 540.0f }, { 1920.0f, 1080.0f } };
    unsigned c;
    int lte;

    for (c = 0; c < sizeof(sizes) / sizeof(sizes[0]); c++)
    for (lte = 0; lte <= 1; lte++) {
        struct calibration_layout l =
            calibration_layout_for(sizes[c][0], sizes[c][1], lte);
        /* Eighteen in the 4G arrangement; sized with room rather than to fit,
           because a check that overruns its own array reports whatever was
           next in the stack -- which it did, as 'back runs off the side'. */
        Rectangle all[24];
        const char *names[24];
        int n = 0, a, b, i;

        all[n] = l.back;        names[n++] = "back";
        all[n] = l.exit;        names[n++] = "exit";
        for (i = 0; i < 3; i++) {
            all[n] = l.tech[i]; names[n++] = "tech";
        }
        all[n] = l.scan;        names[n++] = "scan";
        all[n] = l.channel;     names[n++] = "channel";
        all[n] = l.start;       names[n++] = "start";
        all[n] = l.apply_ppm;   names[n++] = "apply";
        if (lte) {
            for (i = 0; i < CALIBRATION_LTE_BANDS; i++) {
                all[n] = l.lte_band[i]; names[n++] = "lte band";
            }
            all[n] = l.lte_scan; names[n++] = "lte scan";
        }
        all[n] = l.band_label;  names[n++] = "band caption";
        for (i = 0; i < CALIBRATION_STATUS_ROWS; i++) {
            all[n] = l.status[i]; names[n++] = "status";
        }
        all[n] = l.chart;       names[n++] = "chart";

        for (a = 0; a < n; a++)
            for (b = a + 1; b < n; b++)
                if (all[a].width > 0.0f && all[b].width > 0.0f &&
                    overlaps(all[a], all[b]))
                    check_msg(0, "%.0fx%.0f %s: '%s' overlaps '%s'\n",
                              sizes[c][0], sizes[c][1], lte ? "4G" : "2G",
                              names[a], names[b]);

        /* Everything drawn sits inside the window. */
        for (a = 0; a < n; a++) {
            if (all[a].width <= 0.0f)
                continue;
            check_msg(all[a].x >= 0.0f &&
                      all[a].x + all[a].width <= sizes[c][0] + 0.01f,
                      "%.0fx%.0f %s: '%s' runs off the side\n", sizes[c][0],
                      sizes[c][1], lte ? "4G" : "2G", names[a]);
            check_msg(all[a].y + all[a].height <= sizes[c][1] + 0.01f,
                      "%.0fx%.0f %s: '%s' runs off the bottom\n", sizes[c][0],
                      sizes[c][1], lte ? "4G" : "2G", names[a]);
        }

        /* The chart is last, below every row, or a row is drawn over it. */
        for (a = 0; a + 1 < n; a++)
            if (all[a].width > 0.0f)
                check_msg(all[a].y < l.chart.y + 0.01f,
                          "%.0fx%.0f %s: '%s' is below the chart's top\n",
                          sizes[c][0], sizes[c][1], lte ? "4G" : "2G",
                          names[a]);

        /* Selecting 4G adds a row, so everything under it must move down. */
        if (lte) {
            struct calibration_layout gsm =
                calibration_layout_for(sizes[c][0], sizes[c][1], 0);
            check_msg(l.status[0].y > gsm.status[0].y,
                      "%.0fx%.0f the 4G row does not push the status down\n",
                      sizes[c][0], sizes[c][1]);
            check_msg(l.chart.y > gsm.chart.y,
                      "%.0fx%.0f the 4G row does not push the chart down\n",
                      sizes[c][0], sizes[c][1]);
        }

        /*
         * And the picker's rows map back to themselves. Getting this wrong
         * calibrates the receiver against a cell the operator did not choose,
         * and the number it produces looks perfectly reasonable.
         */
        {
            int rows = calibration_cell_rows(l.cell_list);
            int r;
            check_msg(rows >= 4, "%.0fx%.0f the picker shows only %d rows\n",
                      sizes[c][0], sizes[c][1], rows);
            for (r = 0; r < rows; r++) {
                Vector2 mid;
                mid.x = l.cell_list.x + l.cell_list.width / 2.0f;
                mid.y = l.cell_list.y + 30.0f +
                        CALIBRATION_CELL_ROW_H * ((float)r + 0.5f);
                check_msg(calibration_cell_row_at(l.cell_list, rows, mid) == r,
                          "%.0fx%.0f picker row %d of %d does not map back\n",
                          sizes[c][0], sizes[c][1], r, rows);
            }
            check_msg(calibration_cell_row_at(l.cell_list, 4,
                          (Vector2){ l.cell_list.x + 10.0f,
                                     l.cell_list.y + 10.0f }) == -1,
                      "%.0fx%.0f the picker's caption strip selects a row\n",
                      sizes[c][0], sizes[c][1]);
            check_msg(calibration_cell_row_at(l.cell_list, 0,
                          (Vector2){ l.cell_list.x + 10.0f,
                                     l.cell_list.y + 40.0f }) == -1,
                      "%.0fx%.0f an empty picker selects a row\n",
                      sizes[c][0], sizes[c][1]);
        }
    }
}

static void check_chrome(void) {
    for (unsigned c = 0; c < sizeof(chrome_cases) / sizeof(chrome_cases[0]); c++) {
        const struct chrome_case *w = &chrome_cases[c];
        struct chrome_layout l = chrome_layout_for(w->width, w->height);
        check(w->width, w->height, "settings_button", l.settings_button, &w->settings);
        check(w->width, w->height, "calibration_button", l.calibration_button,
              &w->calibration);
        check(w->width, w->height, "tab[0]", l.tab[0], &w->tab0);
        check(w->width, w->height, "tab[1]", l.tab[1], &w->tab1);
        check_msg(fabsf(l.status_left - w->status_left) <= 0.01f,
                  "%.0fx%.0f status_left: got %.2f, expected %.2f\n", w->width,
                  w->height, l.status_left, w->status_left);
        /*
         * Survey sits at the left, where the numbered options used to begin,
         * so the options have to begin after it. The same window that has to
         * hold both at 1000 px holds the tabs and the right-hand buttons too.
         */
        {
            struct expected survey = { "survey_button", 22.00f, 44.00f,
                                       104.00f, 28.00f };
            check(w->width, w->height, "survey_button", l.survey_button,
                  &survey);
            check_msg(l.option_row_left >=
                          l.survey_button.x + l.survey_button.width,
                      "%.0fx%.0f the numbered options start on the Survey "
                      "button\n", w->width, w->height);
            /* And the button's own line clears the title above it. */
            check_msg(l.survey_button.y >= 38.0f,
                      "%.0fx%.0f the Survey button lands on the title\n",
                      w->width, w->height);
            check_msg(l.option_row_y >= l.survey_button.y &&
                      l.option_row_y <=
                          l.survey_button.y + l.survey_button.height,
                      "%.0fx%.0f the options and the Survey button are not on "
                      "the same line\n", w->width, w->height);
            /* It must not run under the tabs on the narrowest window. */
            check_msg(l.survey_button.x + l.survey_button.width < l.tab[0].x,
                      "%.0fx%.0f the Survey button reaches the tab bar\n",
                      w->width, w->height);
        }
        /*
         * The two calibration circles. One per reference, and two widgets each
         * choosing their own position would be two widgets that can land on
         * each other -- which is why the position is here and not inside the
         * widget.
         */
        {
            float gap = l.lte_dot.y - l.gsm_dot.y;
            check_msg(gap >= 18.0f,
                      "%.0fx%.0f the calibration dots overlap: %.0f px apart\n",
                      w->width, w->height, gap);
            check_msg(l.gsm_dot.x == l.lte_dot.x,
                      "%.0fx%.0f the calibration dots are not in a column\n",
                      w->width, w->height);
            /* Both clear of the buttons they sit beside, captions and all. */
            check_msg(l.gsm_dot.x + 12.0f < l.settings_button.x,
                      "%.0fx%.0f a calibration dot reaches the buttons\n",
                      w->width, w->height);
            check_msg(l.lte_dot.y + 12.0f <= l.calibration_button.y +
                          l.calibration_button.height,
                      "%.0fx%.0f the LTE dot hangs below the button column\n",
                      w->width, w->height);
        }
    }
}

/* The ADS-B view, whose analysis mode packs three charts over a log and a
   square scatter. The 1000x540 case is the app's minimum window: the GSM
   layout's comment records that a panel that small pushes its own button out
   of itself, so hold_button is pinned there too. */
#define ADSB_RECTS 10

struct adsb_case {
    float width, height;
    struct expected rect[ADSB_RECTS];
    float header_left;
    float header_right;
};

static const struct adsb_case adsb_cases[] = {
    { 1100.0f, 720.0f, {
        { "record_button", 810.00f, 100.00f, 130.00f, 26.00f },
        { "retune_button", 470.00f, 82.00f, 220.00f, 30.00f },
        { "view_toggle", 950.00f, 100.00f, 130.00f, 26.00f },
        { "hold_button", 954.00f, 400.28f, 112.00f, 22.00f },
        { "chart[0]", 82.00f, 156.00f, 320.00f, 224.28f },
        { "chart[1]", 416.00f, 156.00f, 320.00f, 224.28f },
        { "chart[2]", 750.00f, 156.00f, 320.00f, 224.28f },
        { "log_full", 82.00f, 138.00f, 988.00f, 552.00f },
        { "log_split", 82.00f, 396.28f, 670.28f, 293.72f },
        { "scatter", 776.28f, 396.28f, 293.72f, 293.72f },
    }, 22.00f, 798.00f },
    { 1280.0f, 800.0f, {
        { "record_button", 990.00f, 100.00f, 130.00f, 26.00f },
        { "retune_button", 470.00f, 82.00f, 220.00f, 30.00f },
        { "view_toggle", 1130.00f, 100.00f, 130.00f, 26.00f },
        { "hold_button", 1134.00f, 433.88f, 112.00f, 22.00f },
        { "chart[0]", 82.00f, 156.00f, 380.00f, 257.88f },
        { "chart[1]", 476.00f, 156.00f, 380.00f, 257.88f },
        { "chart[2]", 870.00f, 156.00f, 380.00f, 257.88f },
        { "log_full", 82.00f, 138.00f, 1168.00f, 632.00f },
        { "log_split", 82.00f, 429.88f, 803.88f, 340.12f },
        { "scatter", 909.88f, 429.88f, 340.12f, 340.12f },
    }, 22.00f, 978.00f },
    { 1000.0f, 540.0f, {
        { "record_button", 710.00f, 100.00f, 130.00f, 26.00f },
        { "retune_button", 470.00f, 82.00f, 220.00f, 30.00f },
        { "view_toggle", 850.00f, 100.00f, 130.00f, 26.00f },
        { "hold_button", 854.00f, 324.68f, 112.00f, 22.00f },
        { "chart[0]", 82.00f, 156.00f, 286.67f, 148.68f },
        { "chart[1]", 382.67f, 156.00f, 286.67f, 148.68f },
        { "chart[2]", 683.33f, 156.00f, 286.67f, 148.68f },
        { "log_full", 82.00f, 138.00f, 888.00f, 372.00f },
        { "log_split", 82.00f, 320.68f, 674.68f, 189.32f },
        { "scatter", 780.68f, 320.68f, 189.32f, 189.32f },
    }, 22.00f, 698.00f },
};

static void check_adsb(void) {
    for (unsigned c = 0; c < sizeof(adsb_cases) / sizeof(adsb_cases[0]); c++) {
        const struct adsb_case *w = &adsb_cases[c];
        struct adsb_layout l = adsb_layout_for(w->width, w->height);
        Rectangle got[ADSB_RECTS] = {
            l.record_button, l.retune_button, l.view_toggle, l.hold_button,
            l.chart[0], l.chart[1], l.chart[2], l.log_full, l.log_split,
            l.scatter
        };
        for (int i = 0; i < ADSB_RECTS; i++)
            check(w->width, w->height, w->rect[i].name, got[i], &w->rect[i]);
        check_msg(fabsf(l.header_left - w->header_left) <= 0.01f,
                  "%.0fx%.0f header_left: got %.2f, expected %.2f\n", w->width,
                  w->height, l.header_left, w->header_left);
        /* The header text has to stop before the buttons on its rows, which
           is the property header_right exists for. */
        check_msg(l.header_right <= l.record_button.x,
                  "%.0fx%.0f header text runs under record_button\n", w->width,
                  w->height);
        check_msg(l.record_button.x + l.record_button.width <= l.view_toggle.x,
                  "%.0fx%.0f record_button runs into view_toggle\n", w->width,
                  w->height);
        /* Both lower panels start on the same row and end on the same one, so
           the log and the scatter read as a pair rather than as two panels
           that happen to be adjacent. */
        check_msg(fabsf(l.log_split.y - l.scatter.y) <= 0.01f &&
                      fabsf((l.log_split.y + l.log_split.height) -
                            (l.scatter.y + l.scatter.height)) <= 0.01f,
                  "%.0fx%.0f log_split and scatter are not aligned\n", w->width,
                  w->height);
        check_msg(fabsf(l.header_right - w->header_right) <= 0.01f,
                  "%.0fx%.0f header_right: got %.2f, expected %.2f\n", w->width,
                  w->height, l.header_right, w->header_right);
        /* The two panels that share the lower row must not overlap, whatever
           the numbers above say -- that is the property the pins are there to
           protect, and it is worth stating once rather than inferring. */
        check_msg(l.log_split.x + l.log_split.width <= l.scatter.x + 0.01f,
                  "%.0fx%.0f log_split runs into scatter\n", w->width,
                  w->height);
        check_msg(l.hold_button.x >= l.scatter.x - 0.01f &&
                      l.hold_button.x + l.hold_button.width <=
                          l.scatter.x + l.scatter.width + 0.01f,
                  "%.0fx%.0f hold_button escapes the scatter\n", w->width,
                  w->height);
    }
}

/* The band survey. Its lower row is a pair like the ADS-B view's, and its
   inspect button lives inside a panel that a short window shrinks. */
#define SURVEY_RECTS 19

struct survey_case {
    float width, height;
    struct expected rect[SURVEY_RECTS];
    float header_left;
    float header_right;
};

static const struct survey_case survey_cases[] = {
    { 1100.0f, 720.0f, {
        { "from_field", 82.00f, 128.00f, 150.00f, 30.00f },
        { "to_field", 248.00f, 128.00f, 150.00f, 30.00f },
        { "dwell_field", 414.00f, 128.00f, 100.00f, 30.00f },
        { "sweep_button", 530.00f, 128.00f, 120.00f, 30.00f },
        { "reset_button", 666.00f, 128.00f, 130.00f, 30.00f },
        { "stop_button", 812.00f, 128.00f, 90.00f, 30.00f },
        { "site_field", 82.00f, 180.00f, 164.00f, 30.00f },
        { "site_menu_button", 248.00f, 180.00f, 26.00f, 30.00f },
        { "antenna_field", 288.00f, 180.00f, 180.00f, 30.00f },
        { "antenna_menu_button", 470.00f, 180.00f, 26.00f, 30.00f },
        { "save_button", 510.00f, 180.00f, 120.00f, 30.00f },
        { "confirm_button", 646.00f, 180.00f, 150.00f, 30.00f },
        { "watch_button", 812.00f, 180.00f, 110.00f, 30.00f },
        { "chart", 82.00f, 248.00f, 988.00f, 198.90f },
        { "peak_list", 82.00f, 472.90f, 414.96f, 217.10f },
        { "detail", 516.96f, 472.90f, 553.04f, 217.10f },
        { "scan_button", 528.96f, 614.00f, 258.52f, 28.00f },
        { "waterfall_button", 799.48f, 614.00f, 258.52f, 28.00f },
        { "inspect_button", 528.96f, 650.00f, 529.04f, 28.00f },
    }, 82.00f, 950.00f },
    { 1280.0f, 800.0f, {
        { "from_field", 82.00f, 128.00f, 150.00f, 30.00f },
        { "to_field", 248.00f, 128.00f, 150.00f, 30.00f },
        { "dwell_field", 414.00f, 128.00f, 100.00f, 30.00f },
        { "sweep_button", 530.00f, 128.00f, 120.00f, 30.00f },
        { "reset_button", 666.00f, 128.00f, 130.00f, 30.00f },
        { "stop_button", 812.00f, 128.00f, 90.00f, 30.00f },
        { "site_field", 82.00f, 180.00f, 164.00f, 30.00f },
        { "site_menu_button", 248.00f, 180.00f, 26.00f, 30.00f },
        { "antenna_field", 288.00f, 180.00f, 180.00f, 30.00f },
        { "antenna_menu_button", 470.00f, 180.00f, 26.00f, 30.00f },
        { "save_button", 510.00f, 180.00f, 120.00f, 30.00f },
        { "confirm_button", 646.00f, 180.00f, 150.00f, 30.00f },
        { "watch_button", 812.00f, 180.00f, 110.00f, 30.00f },
        { "chart", 82.00f, 248.00f, 1168.00f, 234.90f },
        { "peak_list", 82.00f, 508.90f, 490.56f, 261.10f },
        { "detail", 592.56f, 508.90f, 657.44f, 261.10f },
        { "scan_button", 604.56f, 694.00f, 310.72f, 28.00f },
        { "waterfall_button", 927.28f, 694.00f, 310.72f, 28.00f },
        { "inspect_button", 604.56f, 730.00f, 633.44f, 28.00f },
    }, 82.00f, 1130.00f },
    { 1000.0f, 540.0f, {
        { "from_field", 82.00f, 128.00f, 150.00f, 30.00f },
        { "to_field", 248.00f, 128.00f, 150.00f, 30.00f },
        { "dwell_field", 414.00f, 128.00f, 100.00f, 30.00f },
        { "sweep_button", 530.00f, 128.00f, 120.00f, 30.00f },
        { "reset_button", 666.00f, 128.00f, 130.00f, 30.00f },
        { "stop_button", 812.00f, 128.00f, 90.00f, 30.00f },
        { "site_field", 82.00f, 180.00f, 164.00f, 30.00f },
        { "site_menu_button", 248.00f, 180.00f, 26.00f, 30.00f },
        { "antenna_field", 288.00f, 180.00f, 180.00f, 30.00f },
        { "antenna_menu_button", 470.00f, 180.00f, 26.00f, 30.00f },
        { "save_button", 510.00f, 180.00f, 120.00f, 30.00f },
        { "confirm_button", 646.00f, 180.00f, 150.00f, 30.00f },
        { "watch_button", 812.00f, 180.00f, 110.00f, 30.00f },
        { "chart", 82.00f, 248.00f, 888.00f, 117.90f },
        { "peak_list", 82.00f, 391.90f, 372.96f, 118.10f },
        { "detail", 474.96f, 391.90f, 495.04f, 118.10f },
        { "scan_button", 486.96f, 434.00f, 229.52f, 28.00f },
        { "waterfall_button", 728.48f, 434.00f, 229.52f, 28.00f },
        { "inspect_button", 486.96f, 470.00f, 471.04f, 28.00f },
    }, 82.00f, 850.00f },
};

static void check_survey(void) {
    for (unsigned c = 0; c < sizeof(survey_cases) / sizeof(survey_cases[0]);
         c++) {
        const struct survey_case *w = &survey_cases[c];
        struct survey_layout l = survey_layout_for(w->width, w->height);
        Rectangle got[SURVEY_RECTS] = {
            l.from_field, l.to_field, l.dwell_field, l.sweep_button,
            l.reset_button, l.stop_button,
            l.site_field, l.site_menu_button, l.antenna_field,
            l.antenna_menu_button, l.save_button,
            l.confirm_button, l.watch_button,
            l.chart, l.peak_list, l.detail,
            l.scan_button, l.waterfall_button, l.inspect_button
        };
        for (int i = 0; i < SURVEY_RECTS; i++)
            check(w->width, w->height, w->rect[i].name, got[i], &w->rect[i]);
        check_msg(fabsf(l.header_left - w->header_left) <= 0.01f &&
                      fabsf(l.header_right - w->header_right) <= 0.01f,
                  "%.0fx%.0f survey header bounds moved\n", w->width,
                  w->height);
        /* The properties the numbers protect. */
        check_msg(l.peak_list.x + l.peak_list.width <= l.detail.x + 0.01f,
                  "%.0fx%.0f peak_list runs into detail\n", w->width,
                  w->height);
        check_msg(fabsf(l.peak_list.y - l.detail.y) <= 0.01f &&
                      fabsf((l.peak_list.y + l.peak_list.height) -
                            (l.detail.y + l.detail.height)) <= 0.01f,
                  "%.0fx%.0f the lower panels are not aligned\n", w->width,
                  w->height);
        check_msg(l.scan_button.x + l.scan_button.width <=
                      l.waterfall_button.x + 0.01f,
                  "%.0fx%.0f the panel's upper buttons overlap\n", w->width,
                  w->height);
        /* The handoff sits on its own row, below the pair. */
        check_msg(l.inspect_button.y >=
                          l.scan_button.y + l.scan_button.height ||
                      l.detail.height <= 80.0f,
                  "%.0fx%.0f inspect_button overlaps the row above\n", w->width,
                  w->height);
        Rectangle panel_buttons[3] = { l.scan_button, l.waterfall_button,
                                       l.inspect_button };
        for (int b = 0; b < 3; b++) {
            if (panel_buttons[b].x < l.detail.x ||
                panel_buttons[b].x + panel_buttons[b].width >
                    l.detail.x + l.detail.width + 0.01f ||
                panel_buttons[b].y < l.detail.y ||
                panel_buttons[b].y + panel_buttons[b].height >
                    l.detail.y + l.detail.height + 0.01f) {
                check_msg(0, "%.0fx%.0f panel button %d escapes the panel\n",
                          w->width, w->height, b);
            }
        }
        if (l.inspect_button.x < l.detail.x ||
            l.inspect_button.x + l.inspect_button.width >
                l.detail.x + l.detail.width + 0.01f ||
            l.inspect_button.y < l.detail.y ||
            l.inspect_button.y + l.inspect_button.height >
                l.detail.y + l.detail.height + 0.01f) {
            check_msg(0, "%.0fx%.0f inspect_button escapes the panel\n",
                      w->width, w->height);
        }
        Rectangle row[6] = { l.from_field, l.to_field, l.dwell_field,
                             l.sweep_button, l.reset_button, l.stop_button };
        for (int i = 1; i < 6; i++) {
            check_msg(row[i].x >= row[i - 1].x + row[i - 1].width,
                      "%.0fx%.0f control %d overlaps the one before\n",
                      w->width, w->height, i);
        }
        /* The second row: where the sweep will be recorded, and the button
           that records it. */
        Rectangle second[7] = { l.site_field, l.site_menu_button,
                                l.antenna_field, l.antenna_menu_button,
                                l.save_button, l.confirm_button,
                                l.watch_button };
        for (int i = 0; i < 7; i++) {
            check_msg(i == 0 ||
                      second[i].x >= second[i - 1].x + second[i - 1].width,
                      "%.0fx%.0f second-row control %d overlaps\n",
                      w->width, w->height, i);
            check_msg(second[i].y >= l.from_field.y + l.from_field.height,
                      "%.0fx%.0f second-row control %d sits on the first row\n",
                      w->width, w->height, i);
            check_msg(second[i].y + second[i].height <= l.status_y,
                      "%.0fx%.0f second-row control %d runs into the status\n",
                      w->width, w->height, i);
            check_msg(second[i].x + second[i].width <= w->width,
                      "%.0fx%.0f second-row control %d runs off the edge\n",
                      w->width, w->height, i);
        }
        /*
         * The site list. It is drawn over the chart, so a hit test one row out
         * silently selects a place the operator did not point at -- and a
         * wrong site is worse than no site, because it files a sweep under
         * somewhere it was not taken.
         */
        Rectangle pickers[2] = { l.site_field, l.antenna_field };
        for (int pick = 0; pick < 2; pick++)
        for (int n = 1; n <= 6; n++) {
            Rectangle field = pickers[pick];
            Rectangle menu = survey_menu_rect(field, n);
            check_msg(menu.y >= field.y + field.height,
                      "%.0fx%.0f picker with %d rows covers its own field\n",
                      w->width, w->height, n);
            check_msg(menu.width >= field.width,
                      "%.0fx%.0f site menu with %d rows is narrower than the "
                      "field\n", w->width, w->height, n);
            /* Every row maps back to itself: the point in the middle of where
               row r is drawn must select row r. */
            for (int r = 0; r < n; r++) {
                Vector2 mid;
                mid.x = menu.x + menu.width / 2.0f;
                mid.y = menu.y + 4.0f + SURVEY_SITE_ROW_H * ((float)r + 0.5f);
                check_msg(survey_menu_row_at(field, n, mid) == r,
                          "%.0fx%.0f site menu row %d of %d does not map back "
                          "to itself\n", w->width, w->height, r, n);
            }
            /* And nothing outside it selects anything. */
            check_msg(survey_menu_row_at(field, n,
                          (Vector2){ menu.x - 4.0f, menu.y + 10.0f }) == -1,
                      "%.0fx%.0f a point left of the site menu selects a row\n",
                      w->width, w->height);
            check_msg(survey_menu_row_at(field, n,
                          (Vector2){ menu.x + 10.0f,
                                     menu.y + menu.height + 4.0f }) == -1,
                      "%.0fx%.0f a point below the site menu selects a row\n",
                      w->width, w->height);
        }
        check_msg(survey_menu_row_at(l.site_field, 0,
                      (Vector2){ l.site_field.x + 10.0f,
                                 l.site_field.y + 40.0f }) == -1,
                  "%.0fx%.0f an empty site menu selects a row\n",
                  w->width, w->height);

        /*
         * Nothing written may land on anything drawn.
         *
         * This is the check that was missing. check-layout compared the
         * rectangles the layout returned and was blind to text, so a caption
         * strip could sit on the row above it and the sweep's progress line
         * could be drawn at a literal coordinate that became the caption
         * strip. Both happened. Text is geometry, so the layout owns its
         * baselines and they are compared here like anything else.
         */
        {
            Rectangle controls[13] = {
                l.from_field, l.to_field, l.dwell_field, l.sweep_button,
                l.reset_button, l.stop_button, l.site_field,
                l.site_menu_button, l.antenna_field, l.antenna_menu_button,
                l.save_button, l.confirm_button, l.watch_button
            };
            /* Each caption, as the box it actually occupies. */
            Rectangle captioned[5] = { l.from_field, l.to_field, l.dwell_field,
                                       l.site_field, l.antenna_field };
            int a, b;
            for (a = 0; a < 5; a++) {
                Rectangle caption;
                caption.x = captioned[a].x;
                caption.y = captioned[a].y - l.label_offset;
                caption.width = captioned[a].width;
                caption.height = l.label_height;
                for (b = 0; b < 13; b++) {
                    if (overlaps(caption, controls[b]))
                        check_msg(0, "%.0fx%.0f a caption lands on control %d\n",
                                  w->width, w->height, b);
                }
            }
            /* And the status line, which doubles as the sweep's progress
               readout: one line, and for a while it had two coordinates. */
            {
                Rectangle status;
                status.x = l.header_left;
                status.y = l.status_y;
                status.width = l.header_right - l.header_left;
                status.height = 17.0f;
                for (b = 0; b < 13; b++)
                    if (overlaps(status, controls[b]))
                        check_msg(0, "%.0fx%.0f the status line lands on "
                                  "control %d\n", w->width, w->height, b);
                check_msg(status.y + status.height <= l.chart.y,
                          "%.0fx%.0f the status line lands on the chart\n",
                          w->width, w->height);
            }
        }

        /* And the status still clears the chart it sits above. */
        check_msg(l.status_y + 17.0f <= l.chart.y,
                  "%.0fx%.0f the status line overlaps the chart\n",
                  w->width, w->height);
        check_msg(row[5].x + row[5].width <= w->width,
                  "%.0fx%.0f the control row runs off the window\n", w->width,
                  w->height);
        check_msg(l.chart.y + l.chart.height <= l.peak_list.y + 0.01f,
                  "%.0fx%.0f the chart overlaps the row below\n", w->width,
                  w->height);
    }
}


/*
 * The LTE view is pinned by property rather than by number.
 *
 * Its three panels are a row -- what a scan found, what the synchronisation
 * signals found, and what the broadcast said -- and what must hold at every
 * window size is that they read as one: same top, same bottom, in order, no
 * overlap, and all of them under the waterfall and inside the window. Pinning
 * coordinates would say the same thing less directly and would have to be
 * rewritten whenever a margin moved.
 */
static void check_lte(void) {
    static const struct { float width, height; } sizes[] = {
        { 1100.0f, 720.0f }, { 1000.0f, 540.0f }, { 1600.0f, 900.0f },
        { 640.0f, 400.0f }, { 480.0f, 320.0f }
    };
    for (unsigned c = 0; c < sizeof(sizes) / sizeof(sizes[0]); c++) {
        float w = sizes[c].width, h = sizes[c].height;
        struct lte_layout l = lte_layout_for(w, h);
        Rectangle row[3] = { l.found_panel, l.cell_panel, l.mib_panel };

        for (int i = 0; i < 3; i++) {
            check_msg(fabsf(row[i].y - row[0].y) <= 0.01f &&
                          fabsf(row[i].height - row[0].height) <= 0.01f,
                      "%.0fx%.0f: panel %d is not on the panel row\n", w, h, i);
            check_msg(row[i].width > 0.0f && row[i].height > 0.0f,
                      "%.0fx%.0f: panel %d collapsed\n", w, h, i);
        }
        for (int i = 0; i + 1 < 3; i++)
            check_msg(row[i].x + row[i].width <= row[i + 1].x + 0.01f,
                      "%.0fx%.0f: panel %d runs into panel %d\n", w, h, i,
                      i + 1);
        check_msg(l.waterfall.y + l.waterfall.height <= row[0].y + 0.01f,
                  "%.0fx%.0f: the waterfall runs into the panels\n", w, h);
        /* The waterfall spans the row, so the screen reads as one column of
           content rather than two that happen to be stacked. */
        check_msg(fabsf(l.waterfall.x - l.found_panel.x) <= 0.01f,
                  "%.0fx%.0f: the waterfall does not start with the row\n",
                  w, h);
        check_msg(fabsf((l.waterfall.x + l.waterfall.width) -
                        (l.mib_panel.x + l.mib_panel.width)) <= 0.01f,
                  "%.0fx%.0f: the waterfall does not end with the row\n",
                  w, h);
        /* The header text must stop before the button on its row, and the
           band buttons must not run into the scan button beside them. */
        check_msg(l.header_right <= l.record_button.x,
                  "%.0fx%.0f: header text runs under the record button\n",
                  w, h);
        for (int i = 0; i < LTE_LAYOUT_BANDS; i++) {
            Rectangle next = (i + 1 < LTE_LAYOUT_BANDS) ? l.band_button[i + 1]
                                                        : l.scan_button;
            check_msg(l.band_button[i].x + l.band_button[i].width <= next.x,
                      "%.0fx%.0f: band button %d runs into the next\n", w, h,
                      i);
        }
        /* Analysis mode reuses the same space: three charts on the row the
           waterfall had, and a square constellation below them. */
        for (int i = 0; i < 3; i++) {
            check_msg(fabsf(l.chart[i].y - l.chart[0].y) <= 0.01f,
                      "%.0fx%.0f: chart %d is off the chart row\n", w, h, i);
            if (i + 1 < 3)
                check_msg(l.chart[i].x + l.chart[i].width <= l.chart[i + 1].x,
                          "%.0fx%.0f: chart %d runs into chart %d\n", w, h, i,
                          i + 1);
        }
        check_msg(fabsf(l.constellation.width - l.constellation.height) <=
                      0.01f,
                  "%.0fx%.0f: the constellation is not square\n", w, h);
        check_msg(l.constellation.y >= l.chart[0].y + l.chart[0].height,
                  "%.0fx%.0f: the constellation runs into the charts\n", w, h);
        check_msg(l.constellation.x + l.constellation.width <=
                      l.waterfall.x + l.waterfall.width + 0.01f,
                  "%.0fx%.0f: the constellation escapes the content\n", w, h);
    }
}


/*
 * The FM decode view: three panels in a row under a waterfall, and a controls
 * strip above it. Same all-against-all as the calibration overlay, and for
 * the same reason -- a header that models half a screen puts a green tick
 * over the half it does not model.
 */
static void check_fm_view(void) {
    const float sizes[][2] = { { 1100.0f, 720.0f }, { 1280.0f, 800.0f },
                               { 1000.0f, 540.0f }, { 1920.0f, 1080.0f } };
    unsigned c;

    for (c = 0; c < sizeof(sizes) / sizeof(sizes[0]); c++) {
        struct fm_layout l = fm_layout_for(sizes[c][0], sizes[c][1]);
        Rectangle all[12];
        const char *names[12];
        int n = 0, a, b, i;

        all[n] = l.frequency_field; names[n++] = "frequency";
        all[n] = l.tune_button;     names[n++] = "tune";
        for (i = 0; i < FM_LAYOUT_STATIONS; i++) {
            all[n] = l.station_button[i]; names[n++] = "preset";
        }
        all[n] = l.waterfall;       names[n++] = "waterfall";
        all[n] = l.signal_panel;    names[n++] = "signal";
        all[n] = l.station_panel;   names[n++] = "station";
        all[n] = l.funnel_panel;    names[n++] = "funnel";

        for (a = 0; a < n; a++)
            for (b = a + 1; b < n; b++)
                check_msg(!overlaps(all[a], all[b]),
                          "%.0fx%.0f: '%s' overlaps '%s'\n", sizes[c][0],
                          sizes[c][1], names[a], names[b]);

        for (a = 0; a < n; a++) {
            check_msg(all[a].x >= 0.0f &&
                      all[a].x + all[a].width <= sizes[c][0] + 0.01f,
                      "%.0fx%.0f: '%s' runs off the side\n", sizes[c][0],
                      sizes[c][1], names[a]);
            check_msg(all[a].y + all[a].height <= sizes[c][1] + 0.01f,
                      "%.0fx%.0f: '%s' runs off the bottom\n", sizes[c][0],
                      sizes[c][1], names[a]);
            check_msg(all[a].width > 0.0f && all[a].height > 0.0f,
                      "%.0fx%.0f: '%s' has no area\n", sizes[c][0],
                      sizes[c][1], names[a]);
        }

        /* The three panels share a row: same top, same height, or one of them
           is a different shape from its neighbours for no reason a reader can
           see. */
        check_msg(l.signal_panel.y == l.station_panel.y &&
                  l.station_panel.y == l.funnel_panel.y,
                  "%.0fx%.0f: the three panels are not on one row\n",
                  sizes[c][0], sizes[c][1]);
        check_msg(l.signal_panel.height == l.funnel_panel.height,
                  "%.0fx%.0f: the panels are different heights\n",
                  sizes[c][0], sizes[c][1]);
        /* And they are below the waterfall, not over it. */
        check_msg(l.signal_panel.y >= l.waterfall.y + l.waterfall.height,
                  "%.0fx%.0f: the panels start above the waterfall's bottom\n",
                  sizes[c][0], sizes[c][1]);
        /* The controls clear the chrome the header owns down to y = 92. */
        check_msg(l.frequency_field.y >= 92.0f,
                  "%.0fx%.0f: the controls are under the header chrome\n",
                  sizes[c][0], sizes[c][1]);
    }
}

int main(void) {
    check_chrome();
    check_calibration_overlay();
    check_fm_view();
    check_adsb();
    check_lte();
    check_survey();
    for (unsigned c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
        float w = cases[c].width, h = cases[c].height;
        struct gsm_layout l = gsm_layout_for(w, h);
        const struct expected *e = cases[c].rect;
        Rectangle got[RECTS] = {
            l.scan_button, l.waterfall, l.scan, l.burst, l.view_toggle,
            l.constellation, l.const_amp_button, l.const_derot_button,
            l.record_button, l.opt_button[0], l.opt_button[1], l.opt_button[2]
        };
        for (int i = 0; i < RECTS; i++)
            check(w, h, e[i].name, got[i], &e[i]);
    }
    return check_report("view layout");
}
