---
name: screenshot
description: Capture what the sdrprobe window draws as a PNG and read it back. Use when a change touches a view, chart, panel or layout, when checking that something renders, or when asked what a screen looks like.
---

# Seeing the window

`sdrprobe` draws to a window and its output says nothing about what appeared.
`--screenshot` writes the last frame to a PNG, which the Read tool displays.

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

## The window size belongs to the compositor

A tiling compositor gives the window whatever the layout has spare, so the
capture is not 1100x720 and is not the same twice if other windows moved. The
layout adapts — that is what `check-layout` pins — but panel captions and
values truncate when it is narrow.

Truncated text means the tile was small, not that the drawing is wrong. Either
ask for a wider window, or get the value from a headless run instead.

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
