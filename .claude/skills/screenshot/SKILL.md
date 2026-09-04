---
name: screenshot
description: Capture what the sdrprobe window draws as a PNG and read it back. Use when a change touches a view, chart, panel or layout, when checking that something renders, or when asked what a screen looks like.
---

# Seeing the window

`sdrprobe` draws to a window and its output says nothing about what appeared.
`--screenshot` writes the last frame to a PNG, which the Read tool displays.

## Every screen, in one command

```sh
make screens          # build/screens/*.png, from captures, about a minute
```

Twelve screens: the four Scope views, the survey, the three decode views, the
calibration overlay in both arrangements, settings and help. All from captures,
so none of it needs a receiver and all of it draws the same picture twice.

**Look at the ones your change touched, and at their neighbours.** A change to
one panel moves the ones below it.

## What a screenshot catches that a check cannot

`check-layout` compares rectangles. These all shipped in this program and all
were plain in a picture:

- **Two things drawing into the same rectangle.** They agree about where they
  are, so comparing rectangles cannot see it. A cell picker and a waterfall
  were both drawing into the calibration overlay's chart.
- **A control offering values that cannot work.** The LTE calibration's band
  buttons offered bands 1, 3 and 7 -- correct geometry, and an R820T cannot
  tune any of them.
- **Text that is wrong rather than misplaced.** A field reading "N/A" under an
  EARFCN caption; a status line saying "Select GSM 900 ARFCN" under the 4G
  arrangement.
- **A frame that never drew.** The help overlay exported pure black while the
  compositor could be photographed drawing it perfectly.

So: geometry checks for where things are, a screenshot for whether the screen
is right. Neither substitutes for the other.

## The commonest fault here: a correct decision nobody consumes

Four of these shipped in one week, and none was an arithmetic error. In every
case the decision was right, the value reached the drawing, and the drawing
ignored it -- so the code reads correctly at both ends and does nothing in the
middle.

- **A flag from another feature gating it.** The waterfall's zoom was
  conditional on `calibration_mode`. The overlay was the only caller that
  zoomed for as long as that went unnoticed; when the Scope's window arrived
  and asked properly, it was discarded. The drag worked throughout.
- **The consumer reading a different rectangle.** The hit test mapped the
  pointer across the rectangle the chart was *handed* while the trace is drawn
  inside a caption strip and a label gutter -- every drag about 50 kHz left of
  where it looked. The highlight used the same wrong mapping, so the two
  agreed with each other and only disagreed with the chart.
- **Nothing drawing it at all.** The waterfall had no drag fields, so it
  zoomed on release with no feedback on the way.
- **A threshold never reached.** The FM band scan visits a carrier for 0.7 s
  and the RDS decode works in chunks of about 1.7 s of baseband, so every
  visit gathered a quarter of a chunk, decoded none of it, and reported all of
  band II as carrying no RDS -- while the panel below the list was reading a
  station by name.

**Before believing a value is used, find its consumer.** `grep` for the field,
not for the function that sets it. A field that is written and never read
compiles, tests green, and draws nothing.

**Then make it unrepresentable rather than fixed.** One function both sides
call beats two correct copies: `sdrgui_spectrum_area()` and
`sdrgui_waterfall_area()` are called by the drawing *and* the hit test, so
they cannot disagree.

## When something "does not work", instrument the funnel

Which stage stops is the diagnosis. Guessing costs a day; a funnel line costs
one run.

The LTE chain prints one per stage already. The FM band scan did not, and
adding it -- baseband gathered, baseband consumed, bits, blocks matched,
groups -- turned a day of suspects into a single line reading `bb 8947
consumed 0 bits 0`, which named the fault outright.

Split the question the same way the headless paths do. `fm_rds_tsf.bin`
decoding correctly while a live station read nothing halved the search
immediately: the subcarrier recovery, timing search, synchroniser and group
parser were all intact, so the fault was between a live receiver and that
chain, or in the view driving it. **Know what the headless run does not rule
out**: file playback is lossless and unpaced, so it proves the chain with
every block present and no deadline -- the two guarantees a receiver does not
give.


## The debug log says what actually arrived

```sh
./sdrprobe --view fm --duration 20 --debug-log /tmp/run.log
```

One line per key, click, retune, and screen change -- and for a key, the
handler the router sent it to and the screen it landed on. Since keys cannot
be injected (below), this is the only way to tell "the key never arrived" from
"it arrived and nothing was bound to it". Ask the operator for it before
guessing.

## Keys cannot be injected here

`wtype` does not work for this. It synthesises a keysym on a scratch keycode
of its own, and raylib reads *physical* keycodes -- so whatever key is asked
for, the program sees something else. Measured: `-k h`, `-k 3` and `-k 2` all
produced the behaviour of Escape or nothing at all, in three different views,
including views this repository has not touched. `-P key -s 200 -p key`, which
holds the key long enough to cross a 60 Hz poll, changes nothing about that.

