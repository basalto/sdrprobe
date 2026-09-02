---
name: rf-environment
description: Say what is on air here, what changed since the last sweep, and what is worth decoding next, from the accumulated surveys in surveys/. Use when asked about the RF environment, after recording a survey, or when choosing the next technology to work on.
---

# Reading the environment

`surveys/` holds one JSON per sweep so they can be compared. A single survey
says what is transmitting; a series says what changed, which is the question
worth asking.

## Steps

1. **Report the newest survey.**

   ```sh
   ./scripts/survey_tool.py report surveys/<newest>.json
   ```

   It groups candidates by the band plan's name for each frequency, which is
   what turns a list of 247 peaks into a finding. Candidates flagged as
   resembling the receiver are set aside and counted, never dropped.

2. **Diff against the one before**, if there is one.

   ```sh
   ./scripts/survey_tool.py diff surveys/<older>.json surveys/<newer>.json
   ```

   Only where the two overlap in frequency. Read "gone" with suspicion: a
   0.12 s dwell catches a bursty transmitter about as often as it misses it, so
   an absence in one sweep is weak and an absence in two is worth something.

3. **Find out what is already settled** before treating anything as new. Each
   `.scratch/<effort>/spec.md` says what it covers and whether it closed;
   `TODO.md`'s Implemented section is the shortest orientation. Reopening a
   closed question is the expensive mistake here, and the closed ones are
   closed for reasons that have not changed.

4. **Put any candidate for new work through both gates below** before
   proposing it. Both, every time, and in this order.

5. **Recommend with the measurement attached.** "FM is strongest and carries
   RDS, pilot 32 dB over noise and the 57 kHz band 10 dB over it" is a
   recommendation. "FM is a good next step" is a guess wearing its clothes.

## Two gates, and why they exist

Both were learned by getting it wrong, one of them at the cost of a ticket and
an afternoon.

### Does it fit the receiver?

An RTL-SDR manages about **2.4 MS/s** before it drops samples, 3.2 at its
limit. Work out the bandwidth of the thing that must be captured *whole* --
not the channel, the specific signal a decoder has to see in one piece -- and
compare.

5G NR on n28 failed here, and only after a ticket had been written for it. Its
synchronisation signals are 127 subcarriers, which is 1.905 MHz at 15 kHz
spacing and fits, but 3.81 MHz at 30 kHz and does not. The spacing was assumed
to be 15 kHz from memory; it was measured at 30. `.scratch/nr-cell-search/`
has the whole of it.

DVB-T fails the same gate at 8 MHz, permanently.

### Is it actually there?

**An allocation is a lookup, not an identification** (ADR-0015). The survey
prints the allocation a frequency falls in and can say nothing about what
occupies it -- band 28 is labelled an LTE downlink and carries 5G NR.

So measure before believing the label:

```sh
make probe-periodicity FILE_PERIODICITY=captures/x.bin
```

Two lag correlations, no sequence model, immune to the tuning error: a burst
folded over its own period says LTE (every 5 ms) or NR (every 20, and nothing
at 5), and the cyclic prefix names 15 or 30 kHz spacing. For a technology with
a different frame period, fold at that period instead -- what matters is a
peak far above its own floor at a phase that holds.

DAB+ failed this gate. Band III has 17 candidates, 1.536 MHz fits comfortably,
and the OFDM machinery would have carried over almost whole -- it was the
obvious recommendation. Folding at DAB's 96 ms frame, where the phase
reference symbol must repeat identically, gives 1.4 times its floor. There is
no DAB here.

For something without a burst structure, measure the payload directly: RDS was
confirmed by finding the 19 kHz pilot and the 57 kHz band above the noise. Note
that both the stereo and RDS subcarriers are *suppressed*, so a measurement at
the exact frequency finds nothing by design -- integrate a band either side,
against a band of the same width where nothing should be. Getting that wrong
the first time reported no RDS on a station that plainly has it.

## Then judge it as work

Once both gates pass, rank on what the repository actually gets:

- **A message, not a waveform.** The technologies here end in something the
  transmitter is *saying* -- MCC/MNC/LAC/CI, a Master Information Block, an
  aircraft position. Something that demodulates to audio is a demodulator, and
  sits oddly beside the rest.
- **Reuse.** An OFDM technology inherits the FFT, the resource grid and the
  Viterbi; a burst technology inherits the correlator. Reuse is worth real
  weight, but never enough to survive a failed gate -- DAB had the best reuse
  of anything considered and is not there.
- **Whether it stays on air.** Two of the three GSM captures cannot be
  retaken: the cells were refarmed away mid-project. A permanent transmitter is
  worth more than a stronger temporary one.
- **Whether it is anyone's to decode.** TETRA is present and its
  synchronisation burst is close kin to GSM's SCH, so it would reuse a great
  deal. It is emergency-services spectrum, and the question of what may be
  decoded from it deserves an answer before code rather than after.

## Recording another sweep

```sh
./sdrprobe --headless --survey --survey-range 24M:1766M --survey-dwell 0.12 \
    | ./scripts/survey_tool.py ingest --note "antenna, gain, where, why"
```

About four minutes. The note is the part a later reader cannot recover, and
levels only compare between sweeps taken the same way. `surveys/README.md` has
the file format.
