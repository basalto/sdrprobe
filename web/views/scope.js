// The Scope view (ticket 14, Phase 3's registry shape): `markup` is the
// panel's HTML, `streams` the stream names it needs beyond the shell's
// own `receiver_state`/`link_health`, `render(msg)` draws from whatever
// wire.js decoded. Wrapped in an IIFE so `ScopeView` is the only name
// this file adds to the shared global scope every concatenated file
// runs in -- its own canvas refs and draw functions stay private, the
// same "exports three things and nothing else" ticket 14 asks of every
// view, enforced rather than merely intended.
const ScopeView = (function () {
  // The canvases are resolved lazily rather than at load time, because
  // `markup` is not in the document yet when this IIFE runs -- viewer.js
  // inserts it into `#panels` later, at `mountViews()` time. `elements()`
  // memoises the lookup so it costs one `getElementById()` per canvas,
  // not one per message.
  let els = null;
  function elements() {
    if (!els) {
      const specCanvas = document.getElementById('spectrum');
      const wfCanvas = document.getElementById('waterfall');
      els = {
        specCanvas, specCtx: specCanvas.getContext('2d'),
        wfCanvas, wfCtx: wfCanvas.getContext('2d'),
      };
    }
    return els;
  }

  function drawSpectrum(average, peak) {
    const { specCanvas, specCtx } = elements();
    const w = specCanvas.width, h = specCanvas.height;
    specCtx.fillStyle = '#0a0f16';
    specCtx.fillRect(0, 0, w, h);
    plot(specCtx, w, h, peak, '#e8a355', dbfsToY);
    plot(specCtx, w, h, average, '#5adcc8', dbfsToY);
  }

  // The rows this view has been sent, so the picture survives a resize --
  // see lib/waterfall.js for what that costs and why keeping them does not
  // put the old whole-redraw cost back on the per-row path.
  const wf = createWaterfall();

  function drawWaterfall(row) {
    const { wfCanvas, wfCtx } = elements();
    waterfallPush(wf, row);
    waterfallDrawNewest(wfCtx, wfCanvas, row);
  }

  return {
    id: 'scope',
    label: 'Scope',
    streams: ['spectrum', 'waterfall'],
    // Each canvas is matched to what CSS laid it out at; the markup gives
    // the two of them equal shares of the room left below the labels.
    // Resizing clears a canvas, so the waterfall is redrawn from the rows
    // it kept -- only when the geometry actually changed, which is what
    // `fitCanvas()` reports. The spectrum needs no such thing: the next
    // message carries the whole trace.
    resize() {
      const { specCanvas, wfCanvas, wfCtx } = elements();
      const spec = measure(specCanvas), wfBox = measure(wfCanvas);

      fitCanvas(specCanvas, spec.width, spec.height);
      if (fitCanvas(wfCanvas, wfBox.width, wfBox.height))
        waterfallRedraw(wfCtx, wfCanvas, wf);
    },
    // The two charts share the room left below their labels, so the page
    // is exactly the viewport and never scrolls. `min-height:0` is what
    // lets each be shorter than its own backing store rather than pushing
    // the column past the bottom.
    markup:
      '<div class="label">spectrum (average: cyan, peak hold: orange)</div>' +
      '<canvas id="spectrum" style="flex:1 1 0;min-height:110px;width:100%;'
      + 'margin-bottom:10px"></canvas>' +
      '<div class="label">waterfall (built from rows in this browser; never re-sent)</div>' +
      '<canvas id="waterfall" style="flex:1 1 0;min-height:110px;width:100%">'
      + '</canvas>',
    render(msg) {
      if (msg.kind === 'spectrum') drawSpectrum(msg.average, msg.peak);
      else if (msg.kind === 'waterfall_row') drawWaterfall(msg.row);
    },
  };
})();
