(function(){
  const hud = document.getElementById('hud');
  const health = document.getElementById('health');
  const specCanvas = document.getElementById('spectrum');
  const specCtx = specCanvas.getContext('2d');
  const wfCanvas = document.getElementById('waterfall');
  const wfCtx = wfCanvas.getContext('2d');
  const surveyChart = document.getElementById('survey-chart');
  const surveyCtx = surveyChart.getContext('2d');
  const surveyStatus = document.getElementById('survey-status');
  const surveyCount = document.getElementById('survey-count');
  const surveyRows = document.getElementById('survey-rows');
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
      // -- the early 'stale generation' return included -- is timed the
      // same way, rather than each needing its own bookkeeping line.
      const t0 = performance.now();
      try {
        bytesThisWindow += (typeof ev.data === 'string') ? ev.data.length : ev.data.byteLength;
        if (typeof ev.data === 'string') { handleState(JSON.parse(ev.data)); return; }
        const view = new DataView(ev.data);
        const type = view.getUint8(1);
        const generation = view.getUint32(4, true);
        const bins = view.getUint32(16, true);
        if (generation < latestGeneration) { dropped++; return; } // ADR-0027
        sent++;
        if (type === 1) { // spectrum: bins average, then bins peak
          const average = new Float32Array(ev.data, 20, bins);
          const peak = new Float32Array(ev.data, 20 + bins * 4, bins);
          drawSpectrum(average, peak);
        } else if (type === 2) { // waterfall_row: bins of one row
          drawWaterfall(new Float32Array(ev.data, 20, bins));
        } else if (type === 3) { // survey_spectrum: ticket 07's wider header
          const lowerHz = view.getUint32(20, true);
          const upperHz = view.getUint32(24, true);
          const power = new Float32Array(ev.data, 28, bins);
          drawSurveyChart(lowerHz, upperHz, power);
        }
      } finally {
        busyMs += performance.now() - t0;
      }
    };
  }

  function handleState(state) {
    // Stored, not rendered here: link_health arrives up to once per
    // server block (tens of times a second), and a full innerHTML
    // rebuild on every one of them is JS busy time this page does not
    // need to spend -- the once-a-second interval below already redraws
    // the panel, and spending less time here is less time not reading
    // the socket, which is less backpressure this page itself causes.
    if (state.type === 'link_health') { lastHealth = state; return; }
    if (state.type === 'survey_state') { renderSurveyState(state); return; }
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

  // Ticket 07's Survey tab: the status line, the candidate table, and the
  // marks the next survey_spectrum message draws over the trace. `mark`
  // is sdrgui_survey_peak_mark()'s own enum, named here rather than
  // reinterpreting the flag word the server already decided from --
  // 0 signal, 1 empty, 2 receiver-like, 3 contested (sdrgui.h).
  const MARK_CLASS = ['mark-signal', 'mark-empty', 'mark-receiver', 'mark-contested'];
  const MARK_GLYPH = ['●', '○', '✕', '✕'];
  let lastCandidates = [];

  function renderSurveyState(state) {
    surveyStatus.textContent = state.status;
    surveyCount.textContent = state.candidate_count;
    lastCandidates = state.candidates || [];
    surveyRows.innerHTML = lastCandidates.map((c) => {
      const cls = MARK_CLASS[c.mark] || 'mark-signal';
      const glyph = MARK_GLYPH[c.mark] || '?';
      return '<tr><td class="' + cls + '">' + glyph + '</td>'
        + '<td>' + (c.hz / 1e6).toFixed(4) + ' MHz</td>'
        + '<td>' + c.power_dbfs.toFixed(1) + ' dBFS</td>'
        + '<td>' + (c.has_carrier ? Math.round(c.width_hz / 1e3) + ' kHz' : '-') + '</td>'
        + '<td>' + (c.has_carrier ? c.shape : '-') + '</td></tr>';
    }).join('');
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

  // Decimal SI throughout (AGENTS.md's units convention): a byte count is
  // KB/MB at 1000/1e6, a throughput is Kbps/Mbps -- bits, not bytes, at
  // the same decimal scale -- never KiB/MiB, never a silent /1024 behind
  // a "KB" label.
  function formatBytes(n) {
    if (n >= 1e6) return (n / 1e6).toFixed(2) + ' MB';
    return (n / 1e3).toFixed(1) + ' KB';
  }
  function formatBitsPerSecond(bytesPerSecond) {
    const bits = bytesPerSecond * 8;
    if (bits >= 1e6) return (bits / 1e6).toFixed(2) + ' Mbps';
    return (bits / 1e3).toFixed(1) + ' Kbps';
  }

  function dbfsToY(v, height) {
    const top = 0, bottom = -120; // SPECTRUM_TOP_DBFS and the floor this page assumes
    const clamped = Math.max(bottom, Math.min(top, v));
    return height * (top - clamped) / (top - bottom);
  }

  function drawSpectrum(average, peak) {
    const w = specCanvas.width, h = specCanvas.height;
    specCtx.fillStyle = '#0a0f16';
    specCtx.fillRect(0, 0, w, h);
    plot(peak, '#e8a355');
    plot(average, '#5adcc8');
    function plot(bins, color) {
      specCtx.strokeStyle = color;
      specCtx.beginPath();
      for (let x = 0; x < w; x++) {
        const i = Math.floor(x * bins.length / w);
        const y = dbfsToY(bins[i], h);
        if (x === 0) specCtx.moveTo(x, y); else specCtx.lineTo(x, y);
      }
      specCtx.stroke();
    }
  }

  // Ticket 07's Survey chart: the same dBFS-to-y mapping the Scope's
  // spectrum uses, over the swept range instead of one block's -- and a
  // mark above the trace at each candidate's own frequency, reading
  // `lastCandidates` from whichever survey_state arrived last rather
  // than recomputing anything the server already decided.
  function drawSurveyChart(lowerHz, upperHz, power) {
    const w = surveyChart.width, h = surveyChart.height;
    const span = upperHz - lowerHz;
    surveyCtx.fillStyle = '#0a0f16';
    surveyCtx.fillRect(0, 0, w, h);
    surveyCtx.strokeStyle = '#5adcc8';
    surveyCtx.beginPath();
    for (let x = 0; x < w; x++) {
      const i = Math.floor(x * power.length / w);
      const y = dbfsToY(power[i], h);
      if (x === 0) surveyCtx.moveTo(x, y); else surveyCtx.lineTo(x, y);
    }
    surveyCtx.stroke();
    if (span > 0) {
      for (const c of lastCandidates) {
        const x = Math.round(w * (c.hz - lowerHz) / span);
        if (x < 0 || x > w) continue;
        surveyCtx.fillStyle = ['#5adcc8', '#8291a0', '#ff9b64', '#ff6864'][c.mark] || '#5adcc8';
        surveyCtx.beginPath();
        surveyCtx.arc(x, dbfsToY(c.power_dbfs, h) - 6, 3, 0, 2 * Math.PI);
        surveyCtx.fill();
      }
      surveyCtx.fillStyle = '#8291a0';
      surveyCtx.font = '12px monospace';
      surveyCtx.fillText((lowerHz / 1e6).toFixed(3) + ' MHz', 4, h - 4);
      const upperLabel = (upperHz / 1e6).toFixed(3) + ' MHz';
      surveyCtx.fillText(upperLabel, w - 8 * upperLabel.length, h - 4);
    }
  }

  function colorFor(dbfs) {
    const t = Math.max(0, Math.min(1, (dbfs + 100) / 90));
    const r = Math.round(20 + 220 * t), g = Math.round(30 + 140 * t), b = Math.round(50 + 60 * (1 - t));
    return 'rgb(' + r + ',' + g + ',' + b + ')';
  }

  // Scrolls the existing image down by one row and draws only the new
  // one at the top, rather than redrawing all `h` rows from the row
  // history every time one more arrives -- the canvas itself is the
  // history now, which is also why this page no longer keeps one of its
  // own. Measured against the whole-redraw version this replaced: it
  // was 95%+ of this page's own JS busy time and growing as history
  // filled (drawImage's one call against up to `w * h` -- 900 * 200 --
  // individual fillRect calls per row).
  function drawWaterfall(row) {
    const w = wfCanvas.width, h = wfCanvas.height;
    wfCtx.drawImage(wfCanvas, 0, 0, w, h - 1, 0, 1, w, h - 1);
    for (let x = 0; x < w; x++) {
      const i = Math.floor(x * row.length / w);
      wfCtx.fillStyle = colorFor(row[i]);
      wfCtx.fillRect(x, 0, 1, 1);
    }
  }

  connect();
})();
