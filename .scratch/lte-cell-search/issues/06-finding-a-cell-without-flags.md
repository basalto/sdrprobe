# 06 — Finding a cell without restarting the program

Status: resolved
Blocked by: (none)

The view could only ever see a cell if the program had been started with
`--earfcn`. Two things stood in the way at runtime, and both had to go.

**The sample rate.** LTE is the 1.92 MS/s grid and nothing else (ADR-0014),
and nothing in the running program could change a rate: the Settings panel has
frequency, gain and PPM, and `start_acquisition` reads whatever was applied at
startup. So the view now borrows the receiver -- `enter_lte` records the
tuning and the rate and switches to LTE's, `leave_lte` puts both back -- which
is what the GSM view already does for the tuning alone.
`retune_receiver_at_rate()` carries the change, sharing every line of the
rollback with the plain retune, because a refusal has to put back whichever of
the two had already moved.

**Knowing where to tune.** An LTE carrier has to be found to within a few
kilohertz, and the raster is 100 kHz, so a band is three hundred tunings. That
is the whole content of `src/lte_scan.h`: the order. Whole megahertz first,
because operators centre carriers in blocks allocated from a band edge and
that lands them there -- the live band 20 carriers here are at 796, 806 and
816 MHz -- then the halves, then the rest. The first pass is thirteen seconds
of the hundred and thirty, and in practice it finds everything.

## What the first live scan taught

It reported five cells. Two were real. Three were on frequencies no carrier is
centred on, with margins a hair over the gate. And a real carrier at 816 MHz
was missed.

Three hundred channels is three hundred chances to be wrong, and a threshold
chosen for "is there a cell in front of me" is far too loose for that. The fix
is not a higher threshold -- that loses the weak real ones -- but a second
look: an identity is listed only once it has repeated on the same channel.
Noise clears the gate often enough to matter; it does not clear it twice with
the same identity out of five hundred and four. The next live scan listed the
strong cell and dropped all three false ones.

The cost lands where it decides something: a channel with nothing on it gets
its two looks and moves on, and only a channel that has already said something
is asked again.

Two things are honestly still imperfect, and are in TODO.md rather than
papered over: a cell that only decodes in half its blocks is missed about a
quarter of the time, and a repeatable artefact would survive the confirmation.

## Also

`--lte-scan <band>` runs the whole thing headless and prints what it found,
for the same reason `--survey` exists: the scan is otherwise a button, and
ADR-0012 does not accept a decision that needs a person to click it. It is how
both of the observations above were made.
