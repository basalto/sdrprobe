# 14 - The RTL-SDR profile describes a tuner, in two places

Status: needs-triage
Opened 2026-09-12, from `docs/two-receivers-compared.md`: a second dongle,
an Elonics E4000, measured against the R820T everything here was built on.

`device_profile_rtlsdr()` is named for the demodulator and carries the
**tuner's** limits:

    p.tune_lower_hz = 24000000.0;
    p.tune_upper_hz = 1766000000.0;

Those are an R820T's. On an E4000 board all three parts of that are wrong, and
the program was asked rather than argued with:

| tuning | result |
| --- | --- |
| 40 MHz | **refused** -- `[E4K] PLL not locked`, and the retune fails |
| 1090 MHz | recorded |
| 1150 MHz | **refused** -- inside this tuner's L-band gap |
| 1300 MHz | recorded |
| 1900 MHz | **recorded**, 134 MHz past what the profile claims |
| 2100 MHz | **recorded**, 334 MHz past |

## The second place, which is worse because it is invisible

`lte_reachable_band()` in `src/lte_dsp.c`:

    static const int reachable[LTE_REACHABLE_BANDS] = { 28, 20, 8 };

The band table three lines above it already carries **1, 3 and 7** with their
EARFCN ranges; only this list withholds them, and it withholds them on one
tuner's behalf. With the E4000 attached, band 3 is reachable and busy: a
1805-1880 MHz sweep returns **12 carriers**, several 3 to 7.5 MHz wide, and
the band plan names them correctly as GSM 1800 / LTE B3 downlink.

This is not a hypothetical loss of capability. `CLAUDE.md` records that the
LTE calibration panel once offered bands 1, 3 and 7 and that "an R820T cannot
tune any of them" -- the fix was to withhold them, and the reason has now
changed under it.

**What it does not yet buy.** A 20 s chain walk at EARFCN 1889, derived from a
sweep's power centre rather than from a raster, read 292 blocks, 21 identities
and **decoded nothing** -- primary correlations of 0.33 against the 0.8 a real
cell gives, so it stops at the first stage. Band 3 here reads -40 to -45 dBFS
against band 8 and 20's -33 to -36 on the same antenna, which a telescopic
whip at 1.8 GHz and this tuner's noise figure both explain. **Reachable is not
the same as decodable**, and nothing above claims it is.

## Three things to decide

**Where the tuner goes.** librtlsdr reports it (`rtlsdr_get_tuner_type`), so
the profile can be built from the device rather than from a constant. The
options are a tuner argument to `device_profile_rtlsdr()`, a second
constructor, or a small tuner table the constructor consults. All three are
cheap; the question is whether `device_profile` gains a `tuner` field, which
is a fact about the part it has so far avoided naming.

**The gap.** 1107-1246 MHz is a discontinuity `tune_lower_hz`/`tune_upper_hz`
cannot express, exactly as `rate_min_hz`/`rate_max_hz` cannot express
librtlsdr's 300-900 kHz rate hole. `issues/02-*` refused to invent a
representation for one device's discontinuity. **There are two now**, on two
different axes and two different devices, which is a different argument from
the one that was refused -- and a tuner that silently fails to lock is worse
than one that refuses, because `rtlsdr_set_center_freq` returning success on
the R820T is what `deepening/issues/10-*` phase 1a already found.

**Whether refusal is part of the profile.** The same ticket concluded that
"the RTL-SDR backend does not refuse an unreachable setting" and that
"Receiver rejected ..." is therefore a message for a failure this device does
not produce. **The E4000 produces it.** So the behaviour is the tuner's, the
rollback path in `receiver_runtime.c` is live rather than defensive, and the
phase 1a finding should be read as "the R820T does not refuse" rather than
"this backend does not".

## What must be checkable

`check-survey-bands` runs its both-directions property against **two**
profiles, for the stated reason that one device's numbers pass while offering
half of one and missing half of the other. Neither of those two is an E4000,
whose reach contains neither of theirs: it loses everything under 52 MHz and
gains 1766-2212. A third profile is the cheapest possible use of a property
that already exists.

And whatever `lte_reachable_band()` becomes, the property to pin is that it
never returns a band the profile cannot tune, and never withholds one it can.
Today it is a literal with no relation to a profile at all.
