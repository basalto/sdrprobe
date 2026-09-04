# The survey reports signals near the top of the tuner that are not there

The 2026-09-03 evening sweep lists five candidates between 1583 and 1604 MHz,
the strongest at **-32.6 dBFS with 37.7 dB of prominence** -- which would make
it the sixth-loudest thing in the whole 24-1766 MHz sweep. None is flagged as
resembling the receiver.

A confirmed re-sweep of 1570-1620 MHz at three times the dwell finds
**nothing at all**: zero carriers, zero candidates.

```
$ ./sdrprobe --headless --survey --survey-range 1570M:1620M \
      --survey-dwell 0.3 --survey-confirm
survey steps 32 bins 8192 bin_hz 6103.5 dwell 0.30 estimate_s 13
survey blocks 192
survey carriers 0
survey candidates 0 suspicious 0
```

The morning sweep of the same day lists one candidate in that range, at
1613.001 MHz -- a different frequency. Three looks, three different answers,
one of them "nothing".

## Why these cannot be what they are labelled

The band plan calls them `GNSS L1 / E1`, and that label is a lookup rather
than an identification (ADR-0015). Here the lookup is not merely unproven, it
is impossible: **GNSS arrives about 20 dB below thermal noise and is recovered
by correlating against a known code.** It cannot appear as a peak standing 37
dB above a local floor on any spectrum. Whatever produced these entries, it
was not a satellite.

## The tell nobody would have looked for

One of the five has **negative prominence**: 1603.219 MHz at -3.0 dB.
`survey_carrier.h` defines prominence as `peak_dbfs - floor_dbfs`, so a
negative value is a maximum below its own local floor -- which is not a weak
signal, it is an arithmetic impossibility for a real one. It is the only
negative prominence in either of the two full sweeps, and it sits inside the
cluster that does not reproduce.

## What this is not

Not the 14.4 MHz comb, which was the first suspicion and is handled correctly.
Nine candidates across the full sweep sit within 30 kHz of a multiple of
14.4 MHz -- half the RTL2832U's 28.8 MHz crystal -- including the loudest
entry in the entire survey, 230.373 MHz at -11.7 dBFS. **All nine are flagged
`reference` and excluded from the report's per-allocation bests.** The suspect
detection is doing its job; this is a different fault.

## Why it matters

The survey's whole purpose is a baseline that can be diffed
(`docs/band-surveys.md`). A candidate that appears at -32.6 dBFS in one sweep,
somewhere else in the next and nowhere in a confirmed third is noise in that
baseline, and it is the *loud* kind: it outranks real signals in the report
and would be the first thing a reader investigated.
