// The LTE view (web-visualization/07): the waterfall, and the window's three
// panels -- what the band scan found, what the synchronisation signals found,
// and what the cell says about itself -- wrapped in an IIFE so `LteView` is
// the only name this file adds to the shared global scope.
//
// The largest of the seven views, and the one where the least is decided
// here: the statistics table, the findings' sentences, the PHICH wording, the
// scan's progress line and its four empty-table notes all arrive already
// chosen (`model/lte_view_model.h`). This picks colours and lays out rows.
const LteView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      const cv = (id) => {
        const c = document.getElementById(id);
        return { c: c, x: c.getContext('2d') };
      };
      els = {
        wf: document.getElementById('lte-waterfall'),
        wfWrap: document.getElementById('lte-waterfall-wrap'),
        markers: document.getElementById('lte-markers'),
        grid: document.getElementById('lte-charts-grid'),
        charts: document.getElementById('lte-charts'),
        scan: document.getElementById('lte-scan'),
        cc: {
          pss: cv('lte-c-pss'), sss: cv('lte-c-sss'),
          channel: cv('lte-c-channel'), ports: cv('lte-c-ports'),
          scatter: cv('lte-c-scatter'),
        },
        axis: document.getElementById('lte-axis'),
        head: document.getElementById('lte-head'),
        funnel: document.getElementById('lte-funnel'),
        scanRows: document.getElementById('lte-scan-rows'),
        scanNote: document.getElementById('lte-scan-note'),
        cellRows: document.getElementById('lte-cell-rows'),
        cellNote: document.getElementById('lte-cell-note'),
        mibRows: document.getElementById('lte-mib-rows'),
        mibNote: document.getElementById('lte-mib-note'),
        findings: document.getElementById('lte-findings'),
      };
      els.wfCtx = els.wf.getContext('2d');
      els.charts.onclick = () => showCharts(!charting);
      // The window's "Scan band" button, which the browser needs because in
      // `web` mode there is no window to press it: the scan list is empty
      // until a walk runs, and a walk takes a live receiver (the server says
      // so if there is none). It toggles on what the last lte_state reported,
      // so the same button stops a sweep. `scan lte`/`scan stop` are commands
      // like `tune` -- the program starts and stops the sweep; this only asks.
      els.scan.onclick = () =>
        sendCommand(scanning ? 'scan stop' : 'scan lte');
      // Click a scan row to park on that cell -- the window's own row click
      // (`scan_select`). The row index is its position in the tbody, which is
      // the order `found` travels in. One handler on the tbody, and the rows
      // are rebuilt only when they change (below), so a click lands on a row
      // that is still there rather than on the tbody mid-rebuild -- the race
      // the FM table hit.
      els.scanRows.onclick = (ev) => {
        const tr = ev.target.closest('tr');
        if (!tr) return;
        const i = Array.prototype.indexOf.call(els.scanRows.children, tr);
        if (i >= 0) sendCommand('select cell ' + i);
      };
    }
    return els;
  }

  // A signature of what the scan rows show, so they rebuild only when the
  // found cells change -- not every lte_state, which would destroy the row
  // under a click.
  let scanSig = null;

  const TRACE = '#5adcc8';
  const LTEBAR = '#a9c5d6';
  let charting = false;
  // What the last lte_state said the band walk is doing, so the Scan button
  // knows whether its next press starts or stops one.
  let scanning = false;
  // The received span and tuned centre, from the last receiver_state -- the
  // cell marker is positioned against the span. And the cell mark itself, from
  // the last lte_state: where it sits and what it says, empty until a cell is
  // found (a marker is a claim that something is there).
  let lastLowerHz = 0, lastUpperHz = 0;
  let markerHz = 0, markerLabel = '';
  // The stream set, swapped on the toggle. lte_state is in both, so the three
  // panels are always fed; the five chart streams and not the waterfall are
  // the charts view's. Mutated in place.
  const SIGNAL_STREAMS = ['lte_state', 'waterfall'];
  const CHART_STREAMS = ['lte_state', 'lte_pss', 'lte_sss', 'lte_channel',
                         'lte_ports', 'lte_scatter'];
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
    // The mark belongs to the waterfall, so it clears with the charts and
    // comes back with the signal view.
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
  }

  function clearChart(x, w, h) {
    x.fillStyle = '#0a0f16';
    x.fillRect(0, 0, w, h);
  }

  // A line auto-scaled to its own min..max -- the PSS, SSS and channel charts
  // all read against their own range.
  function drawLine(key, data) {
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

  // The antenna-port coherence: a bar per port against the 0..1 scale.
  function drawPorts(data) {
    const g = elements().cc.ports, w = g.c.width, h = g.c.height, n = data.length;
    clearChart(g.x, w, h);
    if (n <= 0) return;
    const bw = w / n;
    g.x.fillStyle = LTEBAR;
    for (let k = 0; k < n; k++) {
      const bh = Math.max(0, Math.min(1, data[k])) * (h - 2);
      g.x.fillRect(k * bw + 2, h - bh, Math.max(1, bw - 4), bh);
    }
  }

  // The PBCH constellation: two clusters is a clean QPSK decode.
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

  // view_lte.c's own Color constants, as the hex of their exact RGB.
  const PANEL_FILL = '#111a25';
  const PANEL_EDGE = '#2c3e50';    /* 44, 62, 80 */
  const PANEL_CAPTION = '#bbcdd8'; /* 187, 205, 216 */
  const ROW_LABEL = '#849cac';     /* 132, 156, 172 */
  const ROW_VALUE = '#e2ecf3';     /* 226, 236, 243 */
  const ROW_MUTED = '#788c9b';     /* 120, 140, 155 */
  const WARNING = '#fabe4a';       /* 250, 190, 74 */
  const HEAD_COLOR = '#97aebc';    /* 151, 174, 188 */

  const wf = createWaterfall();

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
    // The span moved, so the cell mark moves with it without waiting for the
    // next lte_state.
    renderMarkers();
  }

  /* The window's waterfall-mark colour, {80, 220, 240}: one pill over the
     found cell at the top of the waterfall, the same mark `view_lte.c` draws
     into the canvas, here as HTML over it so the scrolling picture never
     erases it. */
  const MARKER_COLOR = '#50dcf0';

  // The cell marker: one pill at the cell's own frequency across the received
  // span, from `marker_hz`/`marker` on lte_state -- empty when there is no
  // cell, and cleared while the charts are up (the mark belongs to the
  // waterfall). The window draws exactly one, highlighted; so does this.
  function renderMarkers() {
    const e = elements();
    const span = lastUpperHz - lastLowerHz;
    if (span <= 0 || charting || !markerLabel
        || markerHz < lastLowerHz || markerHz > lastUpperHz) {
      e.markers.innerHTML = '';
      return;
    }
    const pct = (100 * (markerHz - lastLowerHz) / span).toFixed(2);
    e.markers.innerHTML = '<div style="position:absolute;top:0;left:' + pct
      + '%;transform:translateX(-50%);white-space:nowrap;font:11px monospace;'
      + 'padding:1px 4px;border:1px solid ' + MARKER_COLOR + ';border-radius:2px;'
      + 'background:#133;font-weight:bold;color:' + MARKER_COLOR + '">'
      + escapeHtml(markerLabel) + '</div>';
  }

  // A label/value row, the shape every panel here uses.
  function row(label, value, color) {
    return [
      '<td style="color:' + ROW_LABEL + '">' + label + '</td>',
      '<td style="color:' + (color || ROW_VALUE) + '">' + value + '</td>',
    ];
  }

  // One line of the statistics table: smallest, mean, largest, and how many
  // readings are behind them. The count is not decoration -- a mean of one
  // reading is that reading, and the table exists to say which.
  function statRow(label, st, digits, sign) {
    const fmt = (v) => (sign && v > 0 ? '+' : '') + v.toFixed(digits);
    if (!st.count)
      return [
        '<td style="color:' + ROW_LABEL + '">' + label + '</td>',
        '<td colspan="3" style="color:' + ROW_MUTED + '">--</td>',
      ];
    return [
      '<td style="color:' + ROW_LABEL + '">' + label + '</td>',
      '<td style="color:' + ROW_MUTED + '">' + fmt(st.min) + '</td>',
      '<td style="color:' + ROW_VALUE + '">' + fmt(st.mean) + '</td>',
      '<td style="color:' + ROW_MUTED + '">' + fmt(st.max) + '</td>',
    ];
  }

  function renderState(s) {
    const e = elements();

    // The Scan button's label and the cell mark -- both from this state,
    // re-decided by nobody. `marker`/`marker_hz` are empty and 0 until a cell
    // has been found.
    scanning = !!s.scanning;
    e.scan.textContent = scanning ? 'Stop' : 'Scan band';
    markerLabel = s.marker || '';
    markerHz = s.marker_hz || 0;
    renderMarkers();

    e.head.textContent = 'LTE   '
      + (s.earfcn ? 'EARFCN ' + s.earfcn : 'off the EARFCN raster')
      + (s.band ? '   band ' + s.band : '')
      + '   ' + (s.centre_hz / 1e6).toFixed(3) + ' MHz'
      + (s.on_grid ? '' : '   [wrong sample rate]');
    // The window's own rule: a carrier with cells and no confirmed broadcast
    // is the case worth colouring, because it is the one a reader acts on.
    e.head.style.color =
      (!s.on_grid || (s.cells_found > 0 && s.mibs_confirmed === 0))
        ? WARNING : HEAD_COLOR;

    e.funnel.textContent = s.blocks_seen + ' block(s)   '
      + s.cells_found + ' cell   ' + s.mibs_decoded + ' broadcast decoded   '
      + s.mibs_confirmed + ' confirmed';

    // --- the scan, left column -------------------------------------------
    // Rebuilt only when the found cells change, so a click lands on a row that
    // is still there (the FM Band II lesson, one table over).
    const selected = (s.scan_selected === undefined) ? -1 : s.scan_selected;
    const sig = selected + '|' + s.found.map((f) => f.earfcn + ':' + f.pci + ':'
      + f.pss.toFixed(2)).join('|');
    if (sig !== scanSig) {
      scanSig = sig;
      renderRows(e.scanRows, s.found.map((f) => [
        '<td style="color:' + ROW_VALUE + '">'
          + (f.hz / 1e6).toFixed(1) + '</td>',
        '<td style="color:' + ROW_LABEL + '">' + f.earfcn + '</td>',
        '<td style="color:' + ROW_VALUE + '">' + f.pci + '</td>',
        '<td style="color:' + ROW_MUTED + '">' + f.pss.toFixed(2)
          + ' / ' + f.sss_margin.toFixed(2) + '</td>',
      // The row the receiver is parked on, highlighted the way the window
      // highlights its selected found cell. The selection is the server's
      // (`scan_select`), so it survives a reconnect and agrees with the window.
      ]), (i) => 'cursor:pointer'
        + (i === selected ? ';background:#1b3a2e' : ''));
    }
    // Both sentences arrive chosen: which of four reasons the table is
    // empty, and how far along a running pass is.
    // Three sentences, all chosen by the server: why the table is empty,
    // how far along a running pass is, and -- while one could be started --
    // what pressing Scan would cost.
    e.scanNote.textContent = s.scan_progress
      || [s.scan_note, s.scan_cost].filter((t) => t).join('   ');
    e.scanNote.style.color = s.scanning ? WARNING : ROW_MUTED;

    // --- what PSS and SSS found ------------------------------------------
    if (!s.cell_valid) {
      renderRows(e.cellRows, []);
      e.cellNote.textContent = s.status || 'Waiting for samples...';
      // Off the 1.92 MS/s grid nothing could have decoded, which is a
      // different answer from "nothing is transmitting" (ADR-0014).
      e.cellNote.style.color = s.on_grid ? ROW_MUTED : WARNING;
    } else {
      const st = s.stats;
      const rows = [
        row('Cell identity', s.pci),
        row('N_ID_1 / N_ID_2', s.n_id_1 + ' and ' + s.n_id_2),
        row('Cyclic prefix', s.extended_cp ? 'extended' : 'normal'),
        row('Subframe 0 at', 'sample ' + s.subframe_sample + ', '
            + (s.second_half ? 'second' : 'first') + ' half'),
        // Parts per million only: the hertz are in the table below, and
        // printing both puts the same measurement on screen twice. The ppm
        // is the figure that transfers -- it is a property of the
        // receiver's crystal, not of this carrier.
        row('Crystal error', (s.crystal_ppm >= 0 ? '+' : '')
            + s.crystal_ppm.toFixed(1) + ' ppm  ('
            + (s.crystal_subcarriers >= 0 ? '+' : '')
            + s.crystal_subcarriers + ' sc)'),
        [
          '<td></td>',
          '<td style="color:' + ROW_LABEL + '">min</td>',
          '<td style="color:' + ROW_LABEL + '">mean</td>',
          '<td style="color:' + ROW_LABEL + '">max</td>',
        ],
        statRow('Freq offset kHz', st.frequency_khz, 1, true),
        statRow('PSS correlation', st.pss, 2, false),
        statRow('SSS correlation', st.sss, 2, false),
        statRow('RSRP dBFS', st.rsrp_dbfs, 1, false),
        statRow('RSRQ dB', st.rsrq_db, 1, false),
        statRow('RS-SINR dB', st.sinr_db, 1, false),
        statRow('Delay ns', st.delay_ns, 0, true),
        statRow('Spread ns', st.spread_ns, 0, false),
        statRow('Drift Hz', st.drift_hz, 0, true),
        statRow('Antenna ports', st.ports, 0, false),
      ];
      renderRows(e.cellRows, rows);
      e.cellNote.textContent = st.rsrp_dbfs.count + ' blocks, last seen '
        + s.cell_age_seconds.toFixed(1) + ' s ago';
      e.cellNote.style.color = ROW_MUTED;
    }

    // --- what the cell says about itself ---------------------------------
    if (!s.mib_valid) {
      renderRows(e.mibRows, []);
      e.mibNote.textContent = s.cell_valid
        ? 'A cell is there; its broadcast has not survived its parity yet.'
        : 'Nothing to read until a cell is found.';
    } else {
      renderRows(e.mibRows, [
        row('Bandwidth', s.bandwidth_rb + ' blocks, '
            + s.bandwidth_mhz.toFixed(2) + ' MHz'),
        row('PHICH', s.phich),
        row('Frame number', s.frame_number + '  (quarter ' + s.quarter + ')'),
        row('Antenna ports', s.antenna_ports),
      ]);
      e.mibNote.textContent = 'last read ' + s.mib_age_seconds.toFixed(1)
        + ' s ago';
    }
    e.mibNote.style.color = ROW_MUTED;

    // The findings are sentences `lte_findings_from()` chose, printed whole.
    // One of them is a refusal and that is the point -- a reader who is not
    // told cannot know the question was asked.
    e.findings.innerHTML = s.findings.length
      ? s.findings.map((line) =>
          '<div style="margin-bottom:3px">' + escapeHtml(line) + '</div>')
        .join('')
      : '<div style="color:' + ROW_MUTED
        + '">Nothing measured yet, so nothing claimed.</div>';
  }

  function escapeHtml(text) {
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;')
      .replace(/>/g, '&gt;');
  }

  // One grid cell: a label over a canvas that fills the rest.
  function lteChartCell(canvasId, label) {
    return '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column"><div class="label">' + label + '</div>'
      + '<canvas id="' + canvasId + '" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas></div>';
  }

  function panel(caption, bodyHtml, grow) {
    return '<div style="flex:' + grow + ' 1 0;min-width:0;background:'
      + PANEL_FILL + ';border:1px solid ' + PANEL_EDGE
      + ';padding:8px 10px 10px;overflow:auto;display:flex;'
      + 'flex-direction:column">'
      + '<div style="color:' + PANEL_CAPTION
      + ';font-size:14px;margin-bottom:6px">' + caption + '</div>'
      + bodyHtml + '</div>';
  }

  return {
    id: 'lte',
    label: 'LTE',
    streams: streams,
    resize: resizeCanvases,
    // Three panels across the bottom in the window's own order -- what a
    // scan found, what the synchronisation signals found, what the cell says
    // -- with the middle one widest because it carries the statistics table.
    // The waterfall keeps a share rather than the window's larger one: this
    // screen's substance is the numbers, and a browser column is taller than
    // it is wide.
    markup:
      '<div style="margin-bottom:8px;flex:0 0 auto">'
      + '<button id="lte-scan" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer;margin-right:8px">Scan band</button>'
      + '<button id="lte-charts" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer">Show charts</button></div>'
      + '<div id="lte-waterfall-wrap" style="flex:2 1 0;min-height:90px;'
      + 'position:relative;display:flex;flex-direction:column">'
      + '<canvas id="lte-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div id="lte-markers" style="position:absolute;top:0;left:0;right:0;'
      + 'height:0;pointer-events:none"></div>'
      + '<div class="label" id="lte-axis">awaiting receiver_state...</div>'
      + '</div>'
      // The cell-search charts, a 3x2 grid taking the waterfall's room when it
      // is hidden: the PSS correlation, the SSS candidate scores, the channel
      // over 72 subcarriers, the antenna-port coherence and the PBCH
      // constellation.
      + '<div id="lte-charts-grid" hidden style="display:none;flex:2 1 0;'
      + 'min-height:90px;grid-template-columns:repeat(3,1fr);'
      + 'grid-template-rows:repeat(2,1fr);gap:10px">'
      + lteChartCell('lte-c-pss', 'PSS correlation')
      + lteChartCell('lte-c-sss', 'SSS candidate scores (168 N_ID_1)')
      + lteChartCell('lte-c-channel', 'Channel over 72 subcarriers (dB)')
      + lteChartCell('lte-c-ports', 'Antenna-port coherence')
      + lteChartCell('lte-c-scatter', 'PBCH constellation')
      + '</div>'
      + '<div id="lte-head" style="font-size:14px;margin:4px 0 2px;color:'
      + HEAD_COLOR + '">awaiting lte_state...</div>'
      + '<div id="lte-funnel" style="font-size:12px;margin-bottom:6px;'
      + 'color:' + ROW_LABEL + '"></div>'
      + '<div style="display:flex;gap:10px;flex:3 1 0;min-height:0">'
      + panel('Scan -- MHz, cell, PSS/margin',
              '<table><thead><tr><th>MHz</th><th>EARFCN</th><th>PCI</th>'
              + '<th>PSS / MARGIN</th></tr></thead>'
              + '<tbody id="lte-scan-rows"></tbody></table>'
              + '<div id="lte-scan-note" style="font-size:12px;'
              + 'margin-top:6px;color:' + ROW_MUTED + '"></div>', 2)
      + panel('Cell search -- what PSS and SSS found',
              '<table><tbody id="lte-cell-rows"></tbody></table>'
              + '<div id="lte-cell-note" style="font-size:12px;'
              + 'margin-top:6px;color:' + ROW_MUTED + '"></div>', 3)
      + panel('Broadcast -- what the cell says about itself',
              '<table><tbody id="lte-mib-rows"></tbody></table>'
              + '<div id="lte-mib-note" style="font-size:12px;'
              + 'margin:6px 0 8px;color:' + ROW_MUTED + '"></div>'
              + '<div id="lte-findings" style="font-size:12px;line-height:1.4;'
              + 'color:' + ROW_VALUE + '"></div>', 3)
      + '</div>',
    render(msg) {
      if (msg.kind === 'waterfall_row') {
        if (!charting) drawWaterfall(msg.row);
      } else if (msg.kind === 'lte_pss') {
        if (charting) drawLine('pss', msg.data);
      } else if (msg.kind === 'lte_sss') {
        if (charting) drawLine('sss', msg.data);
      } else if (msg.kind === 'lte_channel') {
        if (charting) drawLine('channel', msg.data);
      } else if (msg.kind === 'lte_ports') {
        if (charting) drawPorts(msg.data);
      } else if (msg.kind === 'lte_scatter') {
        if (charting) drawScatter(msg.x, msg.y);
      } else if (msg.kind === 'state') {
        if (msg.state.type === 'lte_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
