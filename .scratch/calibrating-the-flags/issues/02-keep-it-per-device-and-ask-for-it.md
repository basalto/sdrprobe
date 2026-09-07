# 02 - Keep the calibration per device, and recommend it when it is missing

Status: needs-triage

Depends on 01, which produces the number.

## Per device, not per site

The tuning correction is already kept per **site** (`config_site_ppm()`),
and the reason is in CLAUDE.md: a ppm drifts and is measured against whatever
reference a place offers, so arriving somewhere restores that place's
calibration.

A comb is the opposite. It is a property of the **hardware** and travels with
it: the same dongle has the same crystal in every room. So it is keyed on the
device, and a survey at a new site does not need it re-measured -- while
plugging in a different dongle does, at the same desk.

That distinction is worth writing down in the config file's own comments,
because the two live next to each other and a reader will assume they work the
same way.

## What identifies a device

`--record-seconds`' sidecar already records `source` and `tuner` --
"RTL-SDR: Generic RTL2832U" and "R820T". That is a model, not a device: two
identical dongles are indistinguishable by it, and they may have different
crystals.

librtlsdr can report a serial (`rtlsdr_get_device_usb_strings`). Whether it is
unique in practice is a question to answer before relying on it -- generic
dongles ship with the same serial burned in far too often, and a calibration
attributed to the wrong physical device is worse than none. **If the serial
cannot be trusted, key on model and say the calibration is a model default
rather than this device's measurement**, which is honest and still better than
a compiled-in constant.

`.scratch/` note: device selection by index or serial is on TODO.md and not
built. This ticket needs the identity, not the selection.

## Recommending it

A wrong comb is silent. It does not look like a fault -- it looks like
signals: spurs enter the site history and are remembered for ever, and real
spurs go unflagged. So an unknown device has to be *said*, not left to be
noticed.

Where, and how loudly, is the design question. The survey already has a place
for this kind of statement: it says "N crossed are the receiver" under the
chart and marks candidates in the list. An uncalibrated receiver could say
"the receiver's own comb has not been measured on this device -- these marks
are a model default" in the same place, which puts the caveat where the claim
is rather than in a dialogue somebody dismisses.

**It must not become a modal that blocks a sweep.** Somebody plugging in a
borrowed dongle to look at one frequency should not have to calibrate first,
and the marks are useful-but-uncertain rather than wrong.

## Storing it

`struct config` already keeps lists that grow -- sites and antennas -- with
`config_remember_site()` and `config_remember_antenna()`, and unknown lines
are preserved verbatim so an older build does not discard a newer one's
settings. A `config_remember_device()` alongside them is the same shape.

## What must be checkable

The store and the lookup, with no receiver (ADR-0012), as `check-config`
already covers sites and antennas: a device remembered and found again; an
unknown device returning the default and saying it is a default; the list
bounded; and a config file from a build that did not know about devices
round-tripping unchanged.
