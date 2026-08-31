# 02 — The band plan lookup

Status: resolved
Blocked by: (none)

`src/band_plan.{c,h}`: a static table mapping frequency ranges to the service
allocated there, and nothing else. No DSP, no GUI, no receiver.

```c
struct band_plan_entry {
    double lower_hz, upper_hz;
    const char *name;          /* "GSM 900 downlink" */
    const char *note;          /* "200 kHz channels" or NULL */
    int decoder;               /* enum band_plan_decoder */
};

enum band_plan_decoder { BAND_PLAN_NONE, BAND_PLAN_GSM, BAND_PLAN_ADSB };

/* The allocation containing `hz`, or NULL. Ranges do not overlap. */
const struct band_plan_entry *band_plan_lookup(double hz);
```

Cover what an RTL-SDR can hear and someone might point this at: LW/MW/SW
broadcast, the 6-49 m bands as one entry each is too fine — group them; VHF
airband, marine, FM broadcast, DAB, PMR446, the 433/868 MHz ISM bands, GSM 900
and 1800 up and down, ADS-B at 1090, ACARS, weather satellites, the 2 m and
70 cm amateur allocations. Regions differ: say in the header that the table is
ITU Region 1 (Europe) and that a frequency outside it simply returns NULL
rather than being wrong.

Two entries carry a decoder: GSM 900 downlink and 1090 MHz. That is what the
view's handoff button reads; the enum stays in this file so the table does not
need to know what the views are called.

## Checks (`check-band-plan`, its own target, `-lm` only)

- Known frequencies land in the expected entry: 100.1 MHz FM, 1090 MHz ADS-B,
  943.2 MHz GSM 900 downlink, 890.2 MHz GSM 900 uplink.
- A frequency in a gap returns NULL.
- No two entries overlap, and every entry has lower < upper — walked
  programmatically over the whole table, so a badly typed edit fails the check
  rather than silently shadowing an entry.

## Comments
