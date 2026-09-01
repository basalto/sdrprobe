# 03 — The scan step machine and the strongest-BCCH choice

Status: resolved
Blocked by: (none)

`src/overlay_scan.c` decides which ARFCN the scan is on, when it has dwelt long
enough, when it is finished (`update_scan`), and which channel the operator is
handed at the end (`scan_strongest_arfcn`, `scan_strongest_bcch`). The last one
matters most: it chooses the channel the operator then spends their time on,
and a wrong choice looks exactly like a quiet neighbourhood.

`scan_strongest_bcch` prefers a channel with a decoded BSIC over a merely loud
one. That preference is the whole value of the function and there is no check
that it survives a refactor.

What it would take: the two selectors take `const struct app *` but read only
the scan table. Give them a `scan_select.h` over the table alone and check:
an empty table, one candidate, a loud channel with no BSIC against a quieter
one with a BSIC, a tie, and every level equal to the floor. The step machine
needs the same treatment as ticket 01 and can share its shape.

## Comments

## Answer

Done in `src/scan_plan.h`, checked by `tests/scan_plan_test.c`
(`make check-scan`, 40 checks).

`struct scan_plan` with `scan_plan_make/step_centre/covers`, the
settle/probe/next/finished machine, `scan_hold_confidence()`, and the three
selectors -- `scan_select_strongest`, `scan_select_bcch`, `scan_choose`. The
constants came out of `acquisition.h`, which now includes the header;
`struct band_scan` holds a plan instead of three loose doubles, and
`scan_strongest_arfcn`/`scan_strongest_bcch` are one-line adapters that hand it
the arrays out of `struct app`.

The two checks worth having:

`test_every_channel_is_measured` walks all 124 downlink channels at five sample
rates and requires each to fall inside exactly one step's accept window --
covered, and covered once. A step count one too low leaves the top of the band
unmeasured, and those channels then read as absent, which is
indistinguishable from a cell that is not transmitting.

`test_bcch_beats_loud` pins the preference the scan exists to express: a
carrier at -55 dB with an FCCH tone is chosen over one at -20 dB without.
Dropping that preference -- the obvious "simplification", since the fallback
already returns the loudest -- fails it immediately.
