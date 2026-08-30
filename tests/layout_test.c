#include "chrome_layout.h"
#include "gsm_layout.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * Golden-rect check for the layouts: the GSM decode view, and the window
 * chrome shared by every screen.
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

static int failures;

static void check(float width, float height, const char *name,
                  Rectangle got, const struct expected *want) {
    const float tol = 0.01f;
    if (fabsf(got.x - want->x) > tol || fabsf(got.y - want->y) > tol ||
        fabsf(got.width - want->w) > tol || fabsf(got.height - want->h) > tol) {
        fprintf(stderr,
                "%.0fx%.0f %s: got %.2f %.2f %.2f %.2f, expected "
                "%.2f %.2f %.2f %.2f\n",
                width, height, name, got.x, got.y, got.width, got.height,
                want->x, want->y, want->w, want->h);
        failures++;
    }
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

static void check_chrome(void) {
    for (unsigned c = 0; c < sizeof(chrome_cases) / sizeof(chrome_cases[0]); c++) {
        const struct chrome_case *w = &chrome_cases[c];
        struct chrome_layout l = chrome_layout_for(w->width, w->height);
        check(w->width, w->height, "settings_button", l.settings_button, &w->settings);
        check(w->width, w->height, "calibration_button", l.calibration_button,
              &w->calibration);
        check(w->width, w->height, "tab[0]", l.tab[0], &w->tab0);
        check(w->width, w->height, "tab[1]", l.tab[1], &w->tab1);
        if (fabsf(l.status_left - w->status_left) > 0.01f) {
            fprintf(stderr, "%.0fx%.0f status_left: got %.2f, expected %.2f\n",
                    w->width, w->height, l.status_left, w->status_left);
            failures++;
        }
    }
}

int main(void) {
    check_chrome();
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
    if (failures) {
        fprintf(stderr, "%d layout check(s) failed\n", failures);
        return 1;
    }
    puts("layout checks passed");
    return 0;
}