`/dev/uinput` is root-only on this machine, so the honest alternative is not
available either.

So a keyboard interaction is verified by its unit check and by reading the
handler, not by driving it -- and a screenshot after a synthetic keypress is
evidence about which key raylib decided it received, not about which key was
sent. Say which of those you have.

## Screenshot to see it, headless to read it

A screenshot answers **did it draw, and does it look right**: layout, charts,
colours, empty states, whether a panel is where it should be.

For **values**, the headless paths are exact, faster, and already asserted by
`check-pipelines` — `--decode`, `--survey`, `--lte-scan`. Reading a number off
a screenshot is the mistake to avoid: panel text truncates when the window is
narrow, and a misread digit looks like a decode bug.

## Steps

1. **Run with a source, a view, a clock and a path.** The clock is required:
   the frame is captured as the run ends.

   ```sh
   ./sdrprobe --file testfiles/lte_b20_pci28.bin --view lte --earfcn 6200 \
       --duration 6 --screenshot /tmp/shot.png
   ```

   It prints `Wrote /tmp/shot.png (936x1018)` to stderr, or says it could not.
   Absolute paths are fine.

2. **Read the PNG.** The Read tool renders it.

3. **Say what is on it**, not that a screenshot was taken. The image is not in
   the conversation for anyone else, so describe what the change did to the
   screen.

## Recipes

Each puts real decoded data on screen from a capture, so the result is the
same every run. All are verified.

| View | Command tail after `./sdrprobe` |
| --- | --- |
| LTE | `--file testfiles/lte_b20_pci28.bin --view lte --earfcn 6200` |
| GSM | `--file testfiles/gsm_arfcn_69.bin --view gsm --arfcn 69` |
| ADS-B | `--file testfiles/adsb_cpr_pair.bin --view adsb` |
| Scope | `--file testfiles/gsm_arfcn_69.bin --view spectrum` |
| Survey | `--file testfiles/gsm_arfcn_69.bin --frequency 948.4M --view survey` |
| Calibration | `--view calibration` (add `--calibrate lte` for the 4G arrangement) |

`--view` also takes `magnitude`, `scatter` and `waterfall`. A live receiver
works in place of `--file`, and changes between runs, so prefer a capture
unless the point is live behaviour.

## How long to run

`--duration` is the settle time as well as the clock, because the frame
captured is the last one.

- **6 seconds** fills the decode panels. A block covers 65.5 ms, and the LTE
  and GSM views need a few of them before their panels say anything.
- **20 seconds** gives a waterfall visible history. Shorter leaves it mostly
  black, which reads as a fault and is not one.

## Asking for a window size

A tiling compositor gives the window whatever the layout has spare, so a plain
run is not the same size twice and panel text truncates when the tile is
narrow. `scripts/screenshot.sh` floats the window and asks for a size before the frame is
captured:

```sh
./scripts/screenshot.sh 1500 950 /tmp/shot.png \
    --file testfiles/gsm_arfcn_69.bin --view gsm --arfcn 69 --duration 8
```

**1500x950 fits every view.** Ask for more when a chart wants the room: the
window may end up larger than the screen and the capture is still whole,
because it comes from the program's own framebuffer rather than from the
display. The width and height are logical, and the PNG arrives multiplied by
the monitor scale -- 1500x950 at scale 1.25 is an 1875x1188 image.

Truncated captions mean the tile was small, not that the drawing is wrong.

The resize needs Hyprland. Without `hyprctl` the script still captures, at
whatever size the layout gave it.

**The script is flaky and fails visibly.** It sometimes prints `no sdrprobe
window appeared; capturing unsized` and writes nothing, or writes a capture a
few hundred pixels wide -- a PNG of 3 KB rather than several hundred. Check
the reported size and the file size, and re-run; it usually succeeds the
second time. A tiny capture is the harness, not the drawing.

**A run can click your program.** Twice in one session an automated capture
left a stepper pressed and a changed value written to
`~/.config/sdrprobe/config`. If a persisted setting looks wrong after a batch
of captures, suspect this before suspecting the code, and check the config
before drawing conclusions from a screenshot of it.

## A window someone already has open

To photograph a window rather than launch one, take its geometry from the
compositor and hand it to `grim`:

```sh
geom=$(hyprctl clients -j | python3 -c "
import json,sys
for c in json.load(sys.stdin):
    if c.get('class','').startswith('sdrprobe'):
        x,y = c['at']; w,h = c['size']
        print('%d,%d %dx%d' % (x,y,w,h)); break")
grim -g "$geom" /tmp/shot.png
```

This needs the window mapped on the active workspace; `--screenshot` does not,
which is why it is the first choice.

## When there is no display

`--screenshot` needs a window, so it needs a graphical session: check
`WAYLAND_DISPLAY` or `DISPLAY` is set. Without one, say so and use the
headless paths — every decision the program makes is reachable without a
window by design (ADR-0012), and drawing is the only thing that is not.
