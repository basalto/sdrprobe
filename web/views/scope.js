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

  // Scrolls the existing image down by one row and draws only the new
  // one at the top, rather than redrawing all `h` rows from a kept row
  // history every time one more arrives -- the canvas itself is the
  // history, which is also why this page keeps none of its own.
  // Measured against the whole-redraw version this replaced: it was 95%+
  // of this page's own JS busy time and growing as history filled
  // (drawImage's one call against up to `w * h` -- 900 * 200 --
  // individual fillRect calls per row).
  function drawWaterfall(row) {
    const { wfCanvas, wfCtx } = elements();
    const w = wfCanvas.width, h = wfCanvas.height;
    wfCtx.drawImage(wfCanvas, 0, 0, w, h - 1, 0, 1, w, h - 1);
    for (let x = 0; x < w; x++) {
      const i = Math.floor(x * row.length / w);
      wfCtx.fillStyle = colorFor(row[i]);
      wfCtx.fillRect(x, 0, 1, 1);
    }
  }

  return {
    id: 'scope',
    label: 'Scope',
    tab: 1, // TAB_SCOPE, input_route.h's enum active_tab -- matched here
            // rather than reinvented, since receiver_state.tab is that
            // enum's own int.
    streams: ['spectrum', 'waterfall'],
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
