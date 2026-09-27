// The ADS-B view (web-visualization/07): the waterfall, the funnel, and the
// decoded-message log -- wrapped in an IIFE so `AdsbView` is the only name
// this file adds to the shared global scope.
const AdsbView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      els = {
        wf: document.getElementById('adsb-waterfall'),
        axis: document.getElementById('adsb-axis'),
        head: document.getElementById('adsb-head'),
        funnel: document.getElementById('adsb-funnel'),
        rows: document.getElementById('adsb-rows'),
        count: document.getElementById('adsb-count'),
      };
      els.wfCtx = els.wf.getContext('2d');
    }
    return els;
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
    const lower = state.center_hz - state.sample_rate_hz / 2;
    const upper = state.center_hz + state.sample_rate_hz / 2;
    axis.textContent = (lower / 1e6).toFixed(3) + ' - '
      + (upper / 1e6).toFixed(3) + ' MHz   (newest at top)';
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

    e.count.textContent = s.log.length;
    renderRows(e.rows, s.log.map((m) => [
      '<td style="color:' + ROW_LABEL + '">' + m.stamp + '</td>',
      '<td style="color:' + ROW_VALUE + '">' + m.icao + '</td>',
      '<td style="color:' + (TYPE_COLOR[m.label] || ROW_LABEL) + '">'
        + m.label + '</td>',
      '<td style="color:' + ROW_VALUE + '">' + m.detail + '</td>',
      '<td style="color:' + ROW_LABEL + '">' + m.raw + '</td>',
    ]));
  }

  return {
    id: 'adsb',
    label: 'ADS-B',
    // `waterfall` is the Scope's own stream: the ADS-B screen draws the same
    // rows over the same span, so one stream serves both.
    streams: ['adsb_state', 'waterfall'],
    resize() {
      const e = elements();
      const box = measure(e.wf);
      if (fitCanvas(e.wf, box.width, box.height))
        waterfallRedraw(e.wfCtx, e.wf, wf);
    },
    // The log is the substance here and gets the larger share; the
    // waterfall is context. `flex:1 1 0` on both, so neither's content can
    // push the page past the viewport, and the table scrolls inside itself.
    markup:
      '<div id="adsb-waterfall-wrap" style="flex:2 1 0;min-height:100px;'
      + 'display:flex;flex-direction:column">'
      + '<canvas id="adsb-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div class="label" id="adsb-axis">awaiting receiver_state...</div>'
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
      if (msg.kind === 'waterfall_row') drawWaterfall(msg.row);
      else if (msg.kind === 'state') {
        if (msg.state.type === 'adsb_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
