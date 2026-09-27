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
      els = {
        wf: document.getElementById('gsm-waterfall'),
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
    }
    return els;
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
    streams: ['gsm_state', 'waterfall'],
    resize() {
      const e = elements();
      // `flex:1 1 0`, not `auto`: a canvas's content size *is* its backing
      // store, so with an `auto` basis fitting the store changes the box and
      // the two chase each other a pixel at a time (web-visualization/07's
      // FM findings). Keep the rows -- resizing a canvas clears it.
      const wfBox = measure(e.wf);
      if (fitCanvas(e.wf, wfBox.width, wfBox.height))
        waterfallRedraw(e.wfCtx, e.wf, wf);
      const scanBox = measure(e.scan);
      if (fitCanvas(e.scan, scanBox.width, scanBox.height)) drawScan(lastState);
    },
    markup:
      // The waterfall is what a reader watches; the channel scan is a
      // reference beside it, and on a capture it says only "needs a live
      // receiver". Weighted 3 to 1 -- measured at 1400x900, that is 381px
      // against 134 where an even split gave them 243 each.
      '<div id="gsm-waterfall-wrap" style="flex:3 1 0;min-height:100px;'
      + 'display:flex;flex-direction:column">'
      + '<canvas id="gsm-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div class="label" id="gsm-axis">awaiting receiver_state...</div>'
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
      if (msg.kind === 'waterfall_row') drawWaterfall(msg.row);
      else if (msg.kind === 'state') {
        if (msg.state.type === 'gsm_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
