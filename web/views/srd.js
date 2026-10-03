// The SRD view (web-visualization/07): the waterfall over 430-440 MHz, the
// two counters, and the table of decoded frames -- wrapped in an IIFE so
// `SrdView` is the only name this file adds to the shared global scope.
const SrdView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      const cv = (id) => {
        const c = document.getElementById(id);
        return { c: c, x: c.getContext('2d') };
      };
      els = {
        wf: document.getElementById('srd-waterfall'),
        wfWrap: document.getElementById('srd-waterfall-wrap'),
        grid: document.getElementById('srd-charts-grid'),
        charts: document.getElementById('srd-charts'),
        cc: { env: cv('srd-c-env'), chips: cv('srd-c-chips') },
        axis: document.getElementById('srd-axis'),
        head: document.getElementById('srd-head'),
        rows: document.getElementById('srd-rows'),
        count: document.getElementById('srd-count'),
      };
      els.wfCtx = els.wf.getContext('2d');
      els.charts.onclick = () => showCharts(!charting);
    }
    return els;
  }

  const TRACE = '#5adcc8';
  const BAR = '#a9c5d6';
  let charting = false;
  // The stream set, swapped on the toggle: the charts view wants the two
  // analysis streams and not the waterfall (hidden then). srd_state is in
  // both, so the frame table is always fed. Mutated in place.
  const SIGNAL_STREAMS = ['srd_state', 'waterfall'];
  const CHART_STREAMS = ['srd_state', 'srd_envelope', 'srd_chips'];
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
  }

  function clearChart(x, w, h) {
    x.fillStyle = '#0a0f16';
    x.fillRect(0, 0, w, h);
  }

  // The demodulated envelope (the work rate): a flat line is no signal, a
  // burst is the transmission's amplitude over time. Normalised to its own
  // peak, the way the window scales its y axis.
  function drawEnvelope(data) {
    const g = elements().cc.env, w = g.c.width, h = g.c.height, n = data.length;
    clearChart(g.x, w, h);
    if (n <= 0) return;
    let max = 0;
    for (let k = 0; k < n; k++) if (data[k] > max) max = data[k];
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

  // The discretised chips (preamble, delimiter, data): a bar per chip, 0 or 1.
  function drawChips(data) {
    const g = elements().cc.chips, w = g.c.width, h = g.c.height, n = data.length;
    clearChart(g.x, w, h);
    if (n <= 0) return;
    const bw = w / n;
    g.x.fillStyle = BAR;
    for (let k = 0; k < n; k++) {
      const bh = Math.max(0, Math.min(1, data[k])) * (h - 2);
      g.x.fillRect(k * bw, h - bh, Math.max(1, bw - 0.5), bh);
    }
  }

  const ROW_LABEL = '#7e97a6';
  const ROW_VALUE = '#c7d3dc';
  const HEAD_COLOR = '#96b0ca';   /* 150, 176, 202 */
  const WARN_COLOR = '#fabe4a';   /* 250, 190, 74 */
  // The window colours a decoded kind apart from a burst that decoded to
  // nothing, because those are different answers.
  const KIND_COLOR = {
    FULL: '#63e4aa', REPEAT: '#63e4aa', GENERIC: '#5adcc8',
    WAKEUP: '#8fb8d8', UNDECODED: '#7e97a6', unknown: '#7e97a6',
  };

  const wf = createWaterfall();

  function drawWaterfall(row) {
    const { wf: canvas, wfCtx } = elements();
    waterfallPush(wf, row);
    waterfallDrawNewest(wfCtx, canvas, row);
  }

  function renderAxis(state) {
    const { axis } = elements();
    const lower = state.center_hz - state.sample_rate_hz / 2;
    const upper = state.center_hz + state.sample_rate_hz / 2;
    axis.textContent = (lower / 1e6).toFixed(3) + ' - '
      + (upper / 1e6).toFixed(3) + ' MHz   (newest at top)';
  }

  function renderState(s) {
    const e = elements();

    // Outside the allocation there is nothing to expect, and an empty table
    // says nothing about the air. The server decides "inside 430-440 MHz at
    // 1 MS/s or more" -- the whole band would need 10 MS/s, so `ready` is
    // about where the receiver is pointed, not about covering it.
    if (!s.ready) {
      // Two answers, not one: a receiver pointed elsewhere can be retuned
      // and a capture holds the one tuning it was taken at. The server
      // decides which (`srd_readiness_name()`), so the window and this page
      // cannot word the same state differently -- this page used to say one
      // sentence for both.
      e.head.textContent = s.readiness === 'receiver-elsewhere'
        ? 'Receiver is outside 430-440 MHz; retune to hear SRD'
        : 'Capture is not 430-440 MHz / >=1 MS/s; no SRD signal expected';
      e.head.style.color = WARN_COLOR;
    } else {
      e.head.textContent = 'SRD 430-440 MHz   OOK / 2-FSK / Manchester   '
        + (s.centre_hz / 1e6).toFixed(4) + ' MHz   '
        + s.transmissions + ' transmission(s) found   '
        + s.frames + ' frame(s) decoded'
        + (s.have_carrier
            ? '   last carrier ' + (s.carrier_offset_hz >= 0 ? '+' : '')
              + (s.carrier_offset_hz / 1e3).toFixed(1) + ' kHz'
            : '')
        // The parameters the window's analysis panel shows, worded by the
        // server: the line code and the chip rate, with the divide-by-zero
        // guard already applied.
        + (s.have_parameters
            ? '   ' + s.line_code + '   ' + s.chip_us.toFixed(1) + ' us ('
              + s.chip_rate_hz.toFixed(0) + ' chip/s)'
            : '');
      e.head.style.color = HEAD_COLOR;
    }

    e.count.textContent = s.log.length;
    renderRows(e.rows, s.log.map((f) => [
      '<td style="color:' + ROW_LABEL + '">' + f.at.toFixed(1) + 's</td>',
      // The absolute frequency the row was heard at, fixed when it was
      // written -- not the current tuning plus an offset, which would drag
      // the whole history along on the next retune.
      '<td style="color:' + ROW_VALUE + '">'
        + (f.hz / 1e6).toFixed(4) + '</td>',
      '<td style="color:' + (KIND_COLOR[f.kind] || ROW_LABEL) + '">'
        + f.kind + '</td>',
      '<td style="color:' + ROW_LABEL + '">' + f.modulation + '</td>',
      // What the frame *proves* the device is. `unknown` for everything a
      // decode does not establish, which is most of this band.
      '<td style="color:' + ROW_LABEL + '">' + f.device + '</td>',
      // And what the row says about itself -- chosen by the server, in the
      // same words the window's table uses. This column used to compose its
      // own sentence out of the bit count and the violations, so the two
      // readers said different things about the same frame.
      '<td style="color:' + ROW_VALUE + '">' + f.detail + '</td>',
      '<td style="color:' + ROW_LABEL + '">' + f.bytes + '</td>',
    ]));
  }

  return {
    id: 'srd',
    label: 'SRD',
    streams: streams,
    resize: resizeCanvases,
    // An even split: a press produces a handful of frames rather than the
    // stream ADS-B carries, and the waterfall is where a reader sees the
    // transmission arrive at all.
    markup:
      '<div style="margin-bottom:8px;flex:0 0 auto">'
      + '<button id="srd-charts" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer">Show charts</button></div>'
      + '<div id="srd-waterfall-wrap" style="flex:2 1 0;min-height:100px;'
      + 'display:flex;flex-direction:column">'
      + '<canvas id="srd-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div class="label" id="srd-axis">awaiting receiver_state...</div>'
      + '</div>'
      // The analysis charts, two across, taking the waterfall's room when it
      // is hidden: the demodulated envelope and the discretised chips.
      + '<div id="srd-charts-grid" hidden style="display:none;flex:2 1 0;'
      + 'min-height:100px;grid-template-columns:repeat(2,1fr);gap:10px">'
      + '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column"><div class="label">Demodulated envelope '
      + '(work rate)</div><canvas id="srd-c-env" style="flex:1 1 0;'
      + 'min-height:0;width:100%"></canvas></div>'
      + '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column"><div class="label">Discretised chips '
      + '(preamble, delimiter, data)</div><canvas id="srd-c-chips" '
      + 'style="flex:1 1 0;min-height:0;width:100%"></canvas></div>'
      + '</div>'
      + '<div id="srd-head" style="font-size:13px;margin:4px 0 6px;color:'
      + ROW_LABEL + '">awaiting srd_state...</div>'
      + '<div class="label">decoded frames, newest first '
      + '(<span id="srd-count">0</span>)</div>'
      + '<div style="flex:2 1 0;min-height:0;overflow:auto">'
      + '<table><thead><tr><th>TIME</th><th>MHz</th><th>KIND</th>'
      + '<th>MOD</th><th>TYPE</th><th>DECODED</th><th>RAW (hex)</th>'
      + '</tr></thead>'
      + '<tbody id="srd-rows"></tbody></table></div>',
    render(msg) {
      if (msg.kind === 'waterfall_row') {
        if (!charting) drawWaterfall(msg.row);
      } else if (msg.kind === 'srd_envelope') {
        if (charting) drawEnvelope(msg.envelope);
      } else if (msg.kind === 'srd_chips') {
        if (charting) drawChips(msg.chips);
      } else if (msg.kind === 'state') {
        if (msg.state.type === 'srd_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
