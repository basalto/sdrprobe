# 02 - Move the general measurements out of the plugins

Status: resolved

Blocked on 01 only for ordering: this is the same unit, and doing it second
keeps the first change small.

## What moves

- **`tetra_symbol_timing()`** -- Oerder-Meyr, the symbol-rate line in the
  squared magnitude. Its own file says this "is the *same statistic* that says
  the carrier is TETRA at all", which is precisely why it belongs one layer
  down: it answers "does this have a symbol rate, and what is it" for a signal
  nobody has identified.
- **`tetra_burst_find()`** -- finds a burst grid from symbols alone. Its
  comment already says it "works before anybody knows which technology this
  is".
- **`lag_correlation()` and `folded_peak()`** from
  `scripts/signal_periodicity.c`, which are a `main()` today so nothing can
  call them. Folding at a period is how band 28 was found to be NR rather than
  LTE, and it is general.

## The rule that makes this worth doing rather than shuffling

A measurement belongs in `signal_probe` when it needs **nothing transcribed
from a standard**. Oerder-Meyr needs no sequence; a cyclic-prefix
autocorrelation needs no sequence; a Zadoff-Chu correlation needs the sequence
and stays in `lte_dsp`. That line is worth writing down, because without it
this becomes a junk drawer.

## What must be checkable

Each moved measurement keeps its existing checks and gains one that a
technology plugin cannot give it: the same function answering correctly for a
signal of a *different* technology. Oerder-Meyr on the GSM capture, folding on
the LTE one.

`probe-periodicity` keeps working, over the moved implementation rather than
its own copy.

## Comments

**2026-09-06 — done, with one thing deliberately not built.**

All three moved, and each kept its checks:

| moved | to | wrapper left behind |
| --- | --- | --- |
| `tetra_symbol_timing()` | `signal_symbol_line()`, with both rates passed | yes, holding TETRA's two constants |
| `tetra_burst_find()` | `signal_repeat_find()`, profile sized to SIGNAL_PROFILE_MAX | yes, copying the profile only for a timeslot |
| `lag_correlation()`, `folded_peak()` | `signal_lag_correlation()`, `signal_fold_at()` | none -- they were statics in a `main()` |

`check-tetra-dsp` passes 101 checks over the moved implementation, and
`probe-periodicity` reproduces its own numbers on `lte_b20_pci28.bin` exactly:
5 ms at 4.2 times its floor, the prefix at lag 128 for 15 kHz spacing, and the
same conclusion. `check-signal-probe` is 25 checks to 56, the new ones each
answering for something that is not the technology the measurement came from
-- a 50 kBd symbol line, a 148-symbol burst grid, a fold at 9600 samples.

### The blind symbol-rate search does not work, and is not shipped

The obvious next step from `signal_symbol_line()` is to search for the rate
rather than be told it. It was built, measured, and taken back out.

A plain "biggest line wins" search fails outright: the squared magnitude's
low-frequency skirt is larger than any symbol-rate line, so every capture
reports the bottom of the search range. Scoring each candidate against a
*local* floor instead -- the topographic-versus-floor distinction the survey
already makes -- fixes that and finds both TETRA captures correctly:

| signal | best | local ratio |
| --- | --- | --- |
| `tetra_cc17.bin`, channel at +10 kHz | 17998.0 Bd | 37.8 |
| `tetra_cc32.bin`, channel at 0 | 17998.0 Bd | 37.5 |
| **25 kHz slice of `gsm_arfcn_69.bin` at +300 kHz** | **3466.9 Bd** | **28.6** |
| empty channel, cc17 at -10 kHz | 2258.0 Bd | 5.5 |
| empty channel, cc17 at -30 kHz | 42028.5 Bd | 4.4 |
| empty channel, cc17 at +40 kHz | 29577.6 Bd | 3.5 |
| `adsb_cpr_pair.bin` raw | 80711.9 Bd | 3.6 |
| `fm_rds_tsf.bin` raw | 1750.0 Bd | 4.9 |
| `lte_b20_pci28.bin` raw | 44007.8 Bd | 10.1 |

Both TETRA captures land 0.01% from 18 kBd, and the empty controls through
the same filter sit at 3.5 to 5.5. That looks like a working detector until
the GSM row: **3466.9 Bd is twice GSM's 1733 Hz burst rate**, and 28.6 against
37.5 leaves no threshold with any margin.

It is not a bug. Oerder-Meyr detects periodicity in the squared magnitude, and
a burst grid *is* periodicity in the squared magnitude. Asked at a known rate
-- which is all `tetra_dsp` ever asked -- the statistic answers "are the
symbols here". Searched blind it cannot tell a symbol rate from a frame rate,
and separating the two needs something this measurement does not carry.

So `signal_symbol_line()` ships and the search does not. The header says why,
in the place someone would go looking for it.

### One bug found by writing the checks

`tetra_burst_find()`'s header has always said a refusal "reads as a period of
0 rather than as leftovers". The code it described set the period, the repeat
and the runner-up, and only then returned 0 -- so a caller trusting the
sentence read whichever lag had just won a search that was then rejected.
`signal_repeat_find()` decides before it writes, and a check asserts the
sentence. Nothing here read those fields on a refusal, so nothing was wrong on
screen; it was a loaded gun.

### An hour lost to the same mistake as last time

The first measurement run put `tetra_cc17.bin` through `tetra_channel()` at an
offset of 0 and reported a symbol rate of 1834.6 Bd. The carrier is at
**+10 kHz** -- the capture was recorded off-centre, the same as `cc32` at
392.8735 -- so the measurement was of an empty slice, and the "line" was noise
structure. It is the DC-offset lesson from ticket 01 in a different coat:
**isolate the channel before measuring it, and prove the isolation put the
signal in the passband.** The tell was the same tell, too -- a control reading
in the same range as the signal.
