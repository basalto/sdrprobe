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

// One row of pixels at `y`, one bin per column. The one mapping both the
// live path and the redraw use, so the two cannot come to disagree.
function waterfallDrawRow(ctx, width, y, row) {
  for (let x = 0; x < width; x++) {
    const i = Math.floor(x * row.length / width);
    ctx.fillStyle = colorFor(row[i]);
    ctx.fillRect(x, y, 1, 1);
  }
}

// The per-row path, unchanged in cost: scroll what is already drawn down by
// one and put the new row at the top. The canvas is still carrying the
// picture; the history beside it is only what a resize redraws from.
function waterfallDrawNewest(ctx, canvas, row) {
  const w = canvas.width, h = canvas.height;

  ctx.drawImage(canvas, 0, 0, w, h - 1, 0, 1, w, h - 1);
  waterfallDrawRow(ctx, w, 0, row);
}

// Every row the history holds, newest at the top, into a canvas whose size
// has just changed. Rows older than the canvas is tall are not drawn -- and
// are still kept, so making the window taller again brings them back.
function waterfallRedraw(ctx, canvas, wf) {
  const w = canvas.width, h = canvas.height;
  const n = Math.min(h, wf.rows.length);

  ctx.fillStyle = '#0a0f16';
  ctx.fillRect(0, 0, w, h);
  for (let y = 0; y < n; y++)
    waterfallDrawRow(ctx, w, y, wf.rows[wf.rows.length - 1 - y]);
}
