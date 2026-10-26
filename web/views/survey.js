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
        band: document.getElementById('survey-band'),
        sweep: document.getElementById('survey-sweep'),
        ask: document.getElementById('survey-ask'),
        watch: document.getElementById('survey-watch'),
        site: document.getElementById('survey-site'),
        antenna: document.getElementById('survey-antenna'),
        save: document.getElementById('survey-save'),
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
  // The reachable allocations from the last state, and whether the dropdown has
  // been filled from them. The list is constant for a session, so it is built
  // once -- rebuilding it every state would reset a half-open menu.
  let lastBands = [];
  let bandsPopulated = false;

  // A frequency the way the range fields spell it, matching survey_format_hz:
  // whole megahertz as "24M", otherwise a trimmed decimal.
  function formatMHz(hz) {
    const mhz = hz / 1e6;
    if (Number.isInteger(mhz)) return mhz + 'M';
    return mhz.toFixed(3).replace(/0+$/, '').replace(/\.$/, '') + 'M';
  }

  function chooseBand() {
    const e = elements();
    const i = e.band.selectedIndex - 1;   // 0 is the "Band..." placeholder
    const b = i >= 0 ? lastBands[i] : null;
    if (b) {
      // The window's Band... fills the range and dwell fields; so does this,
      // leaving Sweep to send them. No command: the fields stay authoritative.
      e.from.value = formatMHz(b.from_hz);
      e.to.value = formatMHz(b.to_hz);
      e.dwell.value = b.dwell_s.toFixed(2);
    }
    e.band.selectedIndex = 0;             // back to the placeholder
  }

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
    // The band list is constant for a session, so fill the dropdown once.
    lastBands = state.bands || [];
    if (!bandsPopulated && lastBands.length) {
      elements().band.innerHTML = '<option>Band…</option>'
        + lastBands.map((b) => '<option>' + esc(b.name) + '</option>').join('');
      bandsPopulated = true;
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

  // The sweep chart as a freqchart (lib/freqchart.js): the same rich axes,
  // zoom, pan and vertical scale as every other spectrum -- view-only, since a
  // sweep is a range of its own, not a receiver tuning, so a pan does not
  // retune. The candidate marks are drawn over the fresh trace through the
  // chart's own x/y mapping (its onRedraw hook), so they follow a zoom and drop
  // off when a candidate is panned out of view.
  const sweepChart = createFreqChart({ hasSpectrum: true, retune: false });

  function drawCandidateMarks() {
    const { surveyCtx } = elements();
    for (const c of lastCandidates) {
      const x = sweepChart.specX(c.hz);
      if (x === null) continue;
      surveyCtx.fillStyle = MARK_COLOR[c.mark] || '#5adcc8';
      surveyCtx.beginPath();
      surveyCtx.arc(x, sweepChart.specY(c.power_dbfs) - 6, 3, 0, 2 * Math.PI);
      surveyCtx.fill();
    }
  }

  function drawSurveyChart(lowerHz, upperHz, power) {
    sweepChart.onDataSpan(lowerHz, upperHz);
    sweepChart.renderSpectrum(power);   // onRedraw draws the candidate marks
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
      const c = elements().surveyChart;
      const box = measure(c);

      fitCanvas(c, box.width, box.height);
      sweepChart.resize();
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
      sweepChart.mount({
        specCanvas: e.surveyChart,
        isActive: () => {
          const p = document.getElementById('panel-survey');
          if (!p || p.hidden) return false;
          const a = document.activeElement;
          return !(a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA'));
        },
        onRedraw: drawCandidateMarks,
      });
      e.sweep.addEventListener('click', startOrStopSweep);
      // Band... fills the range and dwell from a reachable allocation.
      e.band.addEventListener('change', chooseBand);
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
      // Site and antenna commit on Enter, the way the window commits a field on
      // losing focus; Save writes the sweep and folds it into the site history.
      // A site must be named first -- the server refuses an unnamed save in its
      // own words.
      e.site.addEventListener('keydown', (ev) => {
        if (ev.key === 'Enter' && e.site.value.trim())
          send('survey site ' + e.site.value.trim());
      });
      e.antenna.addEventListener('keydown', (ev) => {
        if (ev.key === 'Enter' && e.antenna.value.trim())
          send('survey antenna ' + e.antenna.value.trim());
      });
      e.save.addEventListener('click', () => {
        // Commit a site typed but not yet entered, so Save does not refuse what
        // the operator has clearly just named.
        if (e.site.value.trim()) send('survey site ' + e.site.value.trim());
        if (e.antenna.value.trim())
          send('survey antenna ' + e.antenna.value.trim());
        send('survey save');
      });
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
      + '<select id="survey-band" style="background:#16222e;color:' + VALUE
      + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;padding:3px 6px;'
      + 'cursor:pointer"><option>Band…</option></select>'
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
      // A second row for the installation and Save -- the window's site/antenna
      // combos and Save survey button. A sweep is refused a save until the site
      // is named, so levels from two sweeps are only ever compared within one.
      '<div style="display:flex;gap:10px;align-items:center;flex-wrap:wrap;'
      + 'flex:0 0 auto;margin-bottom:6px">'
      + '<label style="color:' + LABEL + '">site <input id="survey-site" '
      + 'size="14" placeholder="name this site" style="background:#16222e;color:'
      + VALUE + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;'
      + 'padding:3px 6px"></label>'
      + '<label style="color:' + LABEL + '">antenna <input id="survey-antenna" '
      + 'size="12" style="background:#16222e;color:' + VALUE
      + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;padding:3px 6px">'
      + '</label>'
      + '<button id="survey-save" style="background:#1b2836;color:' + CAPTION
      + ';border:1px solid ' + PANEL_EDGE + ';padding:4px 12px;font:inherit;'
      + 'cursor:pointer">Save survey</button>'
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
