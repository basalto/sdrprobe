// The Scope view (ticket 14, Phase 3's registry shape): `markup` is the
// panel's HTML, `streams` the stream names it needs beyond the shell's
// own `receiver_state`/`link_health`, `render(msg)` draws from whatever
// wire.js decoded. Wrapped in an IIFE so `ScopeView` is the only name
// this file adds to the shared global scope every concatenated file
// runs in.
//
// The spectrum, the waterfall, their axes and all the zoom/pan/scale
// gestures are lib/freqchart.js now -- the same controller every decode
// view's waterfall uses, so the gestures mean the same everywhere. This
// file is the Scope's own chrome: the centre field, the FFT stepper, and
// the vertical-scale and reset buttons.
const ScopeView = (function () {
  // The canvases are resolved lazily rather than at load time, because
  // `markup` is not in the document yet when this IIFE runs -- viewer.js
  // inserts it into `#panels` later, at `mountViews()` time.
  let els = null;
  function elements() {
    if (!els) {
      els = {
        specCanvas: document.getElementById('spectrum'),
        wfCanvas: document.getElementById('waterfall'),
        centre: document.getElementById('scope-centre'),
        tune: document.getElementById('scope-tune'),
        fftDown: document.getElementById('scope-fft-down'),
        fftValue: document.getElementById('scope-fft'),
        fftUp: document.getElementById('scope-fft-up'),
        scaleDown: document.getElementById('scope-scale-down'),
        scaleUp: document.getElementById('scope-scale-up'),
        reset: document.getElementById('scope-reset'),
      };
    }
    return els;
  }

  // The transform sizes the Scope header steps through -- sdr_dsp_fft_choice()'s
  // own, the same axis settings.js lists. Listed only as the stepper's steps;
  // the server refuses a size it does not offer and says so.
  const FFT_SIZES = [256, 512, 1024, 2048, 4096, 8192, 16384];

  function stepFromList(list, current, by) {
    const i = list.indexOf(current);
    const next = i < 0 ? 0 : Math.min(list.length - 1, Math.max(0, i + by));
    return list[next];
  }

  // The window's Scope header colours, as the hex of their exact RGB, matching
  // the survey view's controls so the two tabs read alike.
  const PANEL_EDGE = '#526d7e';
  const CAPTION = '#ebf2f6';
  const LABEL = '#a6bcc9';
  const VALUE = '#ebf2f6';

  function send(line) {
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
    // A bad spelling is left for the operator to fix rather than sent.
    if (hz !== null) send('tune ' + hz);
  }

  // The last centre the receiver reported, so the field can be kept in step
  // without clobbering a half-typed entry (prefilled only while unfocused).
  let lastCentreHz = null;
  // The transform size the receiver reports in use, so the stepper steps from
  // the server's truth rather than guessing.
  let lastFftSize = null;

  function stepFft(by) {
    const from = lastFftSize || 2048;
    const next = stepFromList(FFT_SIZES, from, by);
    // `fft` is the this-run-only size (view_scope.c's stepper), not `set fft` +
    // `apply`, which persists. The server refuses a size it does not offer.
    if (next !== from) send('fft ' + next);
  }

  // The spectrum, the waterfall and every gesture. retune on: a pan past the
  // received edge retunes the receiver, which on the Scope is always allowed.
  const chart = createFreqChart({ hasSpectrum: true, retune: true });

  // The keys act only on the Scope, and only when no field is taking typed
  // input -- input_view_keys_live / input_scale_keys, in the browser.
  function active() {
    const p = document.getElementById('panel-scope');
    if (!p || p.hidden) return false;
    const a = document.activeElement;
    return !(a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' ||
                   a.isContentEditable));
  }

  return {
    id: 'scope',
    label: 'Scope',
    streams: ['spectrum', 'waterfall'],
    // Each canvas is matched to what CSS laid it out at, then everything is
    // redrawn -- a fit clears the canvas, so the traces and both axes have to
    // be drawn again whether or not the geometry changed.
    resize() {
      const { specCanvas, wfCanvas } = elements();
      const spec = measure(specCanvas), wfBox = measure(wfCanvas);

      fitCanvas(specCanvas, spec.width, spec.height);
      fitCanvas(wfCanvas, wfBox.width, wfBox.height);
      chart.resize();
    },
    mounted() {
      const e = elements();
      e.tune.addEventListener('click', tuneToField);
      e.centre.addEventListener('keydown', (ev) => {
        if (ev.key === 'Enter') tuneToField();
      });
      e.fftDown.addEventListener('click', () => stepFft(-1));
      e.fftUp.addEventListener('click', () => stepFft(1));
      e.scaleDown.addEventListener('click', () => chart.scaleDown());
      e.scaleUp.addEventListener('click', () => chart.scaleUp());
      e.reset.addEventListener('click', () => chart.resetZoom());
      chart.mount({ specCanvas: e.specCanvas, wfCanvas: e.wfCanvas, isActive: active });
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
      + '<span style="color:' + LABEL + ';margin-left:8px">FFT</span>'
      + '<button id="scope-fft-down" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 10px;font:inherit;'
      + 'cursor:pointer">‹</button>'
      + '<span id="scope-fft" style="color:' + VALUE + ';min-width:48px;'
      + 'display:inline-block;text-align:center">—</span>'
      + '<button id="scope-fft-up" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 10px;font:inherit;'
      + 'cursor:pointer">›</button>'
      + '<span style="color:' + LABEL + '">points</span>'
      + '<span style="color:' + LABEL + ';margin-left:8px">scale</span>'
      + '<button id="scope-scale-down" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 10px;font:inherit;'
      + 'cursor:pointer">−</button>'
      + '<button id="scope-scale-up" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 10px;font:inherit;'
      + 'cursor:pointer">+</button>'
      + '<button id="scope-reset" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 12px;font:inherit;'
      + 'cursor:pointer;margin-left:8px">Reset zoom</button>'
      + '</div>' +
      '<div class="label">spectrum (average: cyan, peak hold: orange) — scroll '
      + 'to zoom, drag to pan; arrows zoom/pan, +/− scale, 0 reset</div>' +
      '<canvas id="spectrum" style="flex:1 1 0;min-height:110px;width:100%;'
      + 'margin-bottom:10px"></canvas>' +
      '<div class="label">waterfall (built from rows in this browser; never re-sent)</div>' +
      '<canvas id="waterfall" style="flex:1 1 0;min-height:110px;width:100%">'
      + '</canvas>',
    render(msg) {
      if (msg.kind === 'spectrum') chart.renderSpectrum(msg.average, msg.peak);
      else if (msg.kind === 'waterfall_row') chart.renderRow(msg.row);
      else if (msg.kind === 'state' && msg.state.type === 'receiver_state') {
        const e = elements();
        const hz = msg.state.center_hz;
        if (hz !== lastCentreHz && document.activeElement !== e.centre)
          e.centre.value = (hz / 1e6).toFixed(4);
        lastCentreHz = hz;
        const fft = msg.state.fft_size;
        if (fft && fft !== lastFftSize) {
          e.fftValue.textContent = fft;
          lastFftSize = fft;
        }
        chart.onTuning(hz, msg.state.sample_rate_hz);
      }
    },
  };
})();
