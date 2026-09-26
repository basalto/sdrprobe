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
    tab: 1, // TAB_SCOPE, input_route.h's enum active_tab -- matched here
            // rather than reinvented, since receiver_state.tab is that
            // enum's own int.
    streams: ['spectrum', 'waterfall'],
    // Both canvases take the width the shell measured; the heights keep
    // the 260/200 proportion they were authored at. Resizing clears a
    // canvas, so the waterfall is redrawn from the rows it kept -- only
    // when the geometry actually changed, which is what `fitCanvas()`
    // reports. The spectrum needs no such thing: the next message carries
    // the whole trace.
    resize(width, viewportHeight) {
      const { specCanvas, wfCanvas, wfCtx } = elements();

      fitCanvas(specCanvas, width, Math.max(160, Math.round(viewportHeight * 0.30)));
      if (fitCanvas(wfCanvas, width, Math.max(130, Math.round(viewportHeight * 0.23))))
        waterfallRedraw(wfCtx, wfCanvas, wf);
    },
    markup:
      '<div class="label">spectrum (average: cyan, peak hold: orange)</div>' +
      '<canvas id="spectrum" width="900" height="260"></canvas>' +
      '<div class="label">waterfall (built from rows in this browser; never re-sent)</div>' +
      '<canvas id="waterfall" width="900" height="200"></canvas>',
    render(msg) {
      if (msg.kind === 'spectrum') drawSpectrum(msg.average, msg.peak);
      else if (msg.kind === 'waterfall_row') drawWaterfall(msg.row);
    },
  };
})();
