// The Scope view: spectrum and waterfall, drawn onto the canvases
// viewer.html mounts as #spectrum and #waterfall. Plain drawing over
// lib/chart.js's primitives -- this file never opens a socket or names a
// message type; viewer.js decodes a message and calls in here.
const specCanvas = document.getElementById('spectrum');
const specCtx = specCanvas.getContext('2d');
const wfCanvas = document.getElementById('waterfall');
const wfCtx = wfCanvas.getContext('2d');

function drawSpectrum(average, peak) {
  const w = specCanvas.width, h = specCanvas.height;
  specCtx.fillStyle = '#0a0f16';
  specCtx.fillRect(0, 0, w, h);
  plot(specCtx, w, h, peak, '#e8a355', dbfsToY);
  plot(specCtx, w, h, average, '#5adcc8', dbfsToY);
}

// Scrolls the existing image down by one row and draws only the new one
// at the top, rather than redrawing all `h` rows from a kept row history
// every time one more arrives -- the canvas itself is the history, which
// is also why this page keeps none of its own. Measured against the
// whole-redraw version this replaced: it was 95%+ of this page's own JS
// busy time and growing as history filled (drawImage's one call against
// up to `w * h` -- 900 * 200 -- individual fillRect calls per row).
function drawWaterfall(row) {
  const w = wfCanvas.width, h = wfCanvas.height;
  wfCtx.drawImage(wfCanvas, 0, 0, w, h - 1, 0, 1, w, h - 1);
  for (let x = 0; x < w; x++) {
    const i = Math.floor(x * row.length / w);
    wfCtx.fillStyle = colorFor(row[i]);
    wfCtx.fillRect(x, 0, 1, 1);
  }
}
