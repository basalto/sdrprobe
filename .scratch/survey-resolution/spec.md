# A full-tuner sweep cannot resolve most of what it reports

The sweep that produces the site baseline runs 24-1766 MHz in 8192 bins, so a
bin is **212.6 kHz**. That is wider than a whole land-mobile channel, wider
than a TETRA carrier, and about twice a GSM one. Everything narrow that the
survey reports from a full-tuner sweep is therefore a claim the sweep had no
resolution to make.

Measured 2026-09-06, by re-sweeping four allocations at 3-21 kHz resolution
with `--survey-confirm` and comparing against
`surveys/2026-09-05-212301-24M-1766M.json`:

| band | full sweep said | at real resolution |
|---|---|---|
| 148-175 MHz land mobile / marine | 16 candidates | **1 carrier**, 172.800 MHz = 6 x 28.8 |
| 400-406 MHz met aids | 1 | 403.200 (14 x 28.8), one refuted |
| 440-470 MHz | 6 | 460.800 (16 x 28.8), plus 467.77 real |
| 118-137 MHz airband | 3 | 120.000 (on the comb), 129.76 real, two refuted |

The one carrier left in the whole of VHF land mobile is the receiver's own
crystal. There is no land mobile here, no marine VHF and no AIS, and the
baseline says there are sixteen signals.

## Why this is not the comb ticket

`.scratch/receiver-comb/issues/02` is about a flag that is switched off at
coarse bin widths. This is about the measurement underneath the flag: even
with every suspect correctly marked, a 212 kHz bin cannot say whether a
maximum is one carrier, five, or a ripple in the noise floor. The two
interact -- the comb flag needs a width to decide, and the width is what a
coarse sweep does not have -- but fixing either leaves the other.

## Why it matters

`docs/band-surveys.md` says the survey exists to be diffed. A baseline whose
weak entries are mostly artefacts diffs into noise: signals appear and vanish
between sweeps because they were never resolved, not because the band changed.
It also wastes the reader, which is what happened here -- the question "what
else can I decode?" was answered from a list of allocations that turned out
to be empty.

## What is not wrong

The sweep is not miscounting. Given 8192 bins over 1742 MHz the arithmetic is
right, and a full-tuner sweep is genuinely useful for the strong, wide things
-- FM, television multiplexes, cellular downlinks, TETRA. Those were all
confirmed at fine resolution. The fault is that the report presents a 212 kHz
maximum and a 3 kHz one in the same list with the same fields.
