#ifndef SDRGUI_H
#define SDRGUI_H

#include <stddef.h>

#include <raylib.h>

/*
 * SDR visual components: reusable pieces of the display that render one kind of
 * signal information from plain data and geometry, without knowing the
 * application's state. Screens in the application compose these components (and
 * generic raygui widgets) together; the components never see `struct app`.
 *
 * This layer depends only on raylib.
 */

/* --- Plot geometry helpers --- */

/* The inset a chart takes inside its own rectangle: `caption_h` at the top for
   its caption and `gutter` on the left for its axis labels, plus half a line at
   the bottom where the lowest label is centred on the plot edge. Components
   call this so they draw entirely within the rect they were given. */
Rectangle sdrgui_chart_area(Rectangle outer, float gutter, float caption_h);

/* Fill x_fraction/y_fraction/mouse for the cursor over `plot`; returns 0 when
   the cursor is outside the plot. */
int sdrgui_plot_cursor(Rectangle plot, float *x_fraction, float *y_fraction,
                       Vector2 *mouse);

/* Crosshair + floating readout box anchored to the cursor, clamped to `plot`. */
void sdrgui_cursor_readout(Rectangle plot, Vector2 mouse, const char *text);

/* Plot background, optional quarter gridlines, and border. */
void sdrgui_plot_frame(Rectangle plot, int quarter_grid);

/* Map a value in [lower, upper] to a y pixel inside `plot` (clamped). */
float sdrgui_plot_y(Rectangle plot, float value, float lower, float upper);

/* Format a signed frequency offset with an adaptive Hz/kHz/MHz unit. */
void sdrgui_format_frequency_offset(char *text, size_t size, double offset);

/* --- Calibration-health indicator --- */

enum sdrgui_health {
    SDRGUI_HEALTH_UNKNOWN = 0, /* grey */
    SDRGUI_HEALTH_GOOD,        /* green */
    SDRGUI_HEALTH_DRIFT,       /* red */
    SDRGUI_HEALTH_CHECKING     /* amber */
};

struct sdrgui_health_params {
    int state;          /* enum sdrgui_health */
    int arfcn;          /* for the "checking" banner */
    const char *notice; /* drift banner text (may be NULL/empty) */
};

/* Top-right status circle plus an optional checking/drift banner. */
void sdrgui_health_dot(const struct sdrgui_health_params *params);

/* --- Views --- */

struct sdrgui_magnitude_params {
    Rectangle plot;
    int have_samples;
    const float *peaks;
    size_t bin_count;
    float lower;         /* magnitude axis */
    float upper;
    float min;           /* block statistics */
    float mean;
    float max;
    double duration_ms;  /* time span of the block (0 when no samples) */
    float physical_max;  /* full-scale magnitude for the empty-axis case */
};

/* Magnitude-over-time view (peak per time bin). */
void sdrgui_magnitude(const struct sdrgui_magnitude_params *params);

struct sdrgui_spectrum_params {
    Rectangle plot;
    double center_hz;    /* receiver center frequency */
    double sample_rate;
    int ready;
    const float *average; /* dBFS, `bins` entries */
    const float *peak;    /* dBFS peak-hold, `bins` entries */
    int bins;
    float lower_dbfs;    /* axis bottom */
    float top_dbfs;      /* axis top */
    int windows;         /* averaged FFT windows, for the caption */
};

/* Frequency spectrum view (average trace + peak hold) with dBFS/frequency axes. */
void sdrgui_spectrum(const struct sdrgui_spectrum_params *params);

struct sdrgui_scatter_params {
    Rectangle plot;
    Texture2D texture;  /* the accumulated I/Q scatter texture (app-owned) */
    float axis_limit;   /* normalized full-scale half-range */
    size_t inserted;    /* latest-block point count, for the caption */
};

/* I/Q scatter view: draws the scatter texture with I/Q axes and a cursor. */
void sdrgui_scatter(const struct sdrgui_scatter_params *params);

