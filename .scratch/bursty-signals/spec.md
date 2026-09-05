# A signal that comes and goes is a finding, and the survey calls it refuted

The confirmation pass answers one question -- was it there when I looked again
-- and the survey reports the answer as though it settled a different one: is
this a transmitter. For anything that transmits in bursts those are not the
same question, and the pass gets the second one wrong in the expensive
direction.

Measured on 2026-09-05 while resolving `.scratch/phantom-candidates/`. Five
identical sweeps of 1550-1766 MHz, minutes apart, same antenna and gain:

| sweep | candidates | where |
| --- | --- | --- |
| 1 | 0 | -- |
| 2 | 6 | 1612.2, 1623.6, 1624.5, 1633.1, 1636.2, 1649.1 |
| 3 | 6 | 1614.2, 1622.2, 1624.9, 1636.6, 1644.4, 1646.0 |
| 4 | 2 | 1612.2, 1632.5 |
| 5 | 2 | 1613.2, 1629.8 |

Fifteen frequencies, no frequency twice, all at 30 dB and more above their
floors. That is Iridium and the mobile-satellite uplinks: short bursts on
channels that move. Every one of them is a real transmission, and the
confirmation pass -- six blocks, about 0.4 s on the frequency -- refutes nine
of ten:

```
$ ./sdrprobe --headless --survey --survey-range 1400M:1766M \
      --survey-dwell 0.1 --survey-confirm
confirm-summary asked 10 confirmed 1 refuted 9
```

Over band II, where the transmitters are continuous, the same pass confirms 24
of 24. The pass is not broken; it is answering the only question it was built
to answer.

## Why "refuted" is the wrong word for it

`src/survey_confirm.h` says so itself, about the threshold: "being too strict
here would refute real signals, which is the more expensive error, because it
teaches the site that a real transmitter is noise." The site history takes the
pass's word (`survey_confirm_should_record`), so a refuted "new" is never
remembered -- and a bursty transmitter is refuted every time, so it can never
enter the history at all, however many sweeps hear it.

That also makes the mobile-satellite allocations permanently invisible to
`site_history_seen()`'s new/steady/on-off/gone, which is the one part of this
program that could describe them correctly.

## What already exists and is not wired to this

`survey_measure_duty()` and `survey_measure_duty_label()` in
`src/survey_sweep.h` return exactly the right words -- continuous,
intermittent, bursty -- from a count of blocks the candidate was up in. They
are used only by the two-second measurement of a hand-selected candidate in
the survey view. The confirmation pass counts nothing: it peak-holds six blocks
into one spectrum and asks once whether anything is there, which throws away
the count that would answer this.
