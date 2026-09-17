// The shell: the socket, reconnect, ADR-0027's generation rule, the
// subscription line, tab routing and the Health panel. Composes the
// pieces in lib/, wire.js and views/*.js; draws nothing itself.
const hud = document.getElementById('hud');
const health = document.getElementById('health');
const panelScope = document.getElementById('panel-scope');
const panelSurvey = document.getElementById('panel-survey');
const tabScope = document.getElementById('tab-scope');
const tabSurvey = document.getElementById('tab-survey');

let latestGeneration = 0; // the newest tuning_generation receiver_state has named
let sent = 0, dropped = 0; // this Viewer's own count of what it drew vs discarded
let ws = null; // module-scope so the tab buttons can send on it

// Ticket 07: which tab is showing, and which the server has confirmed.
// The click switches the panel at once -- a browser waiting a round
// trip to redraw a button press reads as broken -- and `receiver_state`
// corrects it afterwards if the two ever disagree (another Viewer
// switching it, or this page reconnecting mid-session with no `view`
// of its own yet sent). 0 is TAB_SURVEY, 1 is TAB_SCOPE
// (input_route.h's enum active_tab), matched here rather than
// reinvented, since receiver_state.tab is that enum's own int.
function showTab(tab) {
  const survey = tab === 0;
  panelScope.hidden = survey;
  panelSurvey.hidden = !survey;
  tabScope.classList.toggle('active', !survey);
  tabSurvey.classList.toggle('active', survey);
}
tabScope.onclick = () => { showTab(1); if (ws) ws.send('view scope'); };
tabSurvey.onclick = () => { showTab(0); if (ws) ws.send('view survey'); };

// Ticket 08's Health panel: what this page can measure about itself
// (received bytes/sec, its own JS busy time), rolled up once a second
// alongside whatever link_health last reported about the server side.
let lastHealth = null;
let bytesThisWindow = 0, throughputBps = 0;
let busyMs = 0, windowStart = performance.now(), jsBusyPercent = 0;
setInterval(() => {
  const now = performance.now();
  const elapsed = now - windowStart;
  jsBusyPercent = elapsed > 0 ? (100 * busyMs / elapsed) : 0;
  throughputBps = elapsed > 0 ? (bytesThisWindow * 1000 / elapsed) : 0;
  busyMs = 0; bytesThisWindow = 0; windowStart = now;
  renderHealth();
}, 1000);

function connect() {
  ws = new WebSocket('ws://' + location.host + '/viewer');
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => {
    ws.send('subscribe spectrum waterfall receiver_state link_health survey_spectrum survey_state');
    hud.textContent = 'connected, awaiting receiver state...';
  };
  ws.onclose = () => { hud.textContent = 'disconnected -- retrying...'; setTimeout(connect, 1000); };
  ws.onerror = () => ws.close();
  ws.onmessage = (ev) => {
    // Busy time and received bytes are this page's own two Health
    // fields (ticket 08); the try/finally means every exit path below
    // -- the early 'stale' return included -- is timed the same way,
    // rather than each needing its own bookkeeping line.
    const t0 = performance.now();
    try {
      const msg = decodeMessage(ev, latestGeneration); // wire.js
      bytesThisWindow += msg.bytes;
      if (msg.kind === 'stale') { dropped++; return; } // ADR-0027
      if (msg.kind === 'state') { handleState(msg.state); return; }
      sent++;
      if (msg.kind === 'spectrum') drawSpectrum(msg.average, msg.peak); // views/scope.js
      else if (msg.kind === 'waterfall_row') drawWaterfall(msg.row); // views/scope.js
      else if (msg.kind === 'survey_spectrum') drawSurveyChart(msg.lowerHz, msg.upperHz, msg.power); // views/survey.js
    } finally {
      busyMs += performance.now() - t0;
    }
  };
}

function handleState(state) {
  // Stored, not rendered here: link_health arrives up to once per
  // server block (tens of times a second), and a full innerHTML
  // rebuild on every one of them is JS busy time this page does not
  // need to spend -- the once-a-second interval above already redraws
  // the panel, and spending less time here is less time not reading
  // the socket, which is less backpressure this page itself causes.
  if (state.type === 'link_health') { lastHealth = state; return; }
  if (state.type === 'survey_state') { renderSurveyState(state); return; } // views/survey.js
  if (state.type !== 'receiver_state') return;
  latestGeneration = state.tuning_generation;
  showTab(state.tab); // corrects a click sent before the server answered,
                      // and reflects another Viewer's own switch
  hud.innerHTML = 'center ' + (state.center_hz / 1e6).toFixed(6) + ' MHz &nbsp; '
    + 'rate ' + (state.sample_rate_hz / 1e6).toFixed(3) + ' MS/s &nbsp; '
    + 'ppm ' + state.ppm + ' &nbsp; '
    + 'generation ' + state.tuning_generation + ' &nbsp; '
    + 'drawn ' + sent + ' declined ' + dropped;
}

// Per-stream sent/dropped/high-water are the server's own facts about
// this connection (nothing else could report a dropped message, since
// it never reaches here); received throughput and JS busy% are this
// page's own two, both rolled into the same panel.
function renderHealth() {
  const h = lastHealth;
  const dropRate = (s, d) => (s + d) > 0 ? (100 * d / (s + d)).toFixed(1) : '0.0';
  if (!h) { health.textContent = 'awaiting link_health...'; return; }
  health.innerHTML =
    '<div class="row">spectrum sent <span>' + h.spectrum_sent + '</span> dropped <span>'
      + h.spectrum_dropped + '</span> (<span>' + dropRate(h.spectrum_sent, h.spectrum_dropped) + '%</span>)</div>'
    + '<div class="row">waterfall sent <span>' + h.waterfall_sent + '</span> dropped <span>'
      + h.waterfall_dropped + '</span> (<span>' + dropRate(h.waterfall_sent, h.waterfall_dropped) + '%</span>)</div>'
    + '<div class="row">receiver_state sent <span>' + h.receiver_state_sent + '</span> dropped <span>'
      + h.receiver_state_dropped + '</span></div>'
    + '<div class="row">send-queue high-water <span>' + formatBytes(h.send_queue_high_water) + '</span></div>'
    + '<div class="row">received <span>' + formatBitsPerSecond(throughputBps) + '</span> (this tab)</div>'
    + '<div class="row">server CPU <span>' + h.server_cpu_percent.toFixed(1) + '%</span></div>'
    + '<div class="row">this tab, JS busy <span>' + jsBusyPercent.toFixed(1) + '%</span></div>';
}

connect();
