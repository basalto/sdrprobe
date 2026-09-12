# Two receivers compared: an R820T and an E4000

Every threshold, comb and clock family in this repository was measured on one
dongle at one site, and `docs/receiver-artifacts.md` says so in as many words:
the two comb divisors "were measured on an RTL2832U and are unverified
anywhere else". `.scratch/calibrating-the-flags/` was opened to record that
distinction and `.scratch/device-model/issues/08-*` inherited it.

This is the first second receiver. It is **not** the second *backend* that
`issues/07-*` is about -- both are librtlsdr -- but it is a different tuner on
a different board with a different crystal, which is what turns "this dongle
does this" into a fact about a part, or refutes it.

Measured 2026-09-12, one site (`home-sala-estar`), one telescopic antenna,
moved from one dongle to the other. **Every tone measurement below is
uncorrected** (`--ppm 0`), because that is the condition under which a tone
clocked by the receiver reads its exact nominal whatever the crystal error
(`issues/11-*`); an offset of +488 Hz is half a 976.6 Hz bin and means "on the
nominal".

## The two

| | development dongle | the second one |
| --- | --- | --- |
| board | Generic RTL2832U | Terratec T Stick PLUS (RTL2838UHIDIR) |
| tuner | Rafael Micro **R820T** | Elonics **E4000** |
| USB serial | 77771111153705700 | **00000001** |
| tuning reach | 24 - 1766 MHz | **52 - 2212 MHz, with a gap at 1107 - 1246** |
| gain steps | 29, from 0.0 to 49.6 dB | **14, from -1.0 to 42.0 dB** |
| crystal error | **+32 ppm** | **+19.6 ppm** (residual -19.55, sem 0.47, spread 3.72, 888 FCCH measurements) |
| reference clock | 28.8 MHz | 28.8 MHz (assumed; not independently measured) |

Both crystals are **fast**, and 12 ppm apart. The second one's calibration
locked against the same GSM cell (ARFCN 113) the first one uses, so the two
numbers are measured the same way against the same reference.

