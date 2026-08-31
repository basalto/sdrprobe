# 01 — Peak search and characterisation in the generic core

Status: resolved
Blocked by: (none)

Two technology-independent additions to `src/sdr_dsp.{c,h}`, both hardware-free
and testable in `check-sdr-dsp`.

## Peak search over a survey's power array

```c
struct sdr_peak {
    int    index;            /* bin of the peak within the survey array */
    float  power_dbfs;       /* its level */
    float  floor_dbfs;       /* robust local floor either side of it */
    float  prominence_db;    /* power - floor */
    int    lower_index;      /* -bandwidth_db points, for occupied width */
    int    upper_index;
};

/* Find local maxima standing at least min_prominence_db above a robust local
   floor, merging bins that belong to the same hump. Sentinel-valued bins (not
   measured) break a hump rather than joining it. Returns how many were written,
   strongest first. */
int sdr_dsp_find_peaks(const float *power_dbfs, int count, float sentinel,
                       float min_prominence_db, float bandwidth_db,
                       float *sort_workspace, struct sdr_peak *peaks,
                       int max_peaks);
```

The local floor is the median of a window either side of the candidate,
excluding the hump itself — the same robustness argument as
`robust_center_spread`: one strong neighbour must not raise the floor and hide
a weaker peak beside it.

## Characterising one carrier from a live spectrum

```c
struct sdr_carrier_report {
    double centre_hz;        /* power-weighted, refined from the peak bin */
    double offset_hz;        /* from the receiver's centre */
    float  peak_dbfs;
    float  floor_dbfs;
    float  prominence_db;
    double bandwidth_hz;     /* between the -bandwidth_db points */
};

int sdr_dsp_characterise_carrier(const float *spectrum_dbfs, size_t bin_count,
                                 double centre_hz, double sample_rate,
                                 double search_half_width_hz,
                                 float bandwidth_db, float *sort_workspace,
                                 struct sdr_carrier_report *report);
```

## Checks

- A synthetic array with three humps of known width and level: the right number
  of peaks, strongest first, indices at the humps, prominence within a dB.
- A hump beside a much stronger one is still found — the failure mode a mean
  floor would cause.
- Sentinel runs do not merge two humps into one.
- `sdr_dsp_characterise_carrier` on a synthesised raised-cosine bump recovers
  its centre to within a bin and its width to within 10%.

## Comments

**The ticket's rule was wrong, and the checks caught it.** "Local maxima
standing above a robust local floor" reports the shoulder of a strong carrier
as a signal of its own: the shoulder is far above the floor while being nothing
but the side of its neighbour. The first implementation found three peaks where
the check expected two, and four where a sentinel gap split two humps.

`sdr_dsp_find_peaks` uses **topographic prominence** instead -- how far you must
descend from a peak before you can climb to anything higher. A shoulder scores
almost nothing because reaching its parent costs it nothing; a weak carrier
beside a strong one keeps the full drop to the floor between them, which is the
case a survey most needs to show. The median-window floor stays, but only as a
figure to report.

The walk is bounded to a window rather than running to the array's ends, so a
smooth ramp cannot cost a pass per bin.
