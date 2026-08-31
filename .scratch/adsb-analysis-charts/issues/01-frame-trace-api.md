# 01 — Frame trace in the plugin API

Status: resolved
Blocked by: (none)

Give `src/adsb_dsp.{c,h}` a way to hand back what one frame looked like, the way
`struct gsm_sch_symbols` does for the SCH. No decode behaviour changes.

## New API

```c
#define ADSB_TRACE_LANDSCAPE 65   /* +/-32 samples around the frame start */
#define ADSB_TRACE_SAMPLES   (ADSB_PREAMBLE_SAMPLES + ADSB_LONG_BITS * ADSB_SAMPLES_PER_BIT)

struct adsb_frame_trace {
    int    valid;            /* 0 when no attempt has been seen yet */
    int    crc_ok;           /* the attempt's CRC remainder was zero */
    int    downlink_format;
    int    bit_count;        /* ADSB_SHORT_BITS or ADSB_LONG_BITS */
    uint32_t icao;           /* 0 when crc_ok is 0 -- do not trust a bad frame */
    double time_seconds;

    float  landscape[ADSB_TRACE_LANDSCAPE];  /* preamble score by offset */
    int    landscape_center;                 /* index of the accepted offset */
    float  confidence[ADSB_LONG_BITS];       /* |first-second|/(first+second) */
    float  margin[ADSB_LONG_BITS];           /* signed: (first-second)/(sum) */
    float  amplitude[ADSB_LONG_BITS];        /* (first+second), normalised */
    uint8_t bit[ADSB_LONG_BITS];             /* the hard decision taken */
    float  envelope[ADSB_TRACE_SAMPLES];     /* magnitudes, normalised */
    float  preamble_high;                    /* the normalising level */
};

/* How well the magnitude pattern at `index` matches the Mode S preamble:
   the mean of the four pulse samples over the mean of the twelve quiet ones,
   0 when the window runs past the buffer. The boolean preamble_at() keeps its
   own gating; this is the same evidence expressed as a number, for the
   landscape and for a future stronger detector. */
float adsb_preamble_score(const float *magnitudes, size_t index,
                          size_t pair_count);
```

`adsb_demod()` gains a trailing `struct adsb_frame_trace *trace` parameter
(NULL to skip, like `gsm_sch_decode`'s `symbols`). Fill it for the **last
attempt in the buffer that reached DF17/18 shape**, pass or fail — a failing
frame is the one worth looking at, so it must not be the one dropped. Compute
the landscape only for that attempt, so a block full of false preambles costs
nothing.

## Notes

- `preamble_high` is the mean of samples 0, 2, 7, 9 at the accepted offset —
  the same figure `preamble_at()` already computes. Normalise `envelope` and
  `amplitude` by it so the charts have a fixed axis whatever the gain is.
- Guard the division in confidence/margin: `first + second` can be zero.
- Every caller of `adsb_demod()` must be updated: `src/view_adsb.c` and
  `scripts/adsb_chain_probe.c`.

## Comments
