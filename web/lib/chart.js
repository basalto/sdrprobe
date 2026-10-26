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

// What a canvas has actually been laid out at, which is what its backing
// store should be: a canvas whose store is a different size from its box is
// a stretched picture, and one computed from the viewport instead is a
// second opinion about a layout CSS has already settled.
//
// Falls back to the store it already has where there is no layout to read
// -- a canvas that has never been displayed, or a document with no layout
// engine behind it at all.
function measure(canvas) {
  return {
    width: Math.max(1, Math.round(canvas.clientWidth || canvas.width)),
    height: Math.max(1, Math.round(canvas.clientHeight || canvas.height)),
  };
}

// Resizes a canvas's backing store to a given width and height, returning
// whether it actually changed. Setting either dimension clears the canvas,
// which matters to a caller whose canvas *is* its history -- the waterfalls
// here keep no rows of their own, exactly as the window's own
// `recreate_waterfall()` starts a fresh texture on a resize.
//
// Plain data and geometry: the numbers come from whoever knows the layout,
// which is never this file.
function fitCanvas(canvas, width, height) {
  if (canvas.width === width && canvas.height === height) return false;
  canvas.width = width;
  canvas.height = height;
  return true;
}

// The waterfall's per-bin colour: a dBFS value mapped across a floor..top
// range to an RGB string. The default range is the one this page has always
// used (-100 dBFS black-ish, -10 dBFS bright); the Scope's and the decode
// views' +/- keys move the floor so a faint band can be lifted out of the
// noise, which is INPUT_SCALE_WATERFALL in the window.
function colorForRange(dbfs, floor, top) {
  const t = Math.max(0, Math.min(1, (dbfs - floor) / (top - floor)));
  const r = Math.round(20 + 220 * t), g = Math.round(30 + 140 * t), b = Math.round(50 + 60 * (1 - t));
  return 'rgb(' + r + ',' + g + ',' + b + ')';
}

function colorFor(dbfs) {
  return colorForRange(dbfs, -100, -10);
}

// A chart's plot area inside its canvas: a left gutter for value labels and a
// bottom strip for the frequency axis, mirroring sdrgui_chart_area -- the
// clearance a chart reserves for itself because the label width depends on
// the values, which the caller does not know. Every frequency chart reserves
// the same gutter so a spectrum and the waterfall below it line up
// column-for-column, and a marker pill placed by markerPercent() lands on the
// same column the trace drew.
const CHART_GUTTER = 52;   // left, wide enough for "-120" or "-10 s"
const CHART_STRIP = 16;    // bottom, for the frequency labels
const AXIS_COLOR = '#8291a0';
const GRID_MAJOR = 'rgba(130,145,160,0.20)';   // a labelled tick's line
const GRID_MINOR = 'rgba(130,145,160,0.08)';   // a tick between labels

function chartArea(w, h) {
  const x = CHART_GUTTER;
  return {
    x,
    y: 0,
    width: Math.max(1, w - x),
    height: Math.max(1, h - CHART_STRIP),
  };
}

// The next 1/2/5-times-a-power-of-ten step at or below range/targetCount --
// sdrgui_nice_ceiling's idea, used the other way round to pick a tick
// spacing that gives about `targetCount` labels over `range`.
function niceStep(range, targetCount) {
  if (!(range > 0) || targetCount <= 0) return 0;
  const raw = range / targetCount;
  const mag = Math.pow(10, Math.floor(Math.log10(raw)));
  const norm = raw / mag;
  let step;
  if (norm < 1.5) step = 1;
  else if (norm < 3) step = 2;
  else if (norm < 7) step = 5;
  else step = 10;
  return step * mag;
}

// Every multiple of `step` inside [lo, hi], the tick positions themselves.
function axisTicks(lo, hi, step) {
  const out = [];
  if (!(step > 0)) return out;
  const first = Math.ceil(lo / step - 1e-6) * step;
  for (let v = first; v <= hi + step * 1e-6; v += step) out.push(v);
  return out;
}

