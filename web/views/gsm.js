// The GSM view (web-visualization/07): the ARFCN waterfall, the two
// readouts, and the band's channel power scan -- wrapped in an IIFE so
// `GsmView` is the only name this file adds to the shared global scope
// (every file under web/ is concatenated into one <script>, so a top-level
// `const` collides with every other view's).
const GsmView = (function () {
  let els = null;

  // Lazy, always: `markup` is not in the document when this file runs --
  // viewer.js is concatenated last and inserts it at mountViews().
  function elements() {
    if (!els) {
      const cv = (id) => {
        const c = document.getElementById(id);
        return { c: c, x: c.getContext('2d') };
      };
      els = {
        wf: document.getElementById('gsm-waterfall'),
        wfWrap: document.getElementById('gsm-waterfall-wrap'),
        grid: document.getElementById('gsm-charts-grid'),
        charts: document.getElementById('gsm-charts'),
        cc: {
          corr: cv('gsm-c-corr'), soft: cv('gsm-c-soft'),
          phase: cv('gsm-c-phase'), scatter: cv('gsm-c-scatter'),
        },
        axis: document.getElementById('gsm-axis'),
        sch: document.getElementById('gsm-sch'),
        bcch: document.getElementById('gsm-bcch'),
        scan: document.getElementById('gsm-scan'),
        scanCaption: document.getElementById('gsm-scan-caption'),
        signalRows: document.getElementById('gsm-signal-rows'),
        cellRows: document.getElementById('gsm-cell-rows'),
      };
      els.wfCtx = els.wf.getContext('2d');
      els.scanCtx = els.scan.getContext('2d');
      els.charts.onclick = () => showCharts(!charting);
    }
    return els;
  }

  const TRACE = '#5adcc8';
  const GSMBAR = '#a9c5d6';
  let charting = false;
  // The stream set, swapped on the toggle. gsm_state is in both, so the
  // readouts and scan are always fed; the four chart streams and not the
  // waterfall are the charts view's. Mutated in place.
  const SIGNAL_STREAMS = ['gsm_state', 'waterfall'];
  const CHART_STREAMS = ['gsm_state', 'gsm_corr', 'gsm_soft', 'gsm_phase',
                         'gsm_scatter'];
  const streams = SIGNAL_STREAMS.slice();

  function showCharts(on) {
    const e = elements();
    charting = on;
    e.wfWrap.hidden = on;
    e.wfWrap.style.display = on ? 'none' : 'flex';
    e.grid.hidden = !on;
    e.grid.style.display = on ? 'grid' : 'none';
    e.charts.textContent = on ? 'Show waterfall' : 'Show burst charts';
    const want = on ? CHART_STREAMS : SIGNAL_STREAMS;
    streams.length = 0;
    for (const s of want) streams.push(s);
    if (typeof subscribeToActiveView === 'function') subscribeToActiveView();
    resizeCanvases();
  }

  function resizeCanvases() {
    const e = elements();
    const wfBox = measure(e.wf);
    if (fitCanvas(e.wf, wfBox.width, wfBox.height))
      waterfallRedraw(e.wfCtx, e.wf, wf);
    const scanBox = measure(e.scan);
    if (fitCanvas(e.scan, scanBox.width, scanBox.height)) drawScan(lastState);
    for (const key in e.cc) {
      const g = e.cc[key];
      const b = measure(g.c);
      fitCanvas(g.c, b.width, b.height);
    }
  }

  function clearChart(x, w, h) {
    x.fillStyle = '#0a0f16';
    x.fillRect(0, 0, w, h);
  }

  // A line over its own min..max (the correlation and phase both auto-scale).
  function drawCorr(key, data) {
    const g = elements().cc[key], w = g.c.width, h = g.c.height, n = data.length;
    clearChart(g.x, w, h);
    if (n <= 0) return;
    let lo = Infinity, hi = -Infinity;
    for (let k = 0; k < n; k++) {
      if (data[k] < lo) lo = data[k];
      if (data[k] > hi) hi = data[k];
    }
    if (hi - lo < 1e-9) hi = lo + 1;
    g.x.strokeStyle = TRACE;
    g.x.beginPath();
    for (let px = 0; px < w; px++) {
      const i = Math.floor(px * n / w);
      const y = h - ((data[i] - lo) / (hi - lo)) * (h - 2);
      if (px === 0) g.x.moveTo(px, y); else g.x.lineTo(px, y);
    }
    g.x.stroke();
  }

  // The soft magnitudes, as bars normalised to the burst's 90th percentile
  // (the window's own reference), outliers going past the top.
  function drawSoft(data) {
    const g = elements().cc.soft, w = g.c.width, h = g.c.height, n = data.length;
    clearChart(g.x, w, h);
    if (n <= 0) return;
    const sorted = Array.prototype.slice.call(data).sort((a, b) => a - b);
    let ref = sorted[Math.floor((n - 1) * 0.9)];
    if (ref < 1e-12) ref = 1e-12;
    const bw = w / n;
    g.x.fillStyle = GSMBAR;
    for (let k = 0; k < n; k++) {
      const v = Math.min(1.4, data[k] / ref);
      const bh = (v / 1.4) * (h - 2);
      g.x.fillRect(k * bw, h - bh, Math.max(1, bw - 0.5), bh);
    }
  }

  // The SCH constellation: points already on the unit circle (projected by the
  // server), two clusters meaning a clean differential decode.
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
      const x = w / 2 + px[k] * half;
      const y = h / 2 - py[k] * half;
      g.x.fillRect(x - 1, y - 1, 2, 2);
    }
  }

  // The window's own colours, as the hex of its exact RGB -- view_gsm.c for
  // the two readouts, fm.js's panel set for the rest. A panel that is nearly
  // the window's colour is one a reader looks at twice.
  const PANEL_FILL = '#111a25';
  const PANEL_EDGE = '#526d7e';
  const PANEL_CAPTION = '#97aebc';
  const ROW_LABEL = '#7e97a6';     /* 126, 151, 166 */
  const ROW_VALUE = '#d5e2ea';
  const SCH_COLOR = '#78e6ff';     /* 120, 230, 255 */
  const CELL_COLOR = '#99ebb2';    /* 153, 235, 178 */
  const QUIET_COLOR = '#7e97a6';   /* what the window uses for "not yet" */
  const RECORDING_COLOR = '#ffca69'; /* 255, 202, 105 */
  const BCCH_BAR = '#63e4aa';      /* a channel carrying an FCCH tone */
  const PLAIN_BAR = '#e08a4a';

  // GSM 900 downlink: ARFCN 1..124 on a 200 kHz raster. The *mapping* is the
  // server's (gsm_view_model.h mirrors the bound and asserts it at compile
  // time); these are only the axis this chart draws over.
  const FIRST_ARFCN = 1;
  const LAST_ARFCN = 124;

  // Whichever gsm_state arrived last, so a resize can redraw the scan
  // without waiting for the next block.
  let lastState = null;
  const wf = createWaterfall();   // lib/waterfall.js -- rows survive a resize

  function drawWaterfall(row) {
    const { wf: canvas, wfCtx } = elements();
    waterfallPush(wf, row);
    waterfallDrawNewest(wfCtx, canvas, row);
  }

  // The axis under the waterfall, labelled in ARFCN rather than hertz --
  // which is what makes this the GSM screen's waterfall and not the Scope's,
  // though the rows are byte-for-byte the same stream.
  function renderAxis(state) {
    const { axis } = elements();
    const lower = state.center_hz - state.sample_rate_hz / 2;
    const upper = state.center_hz + state.sample_rate_hz / 2;
    axis.textContent = 'GSM 900 ARFCN (200 kHz spacing)   '
      + (lower / 1e6).toFixed(3) + ' - ' + (upper / 1e6).toFixed(3) + ' MHz';
  }

  // The two readouts. Both arrive already decided, as names
  // (gsm_view_model.h) -- this picks a colour and a wording for a verdict it
  // was handed, and decides nothing.
  const SCH_TEXT = {
    idle: 'SCH   no channel selected',
    searching: 'SCH   searching for a synchronisation burst...',
    recording: 'SCH   recording raw I/Q',
  };
  const BCCH_TEXT = {
    none: '',
    waiting: 'BCCH  waiting for the multiframe to come round',
    missed: 'BCCH  a broadcast block is due here, and did not survive',
  };

  function schLine(s) {
    if (s.sch !== 'decoded') {
      return {
        text: SCH_TEXT[s.sch] || SCH_TEXT.idle,
        color: s.sch === 'recording' ? RECORDING_COLOR : QUIET_COLOR,
      };
    }
    return {
      text: 'SCH   BSIC ' + s.bsic + '  (NCC ' + s.ncc + ', BCC ' + s.bcc
        + ')   frame ' + s.frame_number + '  (T1/T2/T3 ' + s.t1 + '/' + s.t2
        + '/' + s.t3 + ')   match ' + s.confidence.toFixed(2)
        + (s.implausible ? '  [T1 JUMPED]' : ''),
      color: SCH_COLOR,
    };
  }

  function bcchLine(s) {
    if (s.bcch !== 'read')
      return { text: BCCH_TEXT[s.bcch] || '', color: QUIET_COLOR };
    let text = 'BCCH  ';
    if (s.have_lai)
      text += 'MCC ' + s.mcc + '  MNC '
        + String(s.mnc).padStart(s.mnc_digits, '0') + '  LAC ' + s.lac + '   ';
    if (s.have_cell_id) text += 'cell ' + s.cell_id + '   ';
    if (s.neighbours.length) text += 'neighbours ' + s.neighbours.join(' ');
    return { text, color: CELL_COLOR };
  }

  // The Channel Power Scan: one bar per ARFCN, green where the FCCH tone
  // detector is confident enough to call it a broadcast carrier -- the
  // threshold is the server's, carried as a confidence rather than as a
  // colour, so the window and this cannot come to disagree about it.
  const BCCH_CONFIDENT = 0.5;
  const SENTINEL = -300.0;

  function drawScan(s) {
    const { scan, scanCtx, scanCaption } = elements();
    const w = scan.width, h = scan.height;

    scanCtx.fillStyle = '#0a0f16';
    scanCtx.fillRect(0, 0, w, h);

    if (!s || !s.have_scan) {
      scanCtx.fillStyle = ROW_LABEL;
      scanCtx.font = '14px monospace';
      scanCtx.fillText(s && s.scanning ? 'scanning...'
        : 'a band scan needs a live receiver', 12, Math.round(h / 2));
      scanCaption.textContent = s && s.scanning
        ? 'Channel Power Scan -- step ' + s.step + ' / ' + s.step_count
        : 'Channel Power Scan';
      return;
    }

    // The floor is the weakest channel actually visited, not the sentinel:
    // scaling to -300 dBFS would squash every real bar into one pixel.
    let lo = Infinity, hi = -Infinity;
    for (let a = FIRST_ARFCN; a <= LAST_ARFCN; a++) {
      const p = s.power[a];
      if (p <= SENTINEL) continue;
      if (p < lo) lo = p;
      if (p > hi) hi = p;
    }
    if (!isFinite(lo)) return;
    if (hi - lo < 1) hi = lo + 1;

    const span = LAST_ARFCN - FIRST_ARFCN + 1;
    for (let a = FIRST_ARFCN; a <= LAST_ARFCN; a++) {
      const p = s.power[a];
      if (p <= SENTINEL) continue;
      const x = Math.round(w * (a - FIRST_ARFCN) / span);
      const bw = Math.max(1, Math.round(w / span) - 1);
      const y = Math.round(h * (1 - (p - lo) / (hi - lo)));
      scanCtx.fillStyle = s.bcch_confidence[a] >= BCCH_CONFIDENT
        ? BCCH_BAR : PLAIN_BAR;
      scanCtx.fillRect(x, y, bw, h - y);
    }
    // And the channel being inspected, marked the way the window marks it.
    if (s.arfcn > 0) {
      const x = Math.round(w * (s.arfcn - FIRST_ARFCN) / span);
      scanCtx.fillStyle = '#ffca69';
      scanCtx.fillRect(x, 0, 2, h);
    }
    scanCaption.textContent = 'Channel Power Scan   '
      + lo.toFixed(0) + ' to ' + hi.toFixed(0) + ' dBFS'
      + (s.arfcn > 0 ? '   inspecting ARFCN ' + s.arfcn : '');
  }

  function renderState(s) {
    const e = elements();
    lastState = s;

    const sch = schLine(s), bcch = bcchLine(s);
    e.sch.textContent = sch.text;
    e.sch.style.color = sch.color;
    e.bcch.textContent = bcch.text;
    e.bcch.style.color = bcch.color;

    renderRows(e.signalRows, s.stats_ready ? [
      row('noise (p10)', s.noise.toFixed(2)),
      row('signal (p99.5)', s.signal.toFixed(2)),
      row('estimated SNR', s.snr_db.toFixed(1) + ' dB'),
      row('clipping', s.clipping_percent.toFixed(4) + ' %'),
      row('headroom', s.headroom_db.toFixed(1) + ' dB'),
    ] : [row('signal', 'awaiting a block')]);

    renderRows(e.cellRows, s.bcch === 'read' ? [
      row('MCC / MNC', s.have_lai
        ? s.mcc + ' / ' + String(s.mnc).padStart(s.mnc_digits, '0') : '-'),
      row('location area', s.have_lai ? String(s.lac) : '-'),
      row('cell identity', s.have_cell_id ? String(s.cell_id) : '-'),
      row('neighbours', s.neighbours.length ? String(s.neighbours.length) : '0'),
      row('messages read', String(s.blocks)),
    ] : [row('cell', s.bcch === 'none' ? 'no synchronisation burst yet'
                                       : 'nothing read yet')]);

    drawScan(s);
  }

  function row(label, value) {
    return ['<td style="color:' + ROW_LABEL + '">' + label + '</td>',
            '<td style="color:' + ROW_VALUE + '">' + value + '</td>'];
  }

  // One grid cell: a label over a canvas that fills the rest.
  function gsmChartCell(canvasId, label) {
    return '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column"><div class="label">' + label + '</div>'
      + '<canvas id="' + canvasId + '" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas></div>';
  }

  function panel(caption, bodyHtml) {
    return '<div style="flex:1 1 0;min-width:0;background:' + PANEL_FILL
      + ';border:1px solid ' + PANEL_EDGE
      + ';padding:10px 12px 12px;overflow:auto">'
      + '<div style="color:' + PANEL_CAPTION
      + ';font-size:16px;margin-bottom:10px">' + caption + '</div>'
      + bodyHtml + '</div>';
  }

  return {
    id: 'gsm',
    label: 'GSM',
    // `waterfall` is the Scope's own stream and the GSM screen draws the very
    // same rows over the very same span -- only the axis differs. One stream,
    // two views, rather than a `gsm_waterfall` carrying identical bytes under
    // another name. `receiver_state` gives the span that axis is labelled
    // from, and the shell always subscribes to it.
    streams: streams,
    resize: resizeCanvases,
    markup:
      '<div style="margin-bottom:8px;flex:0 0 auto">'
      + '<button id="gsm-charts" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer">Show burst charts</button></div>'
      // The waterfall is what a reader watches; the channel scan is a
      // reference beside it, and on a capture it says only "needs a live
      // receiver". Weighted 3 to 1 -- measured at 1400x900, that is 381px
      // against 134 where an even split gave them 243 each.
      + '<div id="gsm-waterfall-wrap" style="flex:3 1 0;min-height:100px;'
      + 'display:flex;flex-direction:column">'
      + '<canvas id="gsm-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div class="label" id="gsm-axis">awaiting receiver_state...</div>'
      + '</div>'
      // The SCH burst analysis, a 2x2 grid taking the waterfall's room when it
      // is hidden: the timing-correlation landscape, the soft symbol
      // magnitudes, the phase trajectory and the SCH constellation.
      + '<div id="gsm-charts-grid" hidden style="display:none;flex:3 1 0;'
      + 'min-height:100px;grid-template-columns:repeat(2,1fr);'
      + 'grid-template-rows:repeat(2,1fr);gap:10px">'
      + gsmChartCell('gsm-c-corr', 'Timing correlation landscape')
      + gsmChartCell('gsm-c-soft', 'Soft symbol magnitudes')
      + gsmChartCell('gsm-c-phase', 'Differential phase trajectory')
      + gsmChartCell('gsm-c-scatter', 'SCH decoded symbols')
      + '</div>'
      + '<div id="gsm-sch" style="font-size:15px;margin:6px 0 2px;color:'
      + QUIET_COLOR + '">SCH   awaiting gsm_state...</div>'
      + '<div id="gsm-bcch" style="font-size:14px;margin-bottom:8px;color:'
      + QUIET_COLOR + '"></div>'
      + '<div class="label" id="gsm-scan-caption">Channel Power Scan</div>'
      + '<canvas id="gsm-scan" style="flex:1 1 0;min-height:60px;'
      + 'width:100%"></canvas>'
      + '<div style="display:flex;gap:16px;margin-top:10px;flex:0 1 auto;'
      + 'min-height:0">'
      + panel('Signal', '<table><tbody id="gsm-signal-rows"></tbody></table>')
      + panel('Cell', '<table><tbody id="gsm-cell-rows"></tbody></table>')
      + '</div>',
    render(msg) {
      if (msg.kind === 'waterfall_row') {
        if (!charting) drawWaterfall(msg.row);
      } else if (msg.kind === 'gsm_corr') {
        if (charting) drawCorr('corr', msg.data);
      } else if (msg.kind === 'gsm_soft') {
        if (charting) drawSoft(msg.data);
      } else if (msg.kind === 'gsm_phase') {
        if (charting) drawCorr('phase', msg.data);
      } else if (msg.kind === 'gsm_scatter') {
        if (charting) drawScatter(msg.x, msg.y);
      } else if (msg.kind === 'state') {
        if (msg.state.type === 'gsm_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