**Its per-measurement scatter is about four times wider** -- spread 3.72 ppm
here against about 0.9 inferred from the R820T's `sem 0.04 over 498
measurements` -- but those were taken on different days at different
temperatures, so treat it as a hint and not a result.

## What the profile says, and what is true

`device_profile_rtlsdr()` hardcodes `tune_lower_hz = 24000000` and
`tune_upper_hz = 1766000000`. **Those are the R820T's numbers under the
RTL-SDR's name**, and on this board all three parts of that are wrong:

| tuning | result |
| --- | --- |
| 40 MHz | refused: `[E4K] PLL not locked`, `Failed to set RTL-SDR center frequency` |
| 1090 MHz | recorded (ADS-B is reachable, just under the gap) |
| **1150 MHz** | **refused** -- inside the L-band gap |
| 1300 MHz | recorded |
| **1900 MHz** | **recorded** -- 134 MHz past what the profile claims |
| **2100 MHz** | **recorded** -- 334 MHz past |

Three consequences, none of them cosmetic:

**The reach belongs to the tuner, not to the RTL2832.** A profile named for the
demodulator carries the tuner's limits, so `survey_bands.h` would offer this
receiver everything from 24 MHz up -- short wave, CB, the 6 m and 10 m amateur
bands, band I television, none of which it can tune -- and would withhold
1766-2212 MHz, which it can. "A picker offering bands the receiver cannot
tune" is one of the five faults `screenshot`'s skill lists as having shipped
here before, and this is a second way to produce it.
`check-survey-bands` runs its both-directions property against two profiles
precisely so a single device's numbers cannot pass for the truth -- and
neither of those two profiles is this one.

**The L-band gap cannot be expressed**, exactly as librtlsdr's 300 kHz - 900
kHz rate hole cannot. `device_profile.h` refuses to invent a representation
for one device's discontinuity, and that refusal is now paid for twice by two
different devices. It is the same argument and it may be time to reopen it
with two instances instead of one.

**And this tuner refuses what the other one accepts.** `issues/10-*` in
`.scratch/deepening/` recorded, from phase 1a, that "the RTL-SDR backend does
not refuse an unreachable setting -- 10 Hz and a rate inside librtlsdr's own
hole both return success and read back", and concluded that
"Receiver rejected ..." is a message for a failure this device does not
produce. **On the E4000 it is produced**, twice above. The refusal path is
reachable, is not dead code, and the rollback behaviour
`check-receiver-runtime` drives against a fake device now has a real device
that exercises it.

## The artifacts: what transfers and what does not

Uncorrected probes, 2 MHz windows, 976.6 Hz bins, 1.0 s dwell, default gain on
each (29.7 dB on the R820T, 29.0 on the E4000 -- the tables differ, so
identical gain is not available and **absolute levels are not comparable**).
What is comparable is *presence* and *where a line reads*.

| nominal | what it is | R820T | E4000 |
| --- | --- | --- | --- |
| 115.200 | 1.6 x 72, **and 28.8 x 4** | exact, -30.1 | **exact, -11.1, 36.4 dB** |
| 120.000 | 1.6 x 75, ladder | exact, -28.0 | **exact, -31.0** |
| 136.000 | 1.6 x 85 | exact (recorded 2026-09-11) | **exact, -49.7** |
| 240.000 | 1.6 x 150, ladder | exact, -30.4 | **exact, -32.4** |
| 480.000 | 1.6 x 300, ladder | exact, -25.8 | **exact, -31.0** |
| 960.000 | 1.6 x 600, ladder | exact, -48.0 | **exact, -24.5** |
| 300.000 | on neither comb | exact (2026-09-11) | **exact, -41.6** |
| 72.000 | **14.4 x 5**, odd | present, 25.4 dB (2026-09-12) | -2441 Hz, **not coherent** |
| 129.600 | **14.4 x 9**, odd | exact, -47.4 | **absent** |
| 158.400 | **14.4 x 11**, odd | present, 15.1 dB (2026-09-12) | **absent** |
| 187.200 | **14.4 x 13**, odd | present, 10.2 dB (2026-09-12) | -5371 Hz, **not coherent** |
| 60.000 | ladder | exact, -27.5 | absent -- but 8 MHz above this tuner's floor |
| 30.000 | ladder | exact, -50.6 | **out of reach** |
| 75.000 | the unexplained family | exact (2026-09-11) | **absent** |
| 135.000 | the unexplained family | intermittent | **absent** |
| 150.000 | the unexplained family | exact (2026-09-11) | **absent** |
| 540.000 | the unexplained family | intermittent, marginal | -18 kHz, **not coherent** |
| 97.400 | an FM station **(control)** | displaced, external | displaced, external |

### The fine comb transfers; the coarse one does not

Six multiples of 1.6 MHz read on their exact nominal on **both** receivers.
That is the first evidence that the 1.6 MHz family is a property of this part
rather than of one unit, and `survey_fine_comb_spacing_hz()` is measuring
something real.

The odd multiples are recorded as *present* rather than *exact* on the R820T
deliberately: the sweep that established them found five and put **three of
the five** within a bin of the coherent prediction, which is the ticket's own
wording and is weaker than the six fine-comb lines above, every one of which
reads on its nominal.

**Every odd multiple of 14.4 MHz is absent or displaced on the E4000** --
72, 129.6, 158.4, 187.2, all four of them -- while 115.2, which is 14.4 x 8
and therefore **28.8 x 4**, is the strongest line the second receiver has in
that range. This is the sharpest result here, and it lands exactly on a doubt
already written down: `issues/10-*` records that the unplug sort which
originally established the coarse comb left only **even** multiples standing
(489.6, 547.2, 604.8 -- 14.4 x 34, 38, 42, so 28.8 x 17, 19, 21) and says
"the subset made and heard entirely inside the receiver may be the 28.8
multiples specifically".

A second receiver now says the same thing from a different direction.
`survey_comb_spacing_hz()` returns `reference / 2`, and on this evidence the
half is the R820T board's and not the family's. **It is not yet a reason to
change the constant** -- one board, one site, one antenna position, and an
absence is weaker than a presence -- but it is the first measurement that
could ever have distinguished them, and it points one way.

### The unexplained families do not transfer at all

75, 135, 150 and 540 MHz -- the two families `issues/10-*` has been reopened
over -- are **absent or displaced on the E4000**. Whatever generates them is
on the R820T board, not in the chip family. That does not identify them, and
it does not make them less real; it removes "a property of the RTL2832U" from
the list of things they could be.

### The data path is identical

The envelope line at one per **256 sample pairs** (`issues/13-*`) is on both
receivers, at both sample rates, to the resolution of the measurement:

| | 2.000 MS/s | 2.048 MS/s |
| --- | --- | --- |
| R820T | 7812.50 Hz, 132x median | 8000.00 Hz, 154x |
| **E4000** | **7812.50 Hz, 150x** | **8000.00 Hz, 166x** |

`rate / peak = 256.000` in all four. Same channel, same antenna, four seconds
each. **So it is not the tuner and not one unit** -- it belongs to the
RTL2832/USB data path, which is what ticket 13 proposed and could not show
with one receiver. Its "different host, port, hub, machine" experiment is
still owed; this settles the tuner half of it for free.

### The front end is not the same instrument

A band II sweep finds the same stations on both and **ranks them differently**:
the R820T's strongest is 97.4006 MHz and the E4000's is 100.3035, with 97.4
sixth. Absolute levels are not comparable across two gain tables, but the
*order* is, and it is not preserved.

This is why `installation.h` keys the survey history on receiver **and** site
**and** antenna (ADR-0022). Swapping the dongle here would have made stations
appear and vanish and the strongest carrier at this site change identity, and
a history that could not tell that from a change on air would have reported
it as one.

## What this does not establish

- **Absence is weaker than presence.** The gain tables differ, the front ends
  differ, and a line under the detection bar on one receiver is not a line
  that is not there. Every "absent" above is "not found at 8 dB prominence in
  a 2 MHz window at default gain", not "does not exist".
- **60 MHz is confounded.** It is 8 MHz above this tuner's floor, where its
  performance is poor, so its absence cannot be read as a fact about the
  ladder.
- **The reference clock was assumed, not measured.** Both are taken to be
  28.8 MHz. The crystal *errors* were measured; the nominal was not.
- **One board each.** Two units do not separate "this part" from "these two
  units" any more than one separated it from one -- they only make the second
  hypothesis less comfortable.
- **This is not the second backend.** Both are librtlsdr through
  `backend_rtlsdr.c`, so nothing here exercises `device_backend.h`'s vtable in
  a way one receiver did not.

## What follows

- `device_profile_rtlsdr()` describes an R820T. Whatever the fix is -- a
  tuner argument, a second constructor, reading the tuner from librtlsdr --
  the reach, the gain table and the refusal behaviour all belong to the tuner.
- `check-survey-bands`' two profiles should probably become three, with this
  one's numbers, since the E4000's reach contains neither of the existing two.
- The L-band gap is a second instance of the discontinuity `device_profile`
  refuses to represent. Two is a different argument from one.
- `issues/10-*` can drop "a property of the RTL2832U" from the candidate
  explanations of the 75/135/150/540 family.
- Whether `survey_comb_spacing_hz()` should be `reference / 2` at all is now
  a question with evidence on both sides of it rather than one.
