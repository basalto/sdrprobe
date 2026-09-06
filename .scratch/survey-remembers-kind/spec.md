# A survey that remembers what kind of thing a signal was

`.scratch/signal-probe/` built the measurements that say what a signal *is*
rather than that it is there -- a bare carrier against a modulated one, how
long a burst lasts, how much the envelope varies. They are reachable from one
place: select a candidate in the survey window and read the panel.

So they are lost the moment the window closes. A saved survey records
frequency, level, width, shape, suspicion and confirmation, and none of what
this program now knows about kind. Nothing headless produces them, and
`survey_tool.py diff` cannot compare them between two sweeps.

That is the wrong shape for what the program is for. The README says it
surveys the environment "to identify signals over time" and "saves information
about the environment to compare historical data", and **"this carrier was
bare in June and modulated in September" is a finding no single sweep can
reach** -- the same argument that made the site history worth having, and the
same one that makes `by hour` worth more than `on/off`.

## Where it goes

The confirmation pass. It already visits each signal with the receiver settled
and six blocks to spend (`SURVEY_CONFIRM_LOOKS`), which is more than the two
seconds the window's own measurement gets. The measurements come nearly free.

## The one thing that has to change first

**The pass tunes directly to the candidate, so the candidate lands on 0 Hz**,
and `signal_find_carrier()` takes a guard around DC precisely so it cannot
lock onto the receiver's own offset. Guarded, it would skip the very signal it
was pointed at; unguarded, it would find the DC spike -- which is the mistake
that cost `.scratch/signal-probe/` ticket 01 four attempts.

`survey_select()` already tunes `SURVEY_OFFSET_HZ` below a candidate for this
reason and says so. The pass has to do the same.

### What that costs, measured rather than assumed

It was worth checking, because an earlier version of this spec asserted the
opposite: that tuning *onto* the candidate must be losing about 9 dB, since
the default-on filter removes DC. A synthetic said so. The synthetic was a bad
model -- it put the carrier at exactly 0 Hz, where the sample mean *is* the
carrier and `sdr_dsp_remove_dc()` removes it.

On air, the same 75.0005 MHz carrier recorded at both tunings, through the
pass's own path:

| tuning | prominence | peak |
| --- | --- | --- |
| tuned to it, as the pass does | 21.9 dB | -85.8 dBFS |
| tuned 300 kHz below | 20.1 dB | -88.2 dBFS |

Tuning onto it is 1.8 dB *better*. A carrier at the tuned frequency sits at
the tuning error -- hundreds of hertz at least, which is many cycles in a
block -- so its mean is near zero and DC removal does not touch it. The
mechanism would need the receiver calibrated to a tenth of a part per million.

So moving the tuning off costs about 2 dB of prominence and buys the carrier
search. That is the trade, it is small, and `SURVEY_CONFIRM_PROMINENCE_DB` is
6.0 with the sweep's own bar above it, so 2 dB does not reach anything.

## What must be checkable

Everything the pass decides, with no window and no receiver (ADR-0012), as
`check-survey-confirm` already does for the verdicts. What cannot be checked
that way is whether the numbers are right on air, and the two captures above
are the shape of that evidence.

## What this must not become

A verdict in the history. The site history records what was heard; this
records what it measured to. "Bare in June, modulated in September" is a
comparison a reader makes from two measurements, not a claim the program
makes -- the same line `signal_findings.h` already holds.
