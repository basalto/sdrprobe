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
      els = {
        wf: document.getElementById('lte-waterfall'),
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
    }
    return els;
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
    const lower = state.center_hz - state.sample_rate_hz / 2;
    const upper = state.center_hz + state.sample_rate_hz / 2;
    axis.textContent = (lower / 1e6).toFixed(3) + ' - '
      + (upper / 1e6).toFixed(3) + ' MHz   (newest at top)';
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
    renderRows(e.scanRows, s.found.map((f) => [
      '<td style="color:' + ROW_VALUE + '">'
        + (f.hz / 1e6).toFixed(1) + '</td>',
      '<td style="color:' + ROW_LABEL + '">' + f.earfcn + '</td>',
      '<td style="color:' + ROW_VALUE + '">' + f.pci + '</td>',
      '<td style="color:' + ROW_MUTED + '">' + f.pss.toFixed(2)
        + ' / ' + f.sss_margin.toFixed(2) + '</td>',
    ]));
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
    streams: ['lte_state', 'waterfall'],
    resize() {
      const e = elements();
      const box = measure(e.wf);
      if (fitCanvas(e.wf, box.width, box.height))
        waterfallRedraw(e.wfCtx, e.wf, wf);
    },
    // Three panels across the bottom in the window's own order -- what a
    // scan found, what the synchronisation signals found, what the cell says
    // -- with the middle one widest because it carries the statistics table.
    // The waterfall keeps a share rather than the window's larger one: this
    // screen's substance is the numbers, and a browser column is taller than
    // it is wide.
    markup:
      '<div id="lte-waterfall-wrap" style="flex:2 1 0;min-height:90px;'
      + 'display:flex;flex-direction:column">'
      + '<canvas id="lte-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div class="label" id="lte-axis">awaiting receiver_state...</div>'
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
      if (msg.kind === 'waterfall_row') drawWaterfall(msg.row);
      else if (msg.kind === 'state') {
        if (msg.state.type === 'lte_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