// dBFS to a y inside a plot rect, with a caller-chosen top and bottom -- the
// Scope's vertical scale (the +/- keys) moves the bottom, so unlike dbfsToY
// above this cannot bake the range in.
function dbfsToYRange(v, plotY, plotH, top, bottom) {
  const clamped = Math.max(bottom, Math.min(top, v));
  return plotY + plotH * (top - clamped) / (top - bottom);
}

// Plots the slice of `values` covering [lowerFrac, upperFrac] of the array
// across a plot rect's width -- the windowed cousin of plot() above, for a
// view zoomed into part of its received span. One value per pixel column,
// the same nearest-neighbour rule, mapped through the caller's dBFS range.
function plotWindow(ctx, plot, values, color, lowerFrac, upperFrac, top, bottom) {
  const n = values.length;
  const fspan = upperFrac - lowerFrac;

  if (n === 0 || plot.width <= 0) return;
  ctx.strokeStyle = color;
  ctx.beginPath();
  for (let px = 0; px < plot.width; px++) {
    const frac = lowerFrac + fspan * (px / plot.width);
    let i = Math.floor(frac * n);
    if (i < 0) i = 0; else if (i >= n) i = n - 1;
    const x = plot.x + px;
    const y = dbfsToYRange(values[i], plot.y, plot.height, top, bottom);
    if (px === 0) ctx.moveTo(x, y); else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

// The dBFS scale down the left gutter: a labelled major tick with a gridline
// every nice step (about 20 dB over a 120 dB range), and a fainter unlabelled
// minor line halfway between each pair. The window's spectrum axis, which
// shows the floor and a few steps up from it rather than just the two ends.
function drawDbfsAxis(ctx, plot, top, bottom) {
  const range = top - bottom;
  const major = niceStep(range, 6) || range;
  const minor = major / 2;

  ctx.font = '12px monospace';
  // Minor lines first, so a major line drawn over the same y wins.
  ctx.strokeStyle = GRID_MINOR;
  ctx.beginPath();
  for (const v of axisTicks(bottom, top, minor)) {
    const y = Math.round(dbfsToYRange(v, plot.y, plot.height, top, bottom)) + 0.5;
    ctx.moveTo(plot.x, y);
    ctx.lineTo(plot.x + plot.width, y);
  }
  ctx.stroke();
  ctx.strokeStyle = GRID_MAJOR;
  ctx.beginPath();
  ctx.fillStyle = AXIS_COLOR;
  ctx.textAlign = 'right';
  for (const v of axisTicks(bottom, top, major)) {
    const yl = dbfsToYRange(v, plot.y, plot.height, top, bottom);
    const y = Math.round(yl) + 0.5;
    ctx.moveTo(plot.x, y);
    ctx.lineTo(plot.x + plot.width, y);
    let ty = yl + 4;
    if (ty < plot.y + 10) ty = plot.y + 10;
    if (ty > plot.y + plot.height) ty = plot.y + plot.height;
    ctx.fillText(v.toFixed(0), plot.x - 6, ty);
  }
  ctx.stroke();
  ctx.textAlign = 'left';
}

// How many decimals a frequency label needs, at a given unit scale, so that
// two adjacent major ticks `stepHz` apart do not print the same number.
function freqDecimals(stepHz, scale) {
  const d = 2 - Math.floor(Math.log10(stepHz / scale));
  return Math.max(0, Math.min(6, d));
}

// The frequency scale along the bottom strip: a labelled major tick with a
// vertical gridline every nice step across the window, a fainter minor line
// between, and labels carrying enough decimals for the zoom. The unit follows
// the span -- MHz for an RF chart, kHz for a baseband one (FM's multiplex and
// audio spectra), so a 0..100 kHz span does not read "0.000 MHz". Drawn below
// the plot so a scrolling waterfall never touches it, and nothing before a
// span is known.
// `overPlot` true (a spectrum, redrawn whole each frame) draws vertical
// gridlines across the plot; false (a waterfall, whose plot scrolls) draws
// short ticks in the bottom strip only, so nothing is painted over the
// scrolling image.
function drawFreqAxis(ctx, plot, canvasHeight, lowerHz, upperHz, overPlot) {
  if (!(upperHz > lowerHz)) return;
  if (overPlot === undefined) overPlot = true;
  const range = upperHz - lowerHz;
  const major = niceStep(range, 5) || range;
  const minor = major / 5;
  const kHz = range < 1e6;
  const scale = kHz ? 1e3 : 1e6;
  const unit = kHz ? ' kHz' : ' MHz';
  const dec = freqDecimals(major, scale);
  const y = canvasHeight - 4;
  const bottom = plot.y + plot.height;
  const xAt = (hz) => plot.x + plot.width * (hz - lowerHz) / range;
  const ticks = axisTicks(lowerHz, upperHz, major);

  ctx.font = '12px monospace';
  ctx.strokeStyle = overPlot ? GRID_MINOR : '#43525f';
  ctx.beginPath();
  for (const hz of axisTicks(lowerHz, upperHz, minor)) {
    const x = Math.round(xAt(hz)) + 0.5;
    ctx.moveTo(x, overPlot ? plot.y : bottom);
    ctx.lineTo(x, overPlot ? bottom : bottom + 3);
  }
  ctx.stroke();
  ctx.strokeStyle = overPlot ? GRID_MAJOR : AXIS_COLOR;
  ctx.beginPath();
  ctx.fillStyle = AXIS_COLOR;
  ctx.textAlign = 'center';
  ticks.forEach((hz, i) => {
    const xl = xAt(hz);
    const x = Math.round(xl) + 0.5;
    ctx.moveTo(x, overPlot ? plot.y : bottom);
    ctx.lineTo(x, overPlot ? bottom : bottom + 5);
    // The unit rides the last label only, so the strip is numbers and one unit.
    const label = (hz / scale).toFixed(dec) + (i === ticks.length - 1 ? unit : '');
    const pad = 4 * (dec + (i === ticks.length - 1 ? unit.length : 0));
    const tx = Math.max(plot.x + 2 + 4 * dec, Math.min(plot.x + plot.width - pad, xl));
    ctx.fillText(label, tx, y);
  });
  ctx.stroke();
  ctx.textAlign = 'left';
}

// The waterfall's time scale: newest at the top (0 s), older downward, a
// labelled major tick and a shorter minor one every nice number of seconds.
// `secPerRow` is how long one pixel-row stands for -- fixed by the tuning (one
// row is one sample block), so the scale does not wander.
//
// Ticks and labels live in the GUTTER only, never as lines across the plot: a
// waterfall scrolls its plot rect down one pixel per row, so a horizontal line
// drawn over it would be carried down every frame and smear into a trail. The
// gutter is redrawn clean each frame and is not scrolled, so a tick there
// stays put. (The frequency axis's vertical lines are safe over the plot --
// they are redrawn full height each frame and a vertical line scrolled down is
// still the same line.)
function drawTimeAxis(ctx, plot, secPerRow) {
  if (!(secPerRow > 0)) return;
  const totalSec = plot.height * secPerRow;
  const major = niceStep(totalSec, 5) || totalSec;
  const minor = major / 2;
  const yAt = (age) => plot.y + age / secPerRow;

  ctx.font = '12px monospace';
  // Minor ticks, short.
  ctx.strokeStyle = '#43525f';
  ctx.beginPath();
  for (let age = minor; age <= totalSec + 1e-6; age += minor) {
    const y = Math.round(yAt(age)) + 0.5;
    ctx.moveTo(plot.x - 3, y);
    ctx.lineTo(plot.x, y);
  }
  ctx.stroke();
  // Major ticks, longer, with a label.
  ctx.strokeStyle = AXIS_COLOR;
  ctx.fillStyle = AXIS_COLOR;
  ctx.textAlign = 'right';
  ctx.beginPath();
  for (let age = 0; age <= totalSec + major * 1e-6; age += major) {
    const yl = yAt(age);
    const y = Math.round(yl) + 0.5;
    ctx.moveTo(plot.x - 6, y);
    ctx.lineTo(plot.x, y);
    let ty = yl + 4;
    if (ty < plot.y + 10) ty = plot.y + 10;
    if (ty > plot.y + plot.height) continue;
    ctx.fillText(age === 0 ? '0 s' : '-' + (major >= 1 ? age.toFixed(0)
                 : age.toFixed(1)) + ' s', plot.x - 9, ty);
  }
  ctx.stroke();
  ctx.textAlign = 'left';
}
