// The TETRA view (web-visualization/07): the waterfall, the identity, the
// funnel, and the log of identities seen -- wrapped in an IIFE so
// `TetraView` is the only name this file adds to the shared global scope.
const TetraView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      els = {
        wf: document.getElementById('tetra-waterfall'),
        axis: document.getElementById('tetra-axis'),
        head: document.getElementById('tetra-head'),
        funnel: document.getElementById('tetra-funnel'),
        rows: document.getElementById('tetra-rows'),
        count: document.getElementById('tetra-count'),
      };
      els.wfCtx = els.wf.getContext('2d');
    }
    return els;
  }

  const ROW_LABEL = '#7e97a6';
  const ROW_VALUE = '#c7d3dc';
  const HEAD_COLOR = '#78e6ff';
  const WARN_COLOR = '#fabe4a';
  const NET_COLOR = '#ffca69';
  const SYNC_COLOR = '#63e4aa';

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

    // Three things produce an empty table and they are not the same. The
    // rate is the first of them, and it is about the receiver rather than
    // the air: the channel filter decimates by a whole number or not at
    // all, so at the wrong rate nothing could have decoded.
    if (!s.rate_supported) {
      e.head.textContent = 'this sample rate cannot decode TETRA -- the '
        + 'channel filter needs a whole-number decimation';
      e.head.style.color = WARN_COLOR;
    } else if (s.have_identity) {
      // "LA unread" is not "LA 0". The location area rides the broadcast
      // block, which is scrambled with the network's own colour code and
      // cannot be read until the synchronisation block has given that up --
      // so there is a window where the identity is real and the location
      // area is simply not known yet. The server decides (`la_read`).
      e.head.textContent = 'TETRA   MCC ' + s.mcc + '   MNC ' + s.mnc
        + '   colour code ' + s.colour
        + (s.la_read ? '   LA ' + s.la : '   LA unread');
      e.head.style.color = HEAD_COLOR;
    } else {
      e.head.textContent = 'TETRA   no identity yet   lock '
        + s.lock.toFixed(2);
      e.head.style.color = ROW_LABEL;
    }

    // The funnel, and its middle term is the point: a burst whose
    // synchronisation word matched and whose parity did not is TETRA too
    // weak to read, which no count of bursts alone can say.
    e.funnel.textContent = s.bursts_total + ' burst(s)   '
      + s.blocks_total + ' with parity   ' + s.blocks_failed + ' failed   '
      + s.broadcast_total + ' broadcast'
      + '   lock ' + s.lock.toFixed(2)
      + '   offset ' + s.offset_hz.toFixed(0) + ' Hz'
      + '   block ' + s.bursts + '/' + s.blocks + '/' + s.broadcast;

    e.count.textContent = s.log.length;
    renderRows(e.rows, s.log.map((r) => [
      '<td style="color:' + ROW_LABEL + '">' + r.at.toFixed(1) + 's</td>',
      '<td style="color:' + NET_COLOR + '">' + r.mcc + '-' + r.mnc + '</td>',
      '<td style="color:' + SYNC_COLOR + '">SYNC</td>',
      '<td style="color:' + ROW_VALUE + '">colour ' + r.colour
        + '   LA ' + r.la + '</td>',
      '<td style="color:' + ROW_LABEL + '">' + r.bursts + ' burst / '
        + r.blocks + ' block / ' + r.broadcast + ' bcast</td>',
    ]));
  }

  return {
    id: 'tetra',
    label: 'TETRA',
    streams: ['tetra_state', 'waterfall'],
    resize() {
      const e = elements();
      const box = measure(e.wf);
      if (fitCanvas(e.wf, box.width, box.height))
        waterfallRedraw(e.wfCtx, e.wf, wf);
    },
    // The identity table is short -- one row per identity, not per burst,
    // and a base station has one -- so the waterfall takes the larger
    // share here, unlike ADS-B where the log is the substance.
    markup:
      '<div id="tetra-waterfall-wrap" style="flex:3 1 0;min-height:100px;'
      + 'display:flex;flex-direction:column">'
      + '<canvas id="tetra-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div class="label" id="tetra-axis">awaiting receiver_state...</div>'
      + '</div>'
      + '<div id="tetra-head" style="font-size:13px;margin:4px 0 2px;color:'
      + ROW_LABEL + '">awaiting tetra_state...</div>'
      + '<div id="tetra-funnel" style="font-size:12px;margin-bottom:6px;'
      + 'color:' + ROW_LABEL + '"></div>'
      + '<div class="label">identities, newest first '
      + '(<span id="tetra-count">0</span>)</div>'
      + '<div style="flex:1 1 0;min-height:0;overflow:auto">'
      + '<table><thead><tr><th>TIME</th><th>NETWORK</th><th>TYPE</th>'
      + '<th>DECODED MESSAGE</th><th>COUNTS</th></tr></thead>'
      + '<tbody id="tetra-rows"></tbody></table></div>',
    render(msg) {
      if (msg.kind === 'waterfall_row') drawWaterfall(msg.row);
      else if (msg.kind === 'state') {
        if (msg.state.type === 'tetra_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
