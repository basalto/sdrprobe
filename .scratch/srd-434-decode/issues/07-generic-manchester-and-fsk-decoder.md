# 07 — Generic Manchester and 2-FSK decoder for SRD

Status: done

## What

The SRD module currently implements only OOK (on-off keying) envelope
demodulation with rigid, hardcoded delimiters and headers (`src/srd_dsp.c` and
`src/srd_frame.c`). The committed 2-FSK SRD remote-control capture
(`testfiles/srd_remote_control_fsk.bin`) refutes the assumption that 434 MHz
short-range devices are universally OOK.

Furthermore, the decoder must be **generic for any remote or sensor**, rather
than hardcoding specific sync words or byte lengths for tested remotes.

## Design

1. **Modulation Agnostic Front-End (`srd_dsp.c`):**
   - Transmissions are classified as OOK or 2-FSK by `srd_classify_modulation()`.
   - OOK demodulates envelope (`srd_demodulate_envelope()`).
   - 2-FSK demodulates instantaneous frequency (`srd_demodulate_fsk()`).
   - Slicing threshold in 2-FSK (`srd_discriminator_threshold()`) computes the
     midpoint between 10th and 90th frequency percentiles, adapting to any
     frequency deviation and center offset.
   - Dynamic chip period recovery (`srd_chip_period()`) recovers $T_{\text{chip}}$
     generically via histogram mode.

2. **Generic Manchester Frame Extractor (`srd_frame.c`):**
   - Discretises runs into binary chips based on $T_{\text{chip}}$.
   - Searches for valid continuous Manchester chip sequences ($01 \to 0, 10 \to 1$).
   - Synchronisation / frame detection works across:
     * Delimiter-based frames (e.g. 6 runs of 1.5 chips).
     * Preamble + Sync word frames (e.g. alternating preamble followed by sync
       marker such as `0x06C0D4` or standard sync words).
     * Generic valid Manchester packets of length $\ge 24\text{ bits}$ (3 bytes).
   - Known profiles (2-FSK SRD remote control, generic OOK) can populate specific fields; all others
     are decoded as `GENERIC` / `FULL` with decoded hex bytes and bit count.

3. **UI / Presentation (`view_srd.c`, `sdrprobe.c`):**
   - `MOD` column accurately reports `OOK` or `2FSK`.
   - Log displays decoded bytes in real time.

## Comments

**2026-09-15 — most of this shipped in `b52c73c` and the Status line did not
say so.** Everything the Design section names exists: `srd_classify_modulation`,
`srd_demodulate_envelope`, `srd_demodulate_fsk`, `srd_discriminator_threshold`
and `srd_chip_period` in `src/srd_dsp.{c,h}`, wired through `src/srd_session.c`;
`SRD_FRAME_GENERIC` in `src/srd_frame.h:51`; the `MOD` column reporting
`OOK`/`2FSK`. Parts 1 and 3 are delivered.

**Part 2 is the gap, and it is measured.** Headless over the 2-FSK SRD remote control capture
(`--technology srd --sample-rate 2000000 --frequency 434.4M`, 5.0 s):

One frame was reported, against **28 `WAKEUP` lines carrying preamble only**. The extractor
finds a valid Manchester packet once in five seconds and otherwise reports a
preamble it could not carry into a frame. Commit `0008494` said the same in its
subject -- "decode still open".

Three observations from that output, none of them yet a finding:

- **The chip period moves**, 23 us before 2.6 s and 64 us after. 64/23 is 2.8,
  not a clean multiple, so one of the two is the histogram mode landing on the
  wrong peak rather than two real rates.
- **The reported frequency splits into two clusters 520 kHz apart**, -118.8 and
  +401.2 kHz. Too wide to be one transmitter's 2-FSK deviation.
- **The preamble bytes are suspicious**: `55`/`AA` is alternating chips as
  expected, but `E3 8E 38` and `C7 1C 71` are a period-3 pattern, which is what
  a preamble read at the wrong chip period looks like.

The committed 2-FSK capture is required by the SRD DSP and frame checks.

---

**2026-09-15 (later) — done.** Part 2 built, and the gap was not where the
prose said it was.

**It was never a bit error rate.** `srd_dsp.h` recorded 10-13% bit errors on
the data bursts, from a transcript. `make probe-srd` — added for this, since
the question kept coming back — measures **8 or 9 Manchester violations in
about 378 chips**, under 5% of chip pairs, with 164 consecutive bits clean out
of about 189. A frame with a broken tail, not a noisy channel. That claim is
corrected in the header.

**Two defects, both in what a caller was allowed to see.**

1. `srd_extract_frames()`'s generic path kept the **single longest** unbroken
   Manchester stretch and discarded the rest, so a run stream holding several
   frames reported one. It now emits every maximal legal stretch, with the
   chip alignment chosen once per stream by total decoded bits — emitting from
   both phases would report every frame twice, once off by a chip. Each frame
   also carries its own `time_seconds` now, mirroring
   `srd_runs_to_chips()`'s expansion; a generic frame used to report 0.0
   whatever it was.
2. `srd_session_feed()` glued every transmission of a busy period into one run
   stream and only ever emitted frames when the count **grew** — which, given
   a path that could not return more than one, pinned it at one for the whole
   press. A transmission is decoded on its own runs now, and the carried tail
   survives only when the last transmission ran to within
   `SRD_GAP_SECONDS_DEFAULT` of the block's edge, which is the same gap
   `srd_find_transmissions()` groups by.

**Measured, same capture, same command, by stashing the change and
rebuilding:**

| | before | after |
|---|---|---|
| assembled program, headless `--once`, 2-FSK SRD remote control | 1 frame | 15 frames |
| assembled program, headless `--once`, `srd_remote_control_ook_a.bin` | 8 lines | 22 lines |

`probe-srd` calls `srd_extract_frames()` on **one transmission at a time**,
so it never saw the session's glued stream and its per-burst counts did not
move: 19 of the 2-FSK SRD remote control capture's 32 transmissions carry a frame, before and
after. That is the measurement that says the two defects were separable --
the extractor's was only reachable through a caller that hands it more than
one frame's worth of runs, which is what the session was doing.

*(An earlier version of this table claimed "`probe-srd`, whole capture: 1
frame -> 20 frames". Both numbers were wrong: the 1 was the assembled
program's figure attributed to the probe, and the 20 was an eyeball count of
22 lines, 3 of which were spurious frames on a neighbouring transmitter --
the ones ticket 08 is about.)*

The OOK capture moved because of the session half, not the extractor half: its
frames come from the delimiter path, and un-gluing the transmissions stopped
the junctions truncating them. The frames corroborate each other in a way
nothing here manufactures — every `REPEAT` frame's tag byte matches the tag of
the `FULL` frame preceding it, which is what `srd_frame.h` documents and what
no amount of repetition could fake.

On the 2-FSK protocol all 20 frames are **exactly 14 bytes**, share a constant
3-byte prefix and a constant pair at bytes 4-5, and change payload in groups
of about four. The decoder treats the changing payload as opaque.

**What it exposed:** ticket 08. Reporting every frame made a wrong chip period
visible as three spurious frames on a neighbouring transmitter, where before
it read as silence.

**Checks.** `check-srd-frame` gains `test_generic_path_emits_every_frame`
(three frames in one stream, payloads pinned, times increasing, caller's array
respected) and `test_preamble_alone_is_not_a_frame`; `check-srd-session` gains
`test_a_finished_transmission_is_not_carried`. Both were mutation-tested
against the old behaviour and fail on it — 2 and 3 assertions respectively.
Full gate green: 20634 checks in 68 suites.
