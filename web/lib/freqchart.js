// A frequency chart's window, vertical scale, waterfall and input, in one
// place -- so the Scope and every decode view get the same zoom, pan, scale,
// axes and gestures from a few lines each rather than six copies that drift.
//
// It owns a spectrum (optional) and a waterfall over one shared frequency
// window: they are two drawings of the same span, and a reader who zoomed one
// and not the other would have two answers to one question (the window keeps
// a single chart_window for both, app.h's `struct scope_view`). The arithmetic
// is lib/freqwindow.js, the ported freq_window.h; the axes and the colour map
// are lib/chart.js and lib/waterfall.js. This file is `lib/`'s contract
// (ADR-0007): plain data, geometry and canvases, never a message or a view --
// a view hands it canvases and two hooks (is my panel the live one, and
// reposition my markers) and otherwise only feeds it traces, rows and the
// tuning.
//
// A pan that runs past the received span becomes a `tune` when `retune` is on
// (the Scope and the decode views, which the server lets move or refuses); a
// chart whose data is not the receiver's own span -- a swept survey, an
// audio-baseband spectrum -- passes `retune:false` and the pan simply stops
// at the edge.
function createFreqChart(opts) {
  opts = opts || {};
  const minSpan = opts.minSpanHz || FW_MIN_SPAN_HZ;
  const retune = opts.retune !== false;
  const hasSpectrum = !!opts.hasSpectrum;
  // A view with more than one frequency chart (FM's two baseband spectra) can
  // only let the keys act on the one under the pointer; a view with a single
  // chart lets them act whenever its panel is up.
  const keysNeedHover = !!opts.keysNeedHover;
  let hovered = false;

  const fw = { dataLowerHz: 0, dataUpperHz: 0, viewLowerHz: 0, viewUpperHz: 0 };
  let anchorHz = 0, anchorRate = 0;
  let lastCentreHz = null;          // the receiver centre a pan retunes from

  // The spectrum's dBFS axis (top fixed at full scale, floor moved by +/-),
  // and the waterfall's colour floor (the same keys where there is no
  // spectrum -- the window's INPUT_SCALE_WATERFALL).
  let specTop = 0, specBottom = -120;
  let wfFloor = -100, wfTop = -10;
  const SCALE_STEP_DB = 6, FLOOR_MIN = -170, FLOOR_MAX = -20;

  const wf = createWaterfall();
  let lastAverage = null, lastPeak = null;

  let ref = null;                   // attached canvases + contexts
  let isActive = () => false;
  let onRedraw = () => {};

  function fracs() {
    const span = fw.dataUpperHz - fw.dataLowerHz;
    if (span <= 0) return [0, 1];
    let lo = (fw.viewLowerHz - fw.dataLowerHz) / span;
    let hi = (fw.viewUpperHz - fw.dataLowerHz) / span;
    lo = Math.max(0, Math.min(1, lo));
    hi = Math.max(0, Math.min(1, hi));
    if (hi <= lo) return [0, 1];
    return [lo, hi];
  }

  // How long one pixel-row of waterfall stands for: one row is one sample block
  // (SAMPLE_BLOCK_PAIRS pairs) at the current rate, so the scale is fixed by the
  // tuning rather than measured from arrival times -- which jittered with block
  // cadence and drops and made the axis wander. The same block duration the
  // decode views' age-dots use (ADSB_BLOCK_PAIRS / sample_rate).
  const WATERFALL_BLOCK_PAIRS = 131072;
  function secPerRow() {
    return anchorRate > 0 ? WATERFALL_BLOCK_PAIRS / anchorRate : 0;
  }

  function drawSpectrum() {
    if (!hasSpectrum || !ref.specCanvas) return;
    const c = ref.specCanvas, x = ref.specCtx;
    const plot = chartArea(c.width, c.height);
    const [lo, hi] = fracs();

    x.fillStyle = '#0a0f16';
    x.fillRect(0, 0, c.width, c.height);
    drawDbfsAxis(x, plot, specTop, specBottom);
    if (lastPeak)
      plotWindow(x, plot, lastPeak, '#e8a355', lo, hi, specTop, specBottom);
    if (lastAverage)
      plotWindow(x, plot, lastAverage, '#5adcc8', lo, hi, specTop, specBottom);
    drawFreqAxis(x, plot, c.height, fw.viewLowerHz, fw.viewUpperHz);
  }

  function waterfallAxes(plot) {
    const c = ref.wfCanvas, x = ref.wfCtx;
    // The gutter and the strip carry only axis labels, and the per-row path
    // scrolls just the plot rect -- so wipe the margins each time, or a
    // changing time scale prints over its old labels and the first rows'
    // low-frequency columns linger beside a later, narrower window.
    x.fillStyle = '#0a0f16';
    x.fillRect(0, 0, plot.x, c.height);
    x.fillRect(0, plot.y + plot.height, c.width,
               c.height - (plot.y + plot.height));
    drawTimeAxis(x, plot, secPerRow());
    // Strip ticks, not lines across the plot: the waterfall scrolls.
    drawFreqAxis(x, plot, c.height, fw.viewLowerHz, fw.viewUpperHz, false);
  }

  function redrawWaterfall() {
    if (!ref.wfCanvas) return;
    const c = ref.wfCanvas, x = ref.wfCtx;
    const plot = chartArea(c.width, c.height);
    const [lo, hi] = fracs();

    waterfallRedrawWindow(x, c, wf, plot, lo, hi, wfFloor, wfTop);
    waterfallAxes(plot);
  }

  function redrawAll() {
    drawSpectrum();
    redrawWaterfall();
    onRedraw();
  }

  function syncTuning(centerHz, rateHz) {
    const half = rateHz / 2;
    const lower = centerHz - half, upper = centerHz + half;

    if (anchorHz !== centerHz || anchorRate !== rateHz) {
      const span = fw.viewUpperHz - fw.viewLowerHz;
      const middle = (fw.viewUpperHz + fw.viewLowerHz) / 2;
      const shift = centerHz - anchorHz;

      fw.dataLowerHz = lower;
      fw.dataUpperHz = upper;
      if (anchorRate === 0 || span <= 0 || span >= anchorRate) {
        fwReset(fw);
      } else {
        const m = middle + shift;
        fw.viewLowerHz = m - span / 2;
        fw.viewUpperHz = m + span / 2;
        fwClamp(fw, minSpan);
      }
      // The kept rows belong to the frequency the receiver has left.
      wf.rows.length = 0;
      anchorHz = centerHz;
      anchorRate = rateHz;
      return true;
    }
    fw.dataLowerHz = lower;
    fw.dataUpperHz = upper;
    if (fw.viewUpperHz <= fw.viewLowerHz) { fwReset(fw); return true; }
    return false;
  }

  function pan(fraction) {
    const want = fwPanOverflow(fw, fraction, minSpan);
    if (retune && want !== 0 && lastCentreHz) {
      const target = Math.round(lastCentreHz + want);
      if (target > 0 && typeof sendCommand === 'function')
        sendCommand('tune ' + target);
    }
    redrawAll();
  }

  // +/- move the scale of whatever this chart draws: the spectrum's dBFS floor
  // and the waterfall's colour floor together, so on the Scope one key lifts
  // the noise out of both at once, and on a waterfall-only view it still acts.
  function scale(up) {
    const by = up ? SCALE_STEP_DB : -SCALE_STEP_DB;
    specBottom = Math.max(FLOOR_MIN, Math.min(FLOOR_MAX, specBottom + by));
    wfFloor = Math.max(FLOOR_MIN, Math.min(wfTop - SCALE_STEP_DB, wfFloor + by));
    redrawAll();
  }

  function pointerHz(canvas, clientX) {
    const rect = canvas.getBoundingClientRect();
    const plot = chartArea(canvas.width, canvas.height);
    const scaleX = rect.width > 0 ? canvas.width / rect.width : 1;
    return fwHzAt(fw, plot.x, plot.width, (clientX - rect.left) * scaleX);
  }

  let dragCanvas = null, dragLastX = 0, dragging = false;

  function bindMouse(canvas) {
    if (!canvas) return;
    canvas.addEventListener('wheel', (ev) => {
      if (!isActive()) return;
      ev.preventDefault();
      fwZoom(fw, ev.deltaY < 0 ? 1 / FW_ZOOM_STEP : FW_ZOOM_STEP,
             pointerHz(canvas, ev.clientX), true, minSpan);
      redrawAll();
    }, { passive: false });
    canvas.addEventListener('mousedown', (ev) => {
      if (!isActive()) return;
      ev.preventDefault();
      dragCanvas = canvas; dragLastX = ev.clientX; dragging = true;
    });
    canvas.addEventListener('mouseenter', () => { hovered = true; });
    canvas.addEventListener('mouseleave', () => { hovered = false; });
  }

  function onDragMove(ev) {
    if (!dragging || !dragCanvas) return;
    const rect = dragCanvas.getBoundingClientRect();
    const scaleX = rect.width > 0 ? dragCanvas.width / rect.width : 1;
    const plot = chartArea(dragCanvas.width, dragCanvas.height);
    const dxPix = (ev.clientX - dragLastX) * scaleX;
    const span = fw.viewUpperHz - fw.viewLowerHz;

    dragLastX = ev.clientX;
    if (span <= 0 || plot.width <= 0) return;
    // Grab and drag: the frequency under the pointer stays under it, so moving
    // right shows lower frequencies -- the view shifts left.
    pan(-dxPix / plot.width);
  }

  function onKey(ev) {
    if (!isActive()) return;
    if (keysNeedHover && !hovered) return;
    switch (ev.key) {
      case 'ArrowUp':
        fwZoom(fw, 1 / FW_ZOOM_STEP, 0, false, minSpan); redrawAll(); break;
      case 'ArrowDown':
        fwZoom(fw, FW_ZOOM_STEP, 0, false, minSpan); redrawAll(); break;
      case 'ArrowLeft': pan(-FW_PAN_FRACTION); break;
      case 'ArrowRight': pan(FW_PAN_FRACTION); break;
      case '+': case '=': scale(true); break;
      case '-': case '_': scale(false); break;
      case '0': fwReset(fw); redrawAll(); break;
      default: return;
    }
    ev.preventDefault();
  }

  return {
    fw,
    // --- the view attaches its canvases and hooks once, at mount ---
    mount(cfg) {
      ref = {
        specCanvas: cfg.specCanvas || null,
        specCtx: cfg.specCanvas ? cfg.specCanvas.getContext('2d') : null,
        wfCanvas: cfg.wfCanvas || null,
        wfCtx: cfg.wfCanvas ? cfg.wfCanvas.getContext('2d') : null,
      };
      if (cfg.isActive) isActive = cfg.isActive;
      if (cfg.onRedraw) onRedraw = cfg.onRedraw;
      bindMouse(ref.specCanvas);
      bindMouse(ref.wfCanvas);
      window.addEventListener('mousemove', onDragMove);
      window.addEventListener('mouseup', () => { dragging = false; dragCanvas = null; });
      window.addEventListener('keydown', onKey);
    },
    // --- the view feeds it data ---
    // Two traces (the Scope's average + peak) or one (a baseband or swept
    // spectrum, peak omitted). onRedraw runs after, so a view's own marks or
    // landmarks are drawn over the fresh trace.
    renderSpectrum(average, peak) {
      lastAverage = average; lastPeak = peak || null; drawSpectrum(); onRedraw();
    },
    // A spectrum whose span is the data itself (a swept range, a baseband
    // multiplex) rather than the receiver's tuning -- set the data edges
    // directly and let the view window follow, with no retune behind a pan.
    onDataSpan(lowerHz, upperHz) {
      if (!(upperHz > lowerHz)) return false;
      const changed = syncTuning((lowerHz + upperHz) / 2, upperHz - lowerHz);
      if (changed) redrawAll();
      return changed;
    },
    renderRow(row) {
      const c = ref.wfCanvas, x = ref.wfCtx;
      const plot = chartArea(c.width, c.height);
      const [lo, hi] = fracs();

      waterfallPush(wf, row);
      waterfallDrawNewestWindow(x, c, plot, row, lo, hi, wfFloor, wfTop);
      // The axes are not redrawn per row: the scroll touches only the plot
      // rect, so the gutter (time) and strip (frequency) the last redraw drew
      // stay put. They are refreshed by redrawWaterfall on a resize or a
      // window change, which is the only time they move.
      onRedraw();
    },
    onTuning(centerHz, rateHz) {
      // A state with no rate yet is not a retune; ignoring it keeps a
      // transient 0 from clearing the waterfall between subscriptions.
      if (!(rateHz > 0)) return false;
      const changed = syncTuning(centerHz, rateHz);
      lastCentreHz = centerHz;
      if (changed) redrawAll();
      return changed;
    },
    resize() { redrawAll(); },
    redraw() { redrawAll(); },
    // The same gestures as the keys, for a view's on-screen buttons.
    scaleUp() { scale(true); },
    scaleDown() { scale(false); },
    resetZoom() { fwReset(fw); redrawAll(); },

    // --- what a view's markers need: where a frequency lands, as a percent of
    // the waterfall canvas's width (gutter included), or null when it is off
    // the current window; and the plot's share of the canvas height, so an
    // age-positioned mark stays above the frequency strip.
    markerPercent(hz) {
      const c = ref.wfCanvas;
      if (!c) return null;
      const plot = chartArea(c.width, c.height);
      const span = fw.viewUpperHz - fw.viewLowerHz;
      if (span <= 0 || hz < fw.viewLowerHz || hz > fw.viewUpperHz) return null;
      return 100 * (plot.x + plot.width * (hz - fw.viewLowerHz) / span) / c.width;
    },
    plotHeightFraction() {
      const c = ref.wfCanvas;
      if (!c) return 1;
      return chartArea(c.width, c.height).height / c.height;
    },
    viewLowerHz() { return fw.viewLowerHz; },
    viewUpperHz() { return fw.viewUpperHz; },

    // For a spectrum chart that draws its own marks or landmarks into the
    // canvas (the survey's candidates, FM's pilot/stereo/RDS lines): the plot
    // rect and the dBFS range, so the view maps a frequency and a level to the
    // same pixels the trace used. `specX(hz)` is the pixel x, or null when the
    // frequency is off the current window.
    specPlot() {
      const c = ref.specCanvas;
      return c ? chartArea(c.width, c.height) : null;
    },
    specX(hz) {
      const c = ref.specCanvas;
      if (!c) return null;
      const plot = chartArea(c.width, c.height);
      const span = fw.viewUpperHz - fw.viewLowerHz;
      if (span <= 0 || hz < fw.viewLowerHz || hz > fw.viewUpperHz) return null;
      return plot.x + plot.width * (hz - fw.viewLowerHz) / span;
    },
    specY(dbfs) {
      const c = ref.specCanvas;
      if (!c) return 0;
      const plot = chartArea(c.width, c.height);
      return dbfsToYRange(dbfs, plot.y, plot.height, specTop, specBottom);
    },
  };
}
