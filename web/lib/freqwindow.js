// Which part of a frequency range is on screen, and how zooming and panning
// move it -- the browser's copy of src/core/freq_window.h, ported arithmetic
// for arithmetic so the web Scope's zoom reads the same as the window's.
//
// The native file lives in `core/` as plain doubles with no receiver and no
// raylib, exactly so tests/freq_window_test.c can exercise it without a
// window; this is the same reasoning one language over. It is `lib/`'s own
// contract (web-view skill, ADR-0007): plain data and geometry, never a
// socket, a message or a view. The window object is `{dataLowerHz,
// dataUpperHz, viewLowerHz, viewUpperHz}` -- `data` is the range that
// exists (the received span), `view` the part of it drawn, never wider than
// the data and never narrower than min_span.
//
// The constants are chart_window.h's: a linear axis, so the Scope passes
// FW_MIN_SPAN_HZ as its minimum.
const FW_MIN_SPAN_HZ = 20000.0;
const FW_ZOOM_STEP = 1.4;
const FW_PAN_FRACTION = 0.20;

function fwHasData(w) {
  return w.dataUpperHz > w.dataLowerHz;
}

function fwReset(w) {
  w.viewLowerHz = w.dataLowerHz;
  w.viewUpperHz = w.dataUpperHz;
}

// Whether the reader is looking at less than the receiver is hearing -- what
// a Reset has something to undo, and what 0 backs out of.
function fwZoomed(w) {
  if (!w || !fwHasData(w)) return false;
  return w.viewUpperHz - w.viewLowerHz <
         (w.dataUpperHz - w.dataLowerHz) - 1.0;
}

// Keep the window inside the data and no narrower than minSpan.
function fwClamp(w, minSpan) {
  let span;

  if (!fwHasData(w)) { fwReset(w); return; }
  if (w.viewUpperHz <= w.viewLowerHz) fwReset(w);
  span = w.viewUpperHz - w.viewLowerHz;
  if (span < minSpan) span = minSpan;
  if (span > w.dataUpperHz - w.dataLowerHz)
    span = w.dataUpperHz - w.dataLowerHz;
  if (w.viewLowerHz < w.dataLowerHz) w.viewLowerHz = w.dataLowerHz;
  if (w.viewLowerHz + span > w.dataUpperHz)
    w.viewLowerHz = w.dataUpperHz - span;
  w.viewUpperHz = w.viewLowerHz + span;
}

// Zoom by `factor` (below 1 zooms in) about `anchorHz` when `hasAnchor`,
// otherwise about the middle -- so zooming in under the pointer keeps the
// feature there, and zooming from the keyboard keeps the centre.
function fwZoom(w, factor, anchorHz, hasAnchor, minSpan) {
  let span, anchor, fraction, next;

  if (w.viewUpperHz <= w.viewLowerHz) fwReset(w);
  span = w.viewUpperHz - w.viewLowerHz;
  if (span <= 0.0) return;
  anchor = w.viewLowerHz + span / 2.0;
  if (hasAnchor && anchorHz >= w.viewLowerHz && anchorHz <= w.viewUpperHz)
    anchor = anchorHz;
  fraction = (anchor - w.viewLowerHz) / span;
  next = span * factor;
  if (next < minSpan) next = minSpan;
  w.viewLowerHz = anchor - next * fraction;
  w.viewUpperHz = w.viewLowerHz + next;
  fwClamp(w, minSpan);
}

// Pan by a fraction of the span, and return how much of the movement the
// data could not absorb -- in hertz, so a caller with a tuner can turn a
// pan that ran off the edge into a retune (freq_window_pan_overflow). On a
// window already covering the whole span it reports the whole movement as
// leftover rather than looking like a dead key.
function fwPanOverflow(w, fraction, minSpan) {
  let span, wanted, moved, before;

  if (w.viewUpperHz <= w.viewLowerHz) fwReset(w);
  span = w.viewUpperHz - w.viewLowerHz;
  wanted = span * fraction;
  before = w.viewLowerHz;
  w.viewLowerHz += wanted;
  w.viewUpperHz += wanted;
  fwClamp(w, minSpan);
  moved = w.viewLowerHz - before;
  return wanted - moved;
}

// The frequency at a pixel, given where the plot sits and how wide it is --
// the one mapping a drag is measured by and the trace is drawn with.
function fwHzAt(w, plotX, plotWidth, x) {
  let span;

  if (!w || plotWidth <= 0.0) return 0.0;
  span = w.viewUpperHz - w.viewLowerHz;
  return w.viewLowerHz + span * ((x - plotX) / plotWidth);
}
