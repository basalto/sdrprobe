// Axes, gridlines (none drawn yet, but this is where they would live),
// dBFS-to-y, plot geometry and colour -- plain data and geometry, taking
// a canvas context, an array, a rect and a style, and never a message, a
// socket or a view. This file's contract is `sdrgui.h`'s own, restated
// for JavaScript: "Each takes a plain param struct (buffers + geometry +
// style), never `struct app`" with `struct app` replaced by "the
// application".

// SPECTRUM_TOP_DBFS and the floor this page assumes, shared by the
// Scope's spectrum and the Survey's chart -- both read a dBFS array and
// neither should carry its own copy of where zero and the floor are.
function dbfsToY(v, height) {
  const top = 0, bottom = -120;
  const clamped = Math.max(bottom, Math.min(top, v));
  return height * (top - clamped) / (top - bottom);
}

// Plots `values` as one line across a canvas context's own width/height,
// mapping each sample to y with `toY` and picking one value per pixel
// column (nearest-neighbour downsampling). The one mapping both
// views/scope.js's spectrum and views/survey.js's chart use, where before
// this file existed each view carried its own copy of the same loop.
function plot(ctx, width, height, values, color, toY) {
  ctx.strokeStyle = color;
  ctx.beginPath();
  for (let x = 0; x < width; x++) {
    const i = Math.floor(x * values.length / width);
    const y = toY(values[i], height);
    if (x === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

// The waterfall's per-bin colour: dBFS in, an RGB string out.
function colorFor(dbfs) {
  const t = Math.max(0, Math.min(1, (dbfs + 100) / 90));
  const r = Math.round(20 + 220 * t), g = Math.round(30 + 140 * t), b = Math.round(50 + 60 * (1 - t));
  return 'rgb(' + r + ',' + g + ',' + b + ')';
}
