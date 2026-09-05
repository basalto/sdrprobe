# 02 — Find the synchronisation burst

Status: ready-for-agent
Blocked by: 01

A TETRA downlink is continuous, so there is no silence to find a burst against
-- the structure is entirely in the symbols. The synchronisation burst carries
a known training sequence, and correlating for it is what turns a stream of
dibits into frames and timeslots.

This is the closest thing here to `gsm_dsp`'s FCCH and SCH: a known sequence,
found by correlation, that establishes where everything else is. The difference
is that GSM's frequency correction burst is a pure tone and can be found by
looking at the spectrum alone; TETRA has nothing of the kind, so correlation is
the only way in.

## What must be checkable

- The training sequence correlates to a sharp peak on synthetic symbols at a
  known offset, and its runner-up is far below -- the same shape the LTE
  primary sequence is checked with, and for the same reason.
- On the real capture, bursts land on a **56.67 ms** grid and hold that cadence
  across the whole three seconds. A correlator that finds a peak per burst but
  cannot keep the cadence has found noise that fits.
- The peak survives the frequency offset ticket 01 measures, since that is what
  a real capture has.
