// The ADS-B view (web-visualization/07): the waterfall, the funnel, and the
// decoded-message log -- wrapped in an IIFE so `AdsbView` is the only name
// this file adds to the shared global scope.
const AdsbView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      const cv = (id) => {
        const c = document.getElementById(id);
        return { c: c, x: c.getContext('2d') };
      };
      els = {
        wf: document.getElementById('adsb-waterfall'),
        wfWrap: document.getElementById('adsb-waterfall-wrap'),
        grid: document.getElementById('adsb-charts-grid'),
        charts: document.getElementById('adsb-charts'),
        cc: {
          land: cv('adsb-c-land'), conf: cv('adsb-c-conf'),
          env: cv('adsb-c-env'), scatter: cv('adsb-c-scatter'),
        },
        markers: document.getElementById('adsb-markers'),
        axis: document.getElementById('adsb-axis'),
        head: document.getElementById('adsb-head'),
        funnel: document.getElementById('adsb-funnel'),
        rows: document.getElementById('adsb-rows'),
        count: document.getElementById('adsb-count'),
      };
      els.wfCtx = els.wf.getContext('2d');
      els.charts.onclick = () => showCharts(!charting);
      // Click a message row to select that aircraft -- the window's own
      // log-row and marker click (`selected_log`), a reader's cursor that
      // touches nothing on the air, so it stays in the browser. The rows
      // rebuild only when they change (below), so the click lands.
      els.rows.onclick = (ev) => {
        const tr = ev.target.closest('tr');
        if (!tr) return;
        const i = Array.prototype.indexOf.call(els.rows.children, tr);
        if (i >= 0) selectLog(i);
      };
      // And the same selection from the waterfall dot, which carries its log
      // index.
      els.markers.onclick = (ev) => {
        const dot = ev.target.closest('[data-i]');
        if (!dot) return;
        const i = parseInt(dot.dataset.i, 10);
        if (i >= 0) selectLog(i);
      };
    }
    return els;
  }

  // The selected message -- the window's `selected_log`, a per-viewer cursor.
  let selectedLog = -1;
  let lastLog = [], rowSig = null, markerSig = null;
  // The session clock at the last adsb_state, advanced locally so the dots
  // scroll smoothly down the waterfall between states (the SRD pattern).
  let lastNowSeconds = 0, lastNowStamp = 0;
  // One waterfall row is one sample block; its duration sets the scroll rate.
  const ADSB_BLOCK_PAIRS = 131072;
  function now() {
    return lastNowSeconds + (performance.now() - lastNowStamp) / 1000;
  }

  function selectLog(i) {
    selectedLog = (i === selectedLog) ? -1 : i;
    renderLog();
    renderMarkers();
  }

  const TRACE = '#5adcc8';
  const BAR = '#a9c5d6';
  /* The window's ADS-B marker colour, {80, 220, 240}. */
  const MARKER_COLOR = '#50dcf0';
  let charting = false;
  // The received span from the last receiver_state -- the aircraft dots all sit
  // at the 1090 MHz centre (x), separated only by age (y).
  let lastLowerHz = 0, lastUpperHz = 0;
  // The stream set, swapped on the toggle. adsb_state is in both, so the
  // funnel and log are always fed; the four chart streams and not the
  // waterfall are the charts view's. Mutated in place.
  const SIGNAL_STREAMS = ['adsb_state', 'waterfall'];
  const CHART_STREAMS = ['adsb_state', 'adsb_landscape', 'adsb_confidence',
                         'adsb_envelope', 'adsb_scatter'];
  const streams = SIGNAL_STREAMS.slice();

  function showCharts(on) {
    const e = elements();
    charting = on;
    e.wfWrap.hidden = on;
    e.wfWrap.style.display = on ? 'none' : 'flex';
    e.grid.hidden = !on;
    e.grid.style.display = on ? 'grid' : 'none';
    e.charts.textContent = on ? 'Show waterfall' : 'Show charts';
    const want = on ? CHART_STREAMS : SIGNAL_STREAMS;
    streams.length = 0;
    for (const s of want) streams.push(s);
    if (typeof subscribeToActiveView === 'function') subscribeToActiveView();
    resizeCanvases();
    // The dots belong to the waterfall, so they clear with the charts.
    renderMarkers();
  }

  function resizeCanvases() {
    const e = elements();
    const box = measure(e.wf);
    if (fitCanvas(e.wf, box.width, box.height))
      waterfallRedraw(e.wfCtx, e.wf, wf);
    for (const key in e.cc) {
      const g = e.cc[key];
      const b = measure(g.c);
      fitCanvas(g.c, b.width, b.height);
    }
    // The canvas height changed, so the age-to-pixel mapping did too.
    positionMarkers();
  }

  function clearChart(x, w, h) {
    x.fillStyle = '#0a0f16';
    x.fillRect(0, 0, w, h);
  }

  // A line over `data`, normalised to its own peak (or a fixed max). The
  // landscape and envelope are both lines the window scales this way.
  function drawLine(key, data, fixedMax) {
    const g = elements().cc[key], w = g.c.width, h = g.c.height, n = data.length;
    clearChart(g.x, w, h);
    if (n <= 0) return;
    let max = fixedMax || 0;
    if (!fixedMax) for (let k = 0; k < n; k++) if (data[k] > max) max = data[k];
    if (max <= 0) max = 1;
    g.x.strokeStyle = TRACE;
    g.x.beginPath();
    for (let px = 0; px < w; px++) {
      const i = Math.floor(px * n / w);
      const y = h - (data[i] / max) * (h - 2);
      if (px === 0) g.x.moveTo(px, y); else g.x.lineTo(px, y);
    }
    g.x.stroke();
  }

  // A bar per value, 0 to 1 (the bit confidence).
  function drawBars(key, data) {
    const g = elements().cc[key], w = g.c.width, h = g.c.height, n = data.length;
    clearChart(g.x, w, h);
    if (n <= 0) return;
    const bw = w / n;
    g.x.fillStyle = BAR;
    for (let k = 0; k < n; k++) {
      const bh = Math.max(0, Math.min(1, data[k])) * (h - 2);
      g.x.fillRect(k * bw, h - bh, Math.max(1, bw - 0.5), bh);
    }
  }

  // The bit decisions: signed margin (x) against amplitude (y). Two clusters
  // left and right is a clean frame; the axis centres on the expected
  // amplitude, as the window's does.
  function drawScatter(px, py) {
    const g = elements().cc.scatter, w = g.c.width, h = g.c.height;
    clearChart(g.x, w, h);
    g.x.strokeStyle = '#1b2531';
    g.x.beginPath();
    g.x.moveTo(w / 2 + 0.5, 0); g.x.lineTo(w / 2 + 0.5, h);
    g.x.moveTo(0, h / 2 + 0.5); g.x.lineTo(w, h / 2 + 0.5);
    g.x.stroke();
    const half = 0.46 * Math.min(w, h);
    g.x.fillStyle = TRACE;
    for (let k = 0; k < px.length; k++) {
      const x = w / 2 + (px[k] / 1.4) * half;
      const y = h / 2 - (py[k] / 1.4) * half;
      g.x.fillRect(x - 1, y - 1, 2, 2);
    }
  }

  // The window's own colours, as the hex of its exact RGB.
  const ROW_LABEL = '#7e97a6';
  const ROW_VALUE = '#c7d3dc';
  const HEAD_COLOR = '#78e6ff';
  const WARN_COLOR = '#fabe4a';
  const TYPE_COLOR = { POS: '#5adcc8', VEL: '#8fb8d8', MSG: '#97aebc' };

  const wf = createWaterfall();   // lib/waterfall.js -- rows survive a resize

  function drawWaterfall(row) {
    const { wf: canvas, wfCtx } = elements();
    waterfallPush(wf, row);
    waterfallDrawNewest(wfCtx, canvas, row);
  }

  function renderAxis(state) {
    const { axis } = elements();
    lastLowerHz = state.center_hz - state.sample_rate_hz / 2;
    lastUpperHz = state.center_hz + state.sample_rate_hz / 2;
    axis.textContent = (lastLowerHz / 1e6).toFixed(3) + ' - '
      + (lastUpperHz / 1e6).toFixed(3) + ' MHz   (newest at top)';
    positionMarkers();
  }

  // The aircraft dots: one per message at the 1090 MHz centre (x), aged down
  // the waterfall (y), coloured alike and filled when selected, with the ICAO
  // as a hover title rather than a drawn label -- the sky is busy, and a column
  // of dots reads where a stack of call-signs would not (the SRD pattern). Two
  // steps: rebuild the dot DOM only on a content or selection change (so a
  // click survives), then reposition the dots every waterfall row.
  function renderMarkers() {
    const e = elements();
    if (charting || lastUpperHz <= lastLowerHz) {
      e.markers.innerHTML = ''; markerSig = null; return;
    }
    const sig = selectedLog + '|' + lastLog.map((m) => m.icao).join('|');
    if (sig !== markerSig) {
      markerSig = sig;
      e.markers.innerHTML = lastLog.map((m, i) =>
        '<div data-i="' + i + '" id="adsb-mark-' + i + '" title="' + m.icao
        + '" style="position:absolute;left:50%;transform:translate(-50%,-50%);'
        + 'width:9px;height:9px;border:1px solid ' + MARKER_COLOR
        + ';border-radius:2px;background:'
        + (i === selectedLog ? MARKER_COLOR : 'rgba(10,15,22,0.55)')
        + ';cursor:pointer;pointer-events:auto"></div>').join('');
    }
    positionMarkers();
  }

  // Move each dot to its age's row, in CSS pixels down the canvas -- the
  // waterfall shows canvas.height rows, each ADSB_BLOCK_PAIRS/sample_rate
  // seconds, so a message of age A sits that fraction down. Older than the
  // canvas holds, or from the future, is hidden.
  function positionMarkers() {
    const e = elements();
    if (charting) return;
    const sampleRate = lastUpperHz - lastLowerHz;
    const cssH = measure(e.wf).height;
    if (sampleRate <= 0 || cssH <= 0 || e.wf.height <= 0) return;
    const visibleSeconds = e.wf.height * (ADSB_BLOCK_PAIRS / sampleRate);
    const t = now();
    for (const dot of e.markers.children) {
      const m = lastLog[+dot.dataset.i];
      const frac = m ? (t - m.time) / visibleSeconds : -1;
      if (frac < 0 || frac > 1) { dot.style.display = 'none'; continue; }
      dot.style.display = '';
      dot.style.top = (frac * cssH).toFixed(1) + 'px';
    }
  }

  // The message log, rebuilt only when the rows or the selection change so a
  // click lands; the selected row is highlighted the way the window's is.
  function renderLog() {
    const e = elements();
    const sig = selectedLog + '|' + lastLog.map((m) =>
      m.stamp + ':' + m.icao + ':' + m.label).join('|');
    if (sig === rowSig) return;
    rowSig = sig;
    renderRows(e.rows, lastLog.map((m) => [
      '<td style="color:' + ROW_LABEL + '">' + m.stamp + '</td>',
      '<td style="color:' + ROW_VALUE + '">' + m.icao + '</td>',
      '<td style="color:' + (TYPE_COLOR[m.label] || ROW_LABEL) + '">'
        + m.label + '</td>',
      '<td style="color:' + ROW_VALUE + '">' + m.detail + '</td>',
      '<td style="color:' + ROW_LABEL + '">' + m.raw + '</td>',
    ]), (i) => 'cursor:pointer'
      + (i === selectedLog ? ';background:#1b3a2e' : ''));
  }

  function renderState(s) {
    const e = elements();

    // `ready` is the server's answer to "could Mode S even be here", and it
    // is the difference between a quiet sky and a receiver pointed
    // elsewhere -- which an empty table cannot tell a reader by itself.
    if (!s.ready) {
      // Two answers, not one: a receiver pointed elsewhere can be retuned
      // and a capture holds the one tuning it was taken at. The server
      // decides which (`adsb_readiness_name()`); this page used to say one
      // sentence for both, where the window drew a Retune button for one.
      e.head.textContent = s.readiness === 'receiver-elsewhere'
        ? 'Receiver is not on 1090 MHz; retune to hear Mode S'
        : 'Capture is not 1090 MHz / 2 MS/s; no Mode S expected';
      e.head.style.color = WARN_COLOR;
    } else {
      e.head.textContent = '1090 MHz extended squitter   frames decoded '
        + s.frames + '   positions ' + s.positions;
      e.head.style.color = HEAD_COLOR;
    }

    // The funnel, in the window's own words and order: which stage stopped
    // is the diagnosis, so the arrow between them is the point.
    e.funnel.textContent = 'funnel   preambles ' + s.preambles
      + ' -> shaped ' + s.shaped + ' -> CRC failed ' + s.crc_failed
      + ' -> decoded ' + s.decoded
      + '   block ' + s.block_preambles + '/' + s.block_shaped + '/'
      + s.block_crc_failed + '/' + s.block_decoded;
    // Amber when frames are arriving and none of them decode -- the one
    // reading of this funnel a reader acts on, and the state an empty
    // message log cannot express. The window has coloured it for this since
    // before the browser existed; the server decides it now.
    e.funnel.style.color = s.funnel_warn ? WARN_COLOR : ROW_LABEL;

    lastLog = s.log || [];
    e.count.textContent = lastLog.length;
    // The clock the dots age against, re-based each state so `now()` advances
    // smoothly between them.
    if (s.now_seconds !== undefined) {
      lastNowSeconds = s.now_seconds;
      lastNowStamp = performance.now();
    }
    if (selectedLog >= lastLog.length) selectedLog = -1;
    renderLog();
    renderMarkers();
  }

  // One grid cell: a label over a canvas that fills the rest.
  function chartCell(canvasId, label) {
    return '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column"><div class="label">' + label + '</div>'
      + '<canvas id="' + canvasId + '" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas></div>';
  }

  return {
    id: 'adsb',
    label: 'ADS-B',
    streams: streams,
    resize: resizeCanvases,
    // The log is the substance here and gets the larger share; the
    // waterfall is context. `flex:1 1 0` on both, so neither's content can
    // push the page past the viewport, and the table scrolls inside itself.
    markup:
      '<div style="margin-bottom:8px;flex:0 0 auto">'
      + '<button id="adsb-charts" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer">Show charts</button></div>'
      + '<div id="adsb-waterfall-wrap" style="flex:2 1 0;min-height:100px;'
      + 'position:relative;display:flex;flex-direction:column">'
      + '<canvas id="adsb-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div id="adsb-markers" style="position:absolute;top:0;left:0;right:0;'
      + 'height:0"></div>'
      + '<div class="label" id="adsb-axis">awaiting receiver_state...</div>'
      + '</div>'
      // The analysis charts, a 2x2 grid taking the waterfall's room when it is
      // hidden: the preamble-score landscape and the frame envelope (lines),
      // the pulse-position bit confidence (bars), and the bit-decision scatter.
      + '<div id="adsb-charts-grid" hidden style="display:none;flex:2 1 0;'
      + 'min-height:100px;grid-template-columns:repeat(2,1fr);'
      + 'grid-template-rows:repeat(2,1fr);gap:10px">'
      + chartCell('adsb-c-land', 'Preamble score landscape')
      + chartCell('adsb-c-conf', 'Pulse-position bit confidence')
      + chartCell('adsb-c-env', 'Frame magnitude envelope')
      + chartCell('adsb-c-scatter', 'Bit decisions (margin vs amplitude)')
      + '</div>'
      + '<div id="adsb-head" style="font-size:13px;margin:4px 0 2px;color:'
      + ROW_LABEL + '">awaiting adsb_state...</div>'
      + '<div id="adsb-funnel" style="font-size:12px;margin-bottom:6px;'
      + 'color:' + ROW_LABEL + '"></div>'
      + '<div class="label">decoded messages, newest first '
      + '(<span id="adsb-count">0</span>)</div>'
      + '<div style="flex:3 1 0;min-height:0;overflow:auto">'
      + '<table><thead><tr><th>TIME</th><th>ICAO</th><th>TYPE</th>'
      + '<th>DECODED MESSAGE</th><th>RAW (hex)</th></tr></thead>'
      + '<tbody id="adsb-rows"></tbody></table></div>',
    render(msg) {
      if (msg.kind === 'waterfall_row') {
        // A new row is one row's worth of time; the dots step down with it,
        // positioning only (no rebuild), so a click in flight survives.
        if (!charting) { drawWaterfall(msg.row); positionMarkers(); }
      } else if (msg.kind === 'adsb_landscape') {
        if (charting) drawLine('land', msg.data, 0);
      } else if (msg.kind === 'adsb_confidence') {
        if (charting) drawBars('conf', msg.data);
      } else if (msg.kind === 'adsb_envelope') {
        if (charting) drawLine('env', msg.data, 1.2);
      } else if (msg.kind === 'adsb_scatter') {
        if (charting) drawScatter(msg.x, msg.y);
      } else if (msg.kind === 'state') {
        if (msg.state.type === 'adsb_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
