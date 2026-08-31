# 03 — Decode funnel counters

Status: resolved
Blocked by: 01

The cheapest half of this feature, and independently useful: report what the
demodulator threw away.

```c
struct adsb_demod_stats {
    uint64_t preambles;    /* preamble_at() accepted */
    uint64_t attempts;     /* ... and the frame was DF17/18-shaped */
    uint64_t crc_failed;   /* ... and the CRC remainder was nonzero */
    uint64_t decoded;      /* ... and it parsed */
};
```

`adsb_demod()` fills a caller-provided stats struct for the block it just
scanned (NULL to skip). `struct adsb_view` keeps both the latest block's stats
and a running total, and `draw_adsb()` puts them on the header line beside the
existing frames/positions counters.

This is what distinguishes "nothing is transmitting" from "frames are arriving
and failing", which is currently indistinguishable from an empty log. It should
be visible in **both** view modes, not only in the analysis mode.

ADR-0009 is unaffected: failures are still dropped, just no longer silently.

## Comments
