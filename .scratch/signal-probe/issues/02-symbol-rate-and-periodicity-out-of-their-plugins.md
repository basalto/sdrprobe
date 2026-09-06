# 02 - Move the general measurements out of the plugins

Status: needs-triage

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
