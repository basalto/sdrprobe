# 05 - What the tuner reaches comes from the profile

Status: resolved, 2026-09-08. The reach is the profile's,
`check-survey-bands` is a property over three of them, and the band plan runs to
5875 MHz. Verified on the live receiver as well as on a capture.

`SURVEY_TUNER_LOWER_HZ` and `SURVEY_TUNER_UPPER_HZ` are an R820T's 24 MHz and
1766 MHz, and `check-survey-bands` asserts reach in **both** directions --
nothing offered is out of reach, nothing reachable is left off. That check is
correct and it will fail on a new device by design, which is the right
behaviour and the reason this is its own ticket.

A B210-class device is **not a superset**: it reaches 6 GHz and opens 2.4 GHz
ISM, 3.5 GHz n78 and 5 GHz, and it loses everything below about 70 MHz --
including the 25 MHz clock fundamental, though its third harmonic at
75.0005 MHz that `carrier_75000_bare.bin` holds stays reachable.

## What to build

The two constants come from the profile. `survey_bands_for()` takes it.
`check-survey-bands` runs its both-directions assertion against **each** of at
least two profiles, which is what turns it from a device-specific check into a
property.

`docs/band-surveys.md` and the band-plan table gain the allocations above
1766 MHz that were previously unreachable and therefore untested.

## Acceptance criteria

- `check-survey-bands` passes for an RTL profile and a wideband profile.
- No allocation is offered that the active profile cannot tune.
- `check-pipelines` unchanged.

## Not in scope

- Decoding anything new up there. `docs/what-is-on-air.md` gets the entries
  when there is a receiver to measure them with.


## What was built

`SURVEY_TUNER_LOWER_HZ` and `SURVEY_TUNER_UPPER_HZ` are **gone**. Every
function in `survey_bands.h` already took the reach as arguments -- that was
the one thing this ticket did not have to change -- so the work was the six
call sites in `view_survey.c`, which now pass `app->device.tune_lower_hz` and
`tune_upper_hz`.

`check-survey-bands` became a property. `check_both_directions(who, low, high,
least)` runs the whole two-way assertion -- nothing offered is out of reach,
nothing reachable is left off -- against an R820T, a 70 MHz - 6 GHz part, and
an 80-120 MHz window. It also asserts the **asymmetry** directly, which is the
thing a single-device check can never see:

| reach | allocations offered |
| --- | --- |
| R820T, 24 MHz - 1766 MHz | 54 |
| wideband, 70 MHz - 6 GHz | 60 |
| 80 - 120 MHz | 4 |
| the whole table | 80 |

Neither of the first two contains the other. The check names three specific
cases: 2.4 GHz ISM and n78 are reachable by the wide part and not the RTL, CB
at 27 MHz is reachable by the RTL and not the wide part, and both reach the
ILS marker band at 75 MHz -- which matters because
`testfiles/carrier_75000_bare.bin` lives there, the third harmonic of a 25 MHz
clock whose fundamental only the RTL can hear.

## The band plan gained twelve allocations

Above 1766 MHz there was **one** entry (GSM 1800 uplink, which straddles the
edge) and now there are twelve: GSM 1800 / LTE B3 downlink, DECT, LTE B1
up and down, 2.4 GHz ISM, LTE B7 up and down, B38 unpaired, 5G NR n78, the two
5 GHz RLAN blocks and 5.8 GHz ISM. 68 entries to 80.

Three of them name `BAND_PLAN_LTE`, and only three: bands 3, 1 and 7 are
exactly what the E-UTRA table in `lte_dsp.c` already holds, so no entry names
a decoder the program does not have. **No uplink names a decoder** -- there is
nothing to read on an uplink from here -- and n78 names none, because there is
no `nr_dsp`. `check-band-plan` asserts all seven of those.

The table's header claimed "coverage is what an RTL-SDR can hear". That was
the wrong boundary for a reference table and it is corrected: which
allocations a *receiver* reaches is the profile's business, and the plan says
what a band is for (ADR-0015).

The gaps are gaps on purpose, per the table's own rule: 1785-1805 and
1980-2110 are duplex splits, 1900-1920 is the unpaired block, 2483.5-2500 is
mobile-satellite. `check-band-plan` names each of them as returning no entry,
and its old `check_none("above the tuner", 2 GHz)` -- which passed for the
wrong reason -- is now `"the 2 GHz duplex gap"`.

## One claim of mine was wrong first

`check_both_directions` asserted `count > 10` for every reach, and the
80-120 MHz window has four allocations in front of it. The floor is a
per-reach argument now: asserting ten of everything is a claim about the table
rather than about the filter.

## Looked at, both ways

On the **live receiver**: the picker lists CB, 10 m amateur, land mobile, band
I television, 6 m amateur, the ILS markers, band II, FM broadcast picked out
in green, aeronautical navigation, VHF airband, weather satellite -- 24M to
1766M in the range fields.

Under **file playback** it offers exactly one allocation, the one the capture
sits in, and that is ticket 02's decision working rather than a fault: a
capture's tuning range is the single frequency it was taken at, because being
at one place on the band is a fact about a capture and not a missing
capability. A sweep needs a live receiver and the view already says so.

## Acceptance

- `check-survey-bands` passes for an RTL profile and a wideband profile, plus
  a narrow one: 330 checks.
- No allocation is offered that the active profile cannot tune, asserted for
  all three.
- `check-pipelines` unchanged. `make check` is **17220 checks in 46 suites**.
- `docs/band-surveys.md` and `CLAUDE.md` carry the asymmetry and the playback
  case.
