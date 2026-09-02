# 01 — Does an n28 synchronisation block fit this receiver?

Status: ready-for-agent
Blocked by: (none)

The whole effort turns on one number: the subcarrier spacing n28 uses for its
synchronisation block.

| spacing | primary/secondary signal | fits 1.92 MS/s? |
| --- | --- | --- |
| 15 kHz | 127 * 15 kHz = 1.905 MHz | yes, barely |
| 30 kHz | 127 * 30 kHz = 3.81 MHz | no, and no rate this dongle has would |

38.101-1 lists which cases a band supports, and low FR1 bands are usually
Case A, which is 15 kHz. **That is a recollection, not a citation** -- find the
table, name it, and quote the row for n28. If it is 30 kHz, this effort stops
here and the spec says so; that is a perfectly good outcome for a day's work
and much better than discovering it after writing a detector.

## Then take a capture

Independently of the tables, record what is actually on air, because the answer
has to survive contact with the operator's real configuration:

    ./sdrprobe --headless --record-seconds 2 --technology lte --frequency 774.2M

774.2 MHz is the strongest thing the survey found in the band. The sidecar
conventions are in AGENTS.md; `--technology lte` is the closest existing label
and the note field should say plainly that the capture is of a suspected NR
carrier, not an LTE one.

What to get out of it:

- **Is there a repeating structure at 20 ms?** That is the default period for a
  synchronisation block, and nothing in LTE repeats at that interval. It is the
  cheapest positive evidence that the occupant is NR, and it needs no sequence
  model at all -- which is exactly the kind of measurement the LTE work learned
  to start from.
- **How wide is the occupied energy?** A 10 MHz NR carrier and a 10 MHz LTE
  carrier look similar in a survey, but LTE puts a strong narrow spike at the
  carrier centre every 5 ms and NR does not.

## Answer

(record what the tables say, what the capture shows, and whether 02 is worth
opening)
