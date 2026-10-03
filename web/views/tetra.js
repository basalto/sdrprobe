// The TETRA view (web-visualization/07): the waterfall, the identity, the
// funnel, and the log of identities seen -- wrapped in an IIFE so
// `TetraView` is the only name this file adds to the shared global scope.
//
// "Show charts" mirrors the window's analysis arrangement: the waterfall is
// swapped for the phase-steps constellation and the repeats-within-a-slot
// profile, the identity and log staying below. The two charts arrive already
// computed by the server (tetra_runtime.c); this file draws what the window's
// charts read, and re-decides nothing.
const TetraView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      const wf = document.getElementById('tetra-waterfall');
      const cv = (id) => {
        const c = document.getElementById(id);
        return { c: c, x: c.getContext('2d') };
      };
      els = {
        wf, wfCtx: wf.getContext('2d'),
        wfWrap: document.getElementById('tetra-waterfall-wrap'),
        grid: document.getElementById('tetra-charts-grid'),
        charts: document.getElementById('tetra-charts'),
        cc: { phase: cv('tetra-c-phase'), profile: cv('tetra-c-profile') },
        profileCap: document.getElementById('tetra-c-profile-cap'),
        axis: document.getElementById('tetra-axis'),
        head: document.getElementById('tetra-head'),
        funnel: document.getElementById('tetra-funnel'),
        rows: document.getElementById('tetra-rows'),
        count: document.getElementById('tetra-count'),
      };
      els.charts.onclick = () => showCharts(!charting);
    }
    return els;
  }

  const ROW_LABEL = '#7e97a6';
  const ROW_VALUE = '#c7d3dc';
  const HEAD_COLOR = '#78e6ff';
  const WARN_COLOR = '#fabe4a';
  const NET_COLOR = '#ffca69';
  const SYNC_COLOR = '#63e4aa';
  const TRACE = '#5adcc8';
  const BAR = '#a9c5d6';

  let charting = false;
  // The stream set, swapped on the toggle: the signal view wants the
  // waterfall, the charts view the two analysis streams -- and not the
  // waterfall, hidden then and the page's largest stream. tetra_state is in
  // both, so the identity and log are always fed. Mutated in place so the
  // registry's reference stays valid.
  const SIGNAL_STREAMS = ['tetra_state', 'waterfall'];
  const CHART_STREAMS = ['tetra_state', 'tetra_scatter', 'tetra_profile'];
  const streams = SIGNAL_STREAMS.slice();

  const wf = createWaterfall();

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

  // The phase steps as points on a circle: four clusters is a clean QPSK
  // lock, one blob a carrier not resolving. x and y are cos/sin of the step,
  // so they sit within the unit circle.
  function drawPhase(px, py) {
    const g = elements().cc.phase, w = g.c.width, h = g.c.height;
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

  // How much of a 255-symbol slot repeats: a bar per position, 0 to 1.
  function drawProfile(data) {
    const g = elements().cc.profile, w = g.c.width, h = g.c.height;
    clearChart(g.x, w, h);
    const n = data.length;
    if (n <= 0) return;
    const bw = w / n;
    g.x.fillStyle = BAR;
    for (let k = 0; k < n; k++) {
      const bh = Math.max(0, Math.min(1, data[k])) * (h - 2);
      g.x.fillRect(k * bw, h - bh, Math.max(1, bw - 0.5), bh);
    }
  }

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

    // The profile's caption names how many positions the burst finder judged
    // fixed, the way the window's does.
    if (e.profileCap)
      e.profileCap.textContent = s.profile_valid
        ? 'Repeats within a 255-symbol slot: ' + s.profile_fixed + ' fixed'
        : 'Repeats within a slot';
  }

  return {
    id: 'tetra',
    label: 'TETRA',
    streams: streams,
    resize: resizeCanvases,
    // The identity table is short -- one row per identity, not per burst,
    // and a base station has one -- so the waterfall takes the larger
    // share here, unlike ADS-B where the log is the substance.
    markup:
      '<div style="margin-bottom:8px;flex:0 0 auto">'
      + '<button id="tetra-charts" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer">Show charts</button></div>'
      + '<div id="tetra-waterfall-wrap" style="flex:3 1 0;min-height:100px;'
      + 'display:flex;flex-direction:column">'
      + '<canvas id="tetra-waterfall" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas>'
      + '<div class="label" id="tetra-axis">awaiting receiver_state...</div>'
      + '</div>'
      // The analysis charts, two across, taking the waterfall's room when it
      // is hidden.
      + '<div id="tetra-charts-grid" hidden style="display:none;flex:3 1 0;'
      + 'min-height:100px;grid-template-columns:repeat(2,1fr);gap:10px">'
      + '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column"><div class="label">Phase steps</div>'
      + '<canvas id="tetra-c-phase" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas></div>'
      + '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column"><div class="label" id="tetra-c-profile-cap">'
      + 'Repeats within a slot</div>'
      + '<canvas id="tetra-c-profile" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas></div>'
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
      if (msg.kind === 'waterfall_row') {
        if (!charting) drawWaterfall(msg.row);
      } else if (msg.kind === 'tetra_scatter') {
        if (charting) drawPhase(msg.x, msg.y);
      } else if (msg.kind === 'tetra_profile') {
        if (charting) drawProfile(msg.profile);
      } else if (msg.kind === 'state') {
        if (msg.state.type === 'tetra_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
