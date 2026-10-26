// A waterfall's rows, kept so the picture survives a resize.
//
// This page used to keep none, and the comment saying so was right about
// its own measurement and wrong about the conclusion. The measurement:
// redrawing every row from a kept history *on every arriving row* was 95%+
// of this page's JS busy time and grew as the history filled -- one
// `drawImage` against up to `w * h` individual `fillRect` calls. The
// conclusion drawn from it was "keep no history", which made the canvas the
// only copy of the picture, and a canvas loses its contents the moment its
// backing store is resized.
//
// Both are satisfied by separating the two paths, which is what this file
// is. A row arriving still costs one scroll and one row of pixels
// (`waterfallDrawNewest`); the whole history is redrawn only when the
// canvas geometry actually changed (`waterfallRedraw`), which is a resize
// and nothing else. The expensive path is not on the per-row path at all.
//
// Plain data and geometry, `lib/`'s own contract: a context, a rect, an
// array. Nothing here sees a socket, a message or a view.

// What a history costs, stated rather than left to be discovered. A row is
// capped at WATERFALL_MAX_BINS floats and the ring at WATERFALL_MAX_ROWS of
// them, so the worst case is fixed at 2048 * 800 * 4 bytes = 6.6 MB, and
// the ordinary case -- a 2048-point transform's 1024 bins -- is half that.
//
// 800 rows comfortably exceeds any canvas this page sizes itself to (a 4K
// display at the 40% of viewport height views/fm.js asks for is 864 pixels
// tall, and nothing is drawn below the bottom row anyway), and 2048 bins
// exceeds the pixel width of the canvas on all but the widest displays --
// past which the drawing below is sampling one bin per column regardless.
const WATERFALL_MAX_ROWS = 800;
const WATERFALL_MAX_BINS = 2048;

function createWaterfall() {
  return { rows: [] };
}

// Keeps a copy of `row`, decimated if it is wider than the cap.
//
// Decimated by the *same* nearest-neighbour rule the drawing below uses --
// deliberately, and not by taking each group's maximum, which would show
// peaks a live row does not. A redraw that looks better than the live
// picture is a redraw that disagrees with it, and the disagreement would
// appear exactly when a reader resizes to look more closely.
function waterfallPush(wf, row) {
  let kept;

  if (row.length <= WATERFALL_MAX_BINS) {
    kept = new Float32Array(row);
  } else {
    kept = new Float32Array(WATERFALL_MAX_BINS);
    for (let i = 0; i < WATERFALL_MAX_BINS; i++)
      kept[i] = row[Math.floor(i * row.length / WATERFALL_MAX_BINS)];
  }
  wf.rows.push(kept);
  if (wf.rows.length > WATERFALL_MAX_ROWS) wf.rows.shift();
}

// The windowed waterfall, drawing into a plot rect and over the slice
// [lowerFrac, upperFrac] of each row -- so a zoom and pan narrow the waterfall
// exactly as they narrow the spectrum above it (the native side does this
// through sdrgui_waterfall_span). Only the plot rect scrolls, so the gutter
// (the time axis) and the frequency strip beside and below it are left for the
// caller to draw once and keep. `floor`/`top` are the colour range (the +/-
// scale on a waterfall-only view); omitted, the default palette is used.
//
// The whole-canvas forms this replaced (waterfallDrawRow / _DrawNewest /
// _Redraw) are gone with the last caller: every waterfall reserves a gutter
// now, so there is no drawing that starts at x=0.

// One row of pixels at absolute `y`, over the visible bin slice, one bin per
// column across the plot's width. `floor`/`top` are the colour range (the
// +/- scale); omitted, the default palette is used.
//
// Built as a one-pixel-tall ImageData and blitted with a single putImageData,
// not a fillRect per column -- the per-column form was ~1800 fillStyle+fillRect
// calls a row and was most of this page's JS time, which at this data rate was
// draining the socket slowly enough that the server dropped frames. The colour
// ramp is colorForRange's, inlined as bytes (the same 20+220t / 30+140t /
// 50+60(1-t) the string form builds).
function waterfallDrawRowWindow(ctx, plot, y, row, lowerFrac, upperFrac, floor, top) {
  const w = plot.width | 0;
  if (w <= 0) return;
  const n = row.length;
  const fspan = upperFrac - lowerFrac;
  const lo = floor === undefined ? -100 : floor;
  const tp = top === undefined ? -10 : top;
  const range = (tp - lo) || 1;
  const img = ctx.createImageData(w, 1);
  const d = img.data;

  for (let px = 0; px < w; px++) {
    const frac = lowerFrac + fspan * (px / w);
    let i = (frac * n) | 0;
    if (i < 0) i = 0; else if (i >= n) i = n - 1;
    let t = (row[i] - lo) / range;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    const o = px << 2;
    d[o] = 20 + 220 * t;
    d[o + 1] = 30 + 140 * t;
    d[o + 2] = 50 + 60 * (1 - t);
    d[o + 3] = 255;
  }
  ctx.putImageData(img, plot.x | 0, y | 0);
}

// The per-row path: scroll the plot rect down by one and draw the new row at
// its top. The gutter and strip outside the rect are untouched.
function waterfallDrawNewestWindow(ctx, canvas, plot, row, lowerFrac, upperFrac, floor, top) {
  if (plot.height > 1)
    ctx.drawImage(canvas, plot.x, plot.y, plot.width, plot.height - 1,
                  plot.x, plot.y + 1, plot.width, plot.height - 1);
  waterfallDrawRowWindow(ctx, plot, plot.y, row, lowerFrac, upperFrac, floor, top);
}

// The whole history at the current window, after a resize or a zoom/pan that
// changed which bins are on screen -- every kept row has to be redrawn,
// because the one already on the canvas was drawn for a different window.
function waterfallRedrawWindow(ctx, canvas, wf, plot, lowerFrac, upperFrac, floor, top) {
  const n = Math.min(plot.height, wf.rows.length);

  ctx.fillStyle = '#0a0f16';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  for (let k = 0; k < n; k++)
    waterfallDrawRowWindow(ctx, plot, plot.y + k,
                           wf.rows[wf.rows.length - 1 - k],
                           lowerFrac, upperFrac, floor, top);
}
