# Live RDS stopped reading, and the capture did not

Reported from a live session: the FM view decodes RDS on **no** station, where
it previously read several here.

## What is already known

The decode chain is not the fault. The capture still reads through it
end to end:

```
$ ./sdrprobe --file testfiles/fm_rds_tsf.bin --sample-rate 2048000 \
      --frequency 89.6M --headless --technology fm --decode --once
FM   station 0x8343  89.600 MHz
RDS  " TSF    "  news, carries traffic announcements  identification 0x8343 (19 agreeing)
```

So the subcarrier recovery, the timing search, the block synchroniser, the
group parser and the station model all work on stored samples. Whatever is
wrong is between a **live receiver** and that chain, or in the **view** that
drives it -- not in the DSP the checks cover.

That split is the whole value of the headless path, and it is worth saying
what it does *not* rule out: the capture is played back lossless and unpaced
(`acquisition_set_lossless()`), so it exercises the chain with every block
present and no real-time deadline. A live receiver has neither guarantee.

## The suspects, in the order they should be eliminated

1. **Dropped blocks.** The FM view accumulates *bits* across fixed
   non-overlapping baseband chunks, and radio text needs twenty-five seconds
   of groups. The render thread consumes from an overwriteable single slot
   (ADR-0002), so a slow frame drops a block -- and a dropped block in the
   middle of a chunk is not a gap in the bits, it is a discontinuity the
   synchroniser cannot see. `blocks published` against `processed` in the HUD
   says whether this is happening.
2. **A recent change to the frame loop.** Several landed in quick succession:
   the shared chart-key reader now drains `GetCharPressed()` once a frame, the
   Scope control row added a text-focus source, `calculate_plot()` moved every
   Scope chart down, and the option row grew a fallback. None of these should
   touch FM decoding, and that is exactly the kind of belief worth checking
   rather than asserting. `git log` since the last session where RDS was seen
   working is the list.
3. **Tuning or level.** The pilot's lock takes coherence *and* presence
   together, and both are reported. A station tuned 100 kHz off, or an AGC
   sitting somewhere unhelpful, gives a pilot that never locks and therefore
   no subcarrier at all.
4. **The station.** Least likely with *every* station affected, but the
   operator here has refarmed spectrum before -- two of the three GSM captures
   went off the air that way.

## How to tell them apart

`--debug-log` records retunes and screens; the FM view's own panels report
pilot lock, coherence, presence and the group funnel. The funnel is the
instrument: which stage stops is the diagnosis, exactly as it is for the LTE
chain. No pilot is tuning or level; a pilot without symbols is the subcarrier;
symbols without blocks is synchronisation; blocks without groups is the offset
order.

A live recording is the way to make it a check rather than a session:
`--record-seconds` writes a capture with a sidecar, and a capture that
reproduces the failure turns this into something `check-pipelines` can hold.
