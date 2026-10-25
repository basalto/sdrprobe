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
        centre: document.getElementById('scope-centre'),
        tune: document.getElementById('scope-tune'),
      };
    }
    return els;
  }

  // The window's Scope header colours, as the hex of their exact RGB, matching
  // the survey view's controls so the two tabs read alike.
  const PANEL_EDGE = '#526d7e';
  const CAPTION = '#ebf2f6';
  const LABEL = '#a6bcc9';
  const VALUE = '#ebf2f6';

  function send(line) {
    // viewer.js owns the socket; a view never opens one (ADR-0007, restated for
    // JavaScript in the web-view skill).
    if (typeof sendCommand === 'function') sendCommand(line);
  }

  // The centre field is spelled in MHz, the way the window's Scope header field
  // is -- "948.4" or "948.4M", both 948.4 MHz. `tune` wants a positive integer
  // in Hz (the server refuses anything else), so the conversion is this file's:
  // parse the MHz, round to the nearest hertz, and send that.
  function parseMHz(text) {
    let t = text.trim();
    if (!t) return null;
    if (/[Mm]$/.test(t)) t = t.slice(0, -1);
    const mhz = parseFloat(t);
    if (!Number.isFinite(mhz) || mhz <= 0) return null;
    return Math.round(mhz * 1e6);
  }

  function tuneToField() {
    const hz = parseMHz(elements().centre.value);
    // A bad spelling is left for the operator to fix rather than sent: the
    // server would refuse it, but refusing here keeps the field as typed.
    if (hz !== null) send('tune ' + hz);
  }

  // The last centre the receiver reported, so the field can be kept in step
  // without clobbering a half-typed entry (prefilled only while unfocused).
  let lastCentreHz = null;

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
    // The window's Scope header has a centre field that retunes the receiver;
    // this is that field and nothing more. The window's start/end are a view
    // zoom this page does not have (the spectrum is redrawn whole each block),
    // and its FFT stepper is the Settings panel's resolution, reachable there.
    mounted() {
      const e = elements();
      e.tune.addEventListener('click', tuneToField);
      e.centre.addEventListener('keydown', (ev) => {
        if (ev.key === 'Enter') tuneToField();
      });
    },
    markup:
      '<div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap;'
      + 'flex:0 0 auto;margin-bottom:6px">'
      + '<label style="color:' + LABEL + '">centre <input id="scope-centre" '
      + 'size="10" style="background:#16222e;color:' + VALUE
      + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;padding:3px 6px">'
      + ' MHz</label>'
      + '<button id="scope-tune" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 12px;font:inherit;'
      + 'cursor:pointer">Tune</button>'
      + '</div>' +
      '<div class="label">spectrum (average: cyan, peak hold: orange)</div>' +
      '<canvas id="spectrum" style="flex:1 1 0;min-height:110px;width:100%;'
      + 'margin-bottom:10px"></canvas>' +
      '<div class="label">waterfall (built from rows in this browser; never re-sent)</div>' +
      '<canvas id="waterfall" style="flex:1 1 0;min-height:110px;width:100%">'
      + '</canvas>',
    render(msg) {
      if (msg.kind === 'spectrum') drawSpectrum(msg.average, msg.peak);
      else if (msg.kind === 'waterfall_row') drawWaterfall(msg.row);
      else if (msg.kind === 'state' && msg.state.type === 'receiver_state') {
        // Keep the centre field showing where the receiver is, the way the
        // window's field reads back the tuning -- but not while it is being
        // typed into, and only when the value actually moved.
        const e = elements();
        const hz = msg.state.center_hz;
        if (hz !== lastCentreHz && document.activeElement !== e.centre) {
          e.centre.value = (hz / 1e6).toFixed(4);
          lastCentreHz = hz;
        }
      }
    },
  };
})();
