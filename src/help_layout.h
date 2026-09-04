#ifndef HELP_LAYOUT_H
#define HELP_LAYOUT_H

#include <raylib.h>

/*
 * The help overlay's geometry.
 *
 * It already had one source of truth -- the input and the drawing both read
 * the same struct -- and was the only screen here where that was true without
 * a header. The problem was reach rather than duplication: living inside the
 * .c, no check could ask it anything, so the one screen that already got this
 * right was as unverified as the two that did not.
 *
 * The topic list comes with it, because the sidebar has one entry per topic
 * and the layout cannot be computed without knowing how many there are.
 */

enum help_topic {
    HELP_OVERVIEW,
    HELP_MAGNITUDE,
    HELP_SPECTRUM,
    HELP_WATERFALL,
    HELP_SCATTER,
    HELP_SURVEY,
    HELP_QUALITY,
    HELP_SCAN,
    HELP_BURST,
    HELP_CONSTELLATION,
    HELP_ADSB,
    HELP_ADSB_ANALYSIS,
    HELP_LTE,
    HELP_FM,
    HELP_CALIBRATION,
    HELP_TOPIC_COUNT
};

struct help_layout {
    Rectangle panel;
    Rectangle entry[HELP_TOPIC_COUNT];
    Rectangle body;
    Rectangle close;
};

static inline struct help_layout help_layout_for(float width,
                                                 float height) {
    struct help_layout l;
    /*
     * The sidebar is a fixed list of topics in a panel that shrinks with the
     * window, so the rows have to shrink too. At 900x600 the comfortable
     * 28-plus-3 ran the last topic nineteen pixels below the panel, where
     * nobody could click it -- and since it is the last one, nothing above it
     * looked wrong.
     */
    float entry_h = 28.0f;
    float entry_gap = 3.0f;
    float sidebar_w = 196.0f;
    float content_y;
    float room;

    l.panel = (Rectangle){ 34.0f, 34.0f, width - 68.0f, height - 68.0f };
    if (l.panel.width < 320.0f)
        l.panel.width = 320.0f;
    if (l.panel.height < 220.0f)
        l.panel.height = 220.0f;

    l.close = (Rectangle){ l.panel.x + l.panel.width - 104.0f,
                           l.panel.y + 16.0f, 84.0f, 30.0f };
    content_y = l.panel.y + 86.0f;
    room = l.panel.height - 86.0f - 44.0f;
    if ((entry_h + entry_gap) * (float)HELP_TOPIC_COUNT > room) {
        float each = room / (float)HELP_TOPIC_COUNT;
        entry_gap = each > 12.0f ? 2.0f : 1.0f;
        entry_h = each - entry_gap;
        if (entry_h < 10.0f)
            entry_h = 10.0f;
    }
    for (int i = 0; i < HELP_TOPIC_COUNT; i++)
        l.entry[i] = (Rectangle){ l.panel.x + 20.0f,
                                  content_y + (float)i * (entry_h + entry_gap),
                                  sidebar_w, entry_h };
    l.body = (Rectangle){ l.panel.x + 20.0f + sidebar_w + 26.0f, content_y,
                          l.panel.width - sidebar_w - 72.0f,
                          l.panel.height - 86.0f - 44.0f };
    if (l.body.width < 120.0f)
        l.body.width = 120.0f;
    if (l.body.height < 60.0f)
        l.body.height = 60.0f;
    return l;
}

static inline struct help_layout help_layout_now(void) {
    return help_layout_for((float)GetScreenWidth(), (float)GetScreenHeight());
}

#endif
