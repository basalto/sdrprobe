// The Survey view (ticket 14, Phase 3's registry shape): `markup`,
// `streams`, `render(msg)` and nothing else -- wrapped in an IIFE so
// `SurveyView` is the only name this file adds to the shared global
// scope (see views/scope.js's comment for why). `lastCandidates` is
// whichever `survey_state` arrived last -- a candidate's mark is decided
// once, server-side (survey_mark_of(), src/model/survey_mark.h), and
// this file only draws it, never recomputing what the server decided.
const SurveyView = (function () {
  let els = null;
  function elements() {
    if (!els) {
      els = {
        surveyChart: document.getElementById('survey-chart'),
        surveyStatus: document.getElementById('survey-status'),
        surveyCount: document.getElementById('survey-count'),
        surveyRows: document.getElementById('survey-rows'),
        surveyDetail: document.getElementById('survey-detail'),
        from: document.getElementById('survey-from'),
        to: document.getElementById('survey-to'),
        dwell: document.getElementById('survey-dwell'),
        sweep: document.getElementById('survey-sweep'),
        ask: document.getElementById('survey-ask'),
        watch: document.getElementById('survey-watch'),
      };
      els.surveyCtx = els.surveyChart.getContext('2d');
      // Click a candidate to inspect it -- the window's own candidate click
      // (`survey_select`). Candidates travel in peak order, so the row's
      // position is the index the server wants. One handler on the tbody, and
      // the rows rebuild only when they change (below), so a click lands on a
      // row that is still there rather than on the tbody mid-rebuild.
      els.surveyRows.onclick = (ev) => {
        const tr = ev.target.closest('tr');
        if (!tr) return;
        const i = Array.prototype.indexOf.call(els.surveyRows.children, tr);
        if (i >= 0) sendCommand('select candidate ' + i);
      };
    }
    return els;
  }

  // A signature of what the candidate rows show, so they rebuild only when the
  // candidates change -- not every survey_state, which would destroy the row
  // under a click.
  let candidateSig = null;

  // `mark` arrives as survey_mark_name()'s own string, keyed
  // here by name and not by ordinal. It was indexed by the enum's integer
  // in an order this file re-declared wrong -- receiver-like and empty
  // swapped, the pair CLAUDE.md says a reader acts on, green throughout
  // (web-visualization/15). A name cannot be mis-ordered, and one the
  // server adds that this does not know falls back to a signal dot rather
  // than becoming a different mark.
  const MARK_CLASS = {
    signal: 'mark-signal', receiver: 'mark-receiver',
    empty: 'mark-empty', contested: 'mark-contested',
  };
  const MARK_GLYPH = { signal: '●', receiver: '✕', empty: '○', contested: '✕' };
  const MARK_COLOR = {
    signal: '#5adcc8', receiver: '#ff9b64', empty: '#8291a0',
    contested: '#ff6864',
  };
  let lastCandidates = [];

  // view_survey.c's own button colours, as the hex of their exact RGB, plus the
  // orange a running sweep's button takes.
  const PANEL_EDGE = '#526d7e';    /* 82, 109, 126 */
  const CAPTION = '#ebf2f6';       /* 235, 242, 246 */
  const LABEL = '#a6bcc9';
  const VALUE = '#ebf2f6';
  const ACTIVE = '#fabe4a';        /* 250, 190, 74 -- the window's active button */

  function send(line) {
    // viewer.js owns the socket; a view never opens one (ADR-0007, restated for
    // JavaScript in the web-view skill).
    if (typeof sendCommand === 'function') sendCommand(line);
  }

  // Whether the last state said a sweep is running, so the one button reads
  // "Sweep" when idle and "Stop" when sweeping -- and sends the matching
  // command. The server owns the truth; this only mirrors it.
  let lastSweeping = false;
  let lastWatching = false;

  function startOrStopSweep() {
    const e = elements();
    if (lastSweeping) { send('sweep stop'); return; }
    const from = e.from.value.trim();
    const to = e.to.value.trim();
    const dwell = e.dwell.value.trim();
    // The three travel as text; the server parses the spellings ("24M", "0.10")
    // with the same parser the window's fields use, and reports a bad range or
    // a missing receiver in the status line.
    if (from && to && dwell) send('sweep ' + from + ' ' + to + ' ' + dwell);
  }

  // The detail panel's tones, keyed by name (survey_detail_tone_name) -- the
  // same RGB view_survey.c's detail_tone_color() draws, so the panel reads the
  // same in both. Never keyed by the enum's ordinal (the swapped-marks lesson).
  const DETAIL_TONE = {
    bright: '#ebf2f6', value: '#d5e2ea', muted: '#7e97a6',
    active: '#fabe4a', info: '#95cde8', warn: '#c8a56e',
  };

  function esc(s) {
    return String(s).replace(/[&<>]/g,
      (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' }[c]));
  }

  // The Selected-candidate panel, rendered from the decided list the server
  // built (survey_detail_view): the browser chooses no sentence, only the
  // colour for a tone it is handed.
  function renderDetail(detail) {
    const el = elements().surveyDetail;
    if (!el) return;
    if (!detail || !detail.selected) {
      el.innerHTML = '<div style="color:' + CAPTION + '">Selected candidate</div>'
        + '<div style="color:' + LABEL + ';margin-top:10px">click a candidate '
        + 'in the chart or the list</div>';
      return;
    }
    el.innerHTML = (detail.lines || []).map((ln) => {
      const color = DETAIL_TONE[ln.tone] || DETAIL_TONE.value;
      const pad = ln.indent ? 'padding-left:16px;' : '';
      return '<div style="color:' + color + ';' + pad + 'margin:2px 0">'
        + esc(ln.text) + '</div>';
    }).join('');
  }

  function renderSurveyState(state) {
    const { surveyStatus, surveyCount, surveyRows, sweep, watch } = elements();
    surveyStatus.textContent = state.status;
    surveyCount.textContent = state.candidate_count;
    renderDetail(state.detail);
    lastSweeping = !!state.sweeping;
    lastWatching = !!state.watching;
    if (sweep) {
      sweep.textContent = lastSweeping ? 'Stop' : 'Sweep';
      sweep.style.background = lastSweeping ? ACTIVE : '#1b2836';
      sweep.style.color = lastSweeping ? '#0a0f16' : CAPTION;
    }
    // Watch reads "Stop watching" while a watch is folding each sweep into the
    // site's history, the way the window's button does.
    if (watch) {
      watch.textContent = lastWatching ? 'Stop watching' : 'Watch';
      watch.style.background = lastWatching ? ACTIVE : '#1b2836';
      watch.style.color = lastWatching ? '#0a0f16' : CAPTION;
    }
    lastCandidates = state.candidates || [];
    // Rebuilt only when the candidates change, so a click lands on a row that
    // is still there (the FM Band II lesson).
    const sig = lastCandidates.map((c) => Math.round(c.hz) + ':' + c.mark + ':'
      + c.power_dbfs.toFixed(1)).join('|');
    if (sig !== candidateSig) {
      candidateSig = sig;
      renderRows(surveyRows, lastCandidates.map((c) => {
        const cls = MARK_CLASS[c.mark] || 'mark-signal';
        const glyph = MARK_GLYPH[c.mark] || '●';
        return [
          '<td class="' + cls + '">' + glyph + '</td>',
          '<td>' + (c.hz / 1e6).toFixed(4) + ' MHz</td>',
          '<td>' + c.power_dbfs.toFixed(1) + ' dBFS</td>',
          '<td>' + (c.has_carrier ? Math.round(c.width_hz / 1e3) + ' kHz' : '-') + '</td>',
          '<td>' + (c.has_carrier ? c.shape : '-') + '</td>',
        ];
      }));
    }
  }

  // The same dBFS-to-y mapping the Scope's spectrum uses (lib/chart.js),
  // over the swept range instead of one block's -- and a mark above the
  // trace at each candidate's own frequency.
  function drawSurveyChart(lowerHz, upperHz, power) {
    const { surveyChart, surveyCtx } = elements();
    const w = surveyChart.width, h = surveyChart.height;
    const span = upperHz - lowerHz;
    surveyCtx.fillStyle = '#0a0f16';
    surveyCtx.fillRect(0, 0, w, h);
    plot(surveyCtx, w, h, power, '#5adcc8', dbfsToY);
    if (span > 0) {
      for (const c of lastCandidates) {
        const x = Math.round(w * (c.hz - lowerHz) / span);
        if (x < 0 || x > w) continue;
        surveyCtx.fillStyle = MARK_COLOR[c.mark] || '#5adcc8';
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

  return {
    id: 'survey',
    label: 'Survey',
    streams: ['survey_spectrum', 'survey_state'],
    // The sweep chart is matched to what CSS laid it out at. Clearing it
    // on a resize costs nothing here: unlike a waterfall this canvas is
    // redrawn whole from the next `survey_spectrum`, which carries the
    // entire swept range every time.
    resize() {
      const chart = elements().surveyChart;
      const box = measure(chart);

      fitCanvas(chart, box.width, box.height);
    },
    // The chart takes two thirds of the room and the candidate list the
    // rest, scrolling inside itself -- a sweep can find three signals or
    // three hundred, and the page must be the viewport either way.
    //
    // It was an even split, which spent half the panel on a table that is
    // *empty* on a capture (a sweep needs a live receiver) and has a
    // scrollbar when it is not. The chart is the thing being watched.
    // The window's Sweep controls: a range, a dwell, and the button that starts
    // the walk. The survey was otherwise read-only here -- able to watch a
    // sweep but never start one, which is why `./sdrprobe web` could only ever
    // show "press Sweep to survey the range" with no Sweep to press.
    mounted() {
      const e = elements();
      e.sweep.addEventListener('click', startOrStopSweep);
      // Enter in any field starts the sweep, the way the window commits a field.
      for (const f of [e.from, e.to, e.dwell])
        f.addEventListener('keydown', (ev) => {
          if (ev.key === 'Enter') startOrStopSweep();
        });
      // Ask again (the confirmation pass over what changed) and Watch (keep
      // sweeping, folding each into the site's history). The server refuses
      // with the session's own words -- no receiver, nothing to ask, no site.
      e.ask.addEventListener('click', () => send('survey ask'));
      e.watch.addEventListener('click',
        () => send(lastWatching ? 'survey watch stop' : 'survey watch'));
    },
    markup:
      '<div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap;'
      + 'flex:0 0 auto;margin-bottom:6px">'
      + '<label style="color:' + LABEL + '">Range <input id="survey-from" '
      + 'size="7" value="24M" style="background:#16222e;color:' + VALUE
      + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;padding:3px 6px">'
      + '</label>'
      + '<label style="color:' + LABEL + '">to <input id="survey-to" size="7" '
      + 'value="1766M" style="background:#16222e;color:' + VALUE
      + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;padding:3px 6px">'
      + '</label>'
      + '<label style="color:' + LABEL + '">dwell (s) <input id="survey-dwell" '
      + 'size="4" value="0.10" style="background:#16222e;color:' + VALUE
      + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;padding:3px 6px">'
      + '</label>'
      + '<button id="survey-sweep" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 12px;font:inherit;'
      + 'cursor:pointer">Sweep</button>'
      + '<button id="survey-ask" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 12px;font:inherit;'
      + 'cursor:pointer">Ask again</button>'
      + '<button id="survey-watch" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 12px;font:inherit;'
      + 'cursor:pointer">Watch</button>'
      + '</div>' +
      '<div class="label" id="survey-status">no sweep yet</div>' +
      '<canvas id="survey-chart" style="flex:2 1 0;min-height:120px;width:100%">'
      + '</canvas>' +
      '<div class="label">candidates (<span id="survey-count">0</span>)</div>' +
      // Candidates on the left, the Selected-candidate detail on the right --
      // the window's own two-panel arrangement. Each scrolls inside itself.
      '<div style="flex:1 1 0;min-height:0;display:flex;gap:10px">' +
      '<div style="flex:1 1 0;min-height:0;overflow:auto">' +
      '<table>' +
      '<thead><tr><th></th><th>frequency</th><th>level</th><th>width</th><th>shape</th></tr></thead>' +
      '<tbody id="survey-rows"></tbody>' +
      '</table></div>' +
      '<div id="survey-detail" style="flex:1 1 0;min-height:0;overflow:auto;'
      + 'border:1px solid ' + PANEL_EDGE + ';padding:8px 10px;font-size:12px">'
      + '<div style="color:' + CAPTION + '">Selected candidate</div>'
      + '<div style="color:' + LABEL + ';margin-top:10px">click a candidate in '
      + 'the chart or the list</div></div>' +
      '</div>',
    render(msg) {
      if (msg.kind === 'survey_spectrum') drawSurveyChart(msg.lowerHz, msg.upperHz, msg.power);
      else if (msg.kind === 'state' && msg.state.type === 'survey_state') renderSurveyState(msg.state);
    },
  };
})();