struct sdrgui_waterfall_params {
    Rectangle plot;
    Texture2D texture;          /* full-span waterfall history (app-owned) */
    double center_hz;
    double sample_rate;
    int calibration_mode;       /* suppress the top caption, allow zoom */
    int channel_axis;           /* label the x-axis by channel number */
    double zoom_center_hz;      /* 0 disables the zoom-to-channel */
    double zoom_half_width_hz;
    int rows;                   /* rows of history currently retained */
    int height;                 /* texture height in rows */
    size_t pair_count;          /* pairs per row (0 -> use fallback_pairs) */
    size_t fallback_pairs;
    float lower_dbfs;           /* colour scale */
    float top_dbfs;
    /* Evenly-spaced channel grid used by the channel axis. */
    double channel_base_hz;
    double channel_spacing_hz;
    int channel_max;
    const char *channel_label;    /* e.g. "ARFCN" */
    const char *channel_caption;  /* bottom caption in channel-axis mode */
    const char *channel_outside;  /* cursor text off-grid */
};

/* Frequency/time waterfall with a frequency or channel x-axis and a cursor. */
void sdrgui_waterfall(const struct sdrgui_waterfall_params *params);

struct sdrgui_scan_chart_params {
    Rectangle plot;
    const float *power;      /* per-channel dBFS, indices 1..count */
    const float *bcch_conf;  /* per-channel FCCH coherence, 1..count */
    int count;               /* highest channel index */
    float sentinel;          /* value marking an unmeasured channel */
    float bcch_min_conf;     /* coherence at/above which a bar is a BCCH */
    int hover;               /* hovered channel (0 = none) */
    double base_hz;          /* channel 0 frequency, for the hover readout */
    double spacing_hz;
    int selected;            /* selected/inspected channel (0 = none) */
};

/* Per-channel power bar chart with BCCH highlighting and a hover readout. */
void sdrgui_scan_chart(const struct sdrgui_scan_chart_params *params);

/* --- Decoded-message log --- */

struct sdrgui_message_log_row {
    const char *time;    /* decode timestamp, e.g. "14:32:07" */
    const char *icao;    /* transmitter id, e.g. "4840D6" */
    const char *label;   /* short message-kind tag, e.g. "ID", "POS", "VEL" */
    const char *detail;  /* decoded summary text */
    const char *raw;     /* raw hexadecimal frame */
    int highlight;       /* newest / just-updated row */
};

struct sdrgui_message_log_params {
    Rectangle plot;
    const struct sdrgui_message_log_row *rows; /* newest first */
    int count;
    const char *caption;      /* top-of-panel caption (may be NULL) */
    const char *empty_notice; /* shown when count == 0 */
};

/* Scrolling, newest-first table of decoded messages (ICAO | kind | detail). */
void sdrgui_message_log(const struct sdrgui_message_log_params *params);

/* --- Symbol constellation --- */

struct sdrgui_constellation_params {
    Rectangle plot;
    const float *x;       /* normalised in-phase, `count` entries */
    const float *y;       /* normalised quadrature */
    const unsigned char *bit; /* per-point hard decision (may be NULL) */
    int count;
    const char *caption;
    const char *empty_notice; /* shown when count == 0 */
};

/* Decoded-symbol constellation: normalised points on I/Q axes, coloured by
   their hard bit decision, showing demodulation/decoding quality. */
void sdrgui_constellation(const struct sdrgui_constellation_params *params);

/* --- Burst Analysis Chart --- */

enum sdrgui_burst_chart_type {
    SDRGUI_BURST_LINE,
    SDRGUI_BURST_BAR
};

/* Every chart component here draws entirely INSIDE the rectangle it is given.
   Each reserves a strip at the top for its caption and a gutter on the left
   wide enough for its own axis labels, then plots in what remains.

   That is the whole point of the arrangement: the gutter depends on how wide
   the labels render, which depends on the values, which only the component
   sees. A caller cannot leave the right clearance because it cannot know the
   number. So callers pack these by rectangle alone, and any gap between them
   is purely for looks. */
struct sdrgui_burst_chart_params {
    Rectangle plot;
    const float *data;        /* The values to plot, `count` entries */
    int count;
    enum sdrgui_burst_chart_type type;
    float y_min;              /* Axis minimum */
    float y_max;              /* Axis maximum */
    const char *title;
    const char *empty_notice; /* Shown when count == 0 */
};

/* Time-series plot of intermediate decoder metrics (correlation, soft bits, phase). */
void sdrgui_burst_chart(const struct sdrgui_burst_chart_params *params);

/* --- Widgets --- */

/* Render-only text-entry box (the app owns editing/validation). Draws the box,
   a focus-tinted border, and the current text. */
void sdrgui_text_field(Rectangle box, const char *text, int focused);

#endif
