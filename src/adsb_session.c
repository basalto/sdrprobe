#include "adsb_session.h"

#include <string.h>

void adsb_session_reset(struct adsb_session *s) {
    if (s)
        memset(s, 0, sizeof(*s));
}

int adsb_session_feed(struct adsb_session *s, const float *magnitudes,
                      size_t pair_count, double now) {
    struct adsb_frame_trace trace;
    struct adsb_demod_stats stats;
    size_t count;
    int i;

    if (!s)
        return 0;
    s->message_count = 0;
    if (!magnitudes || pair_count == 0)
        return 0;

    memset(&trace, 0, sizeof(trace));
    count = adsb_demod(&s->decoder, magnitudes, pair_count, now, s->messages,
                       ADSB_SESSION_MESSAGES, &trace, &stats);
    s->block_stats = stats;
    adsb_totals_add(&s->totals, &stats);
    adsb_trace_keep(&s->trace, &s->good_trace, &trace);

    if (count > ADSB_SESSION_MESSAGES)
        count = ADSB_SESSION_MESSAGES;
    s->message_count = (int)count;
    for (i = 0; i < s->message_count; i++) {
        s->frames_total++;
        if (s->messages[i].has_position)
            s->positions_total++;
    }
    return s->message_count;
}
