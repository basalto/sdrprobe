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
      };
      els.surveyCtx = els.surveyChart.getContext('2d');
    }
    return els;
  }

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

  function renderSurveyState(state) {
    const { surveyStatus, surveyCount, surveyRows } = elements();
    surveyStatus.textContent = state.status;
    surveyCount.textContent = state.candidate_count;
    lastCandidates = state.candidates || [];
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
    markup:
      '<div class="label" id="survey-status">no sweep yet</div>' +
      '<canvas id="survey-chart" style="flex:2 1 0;min-height:120px;width:100%">'
      + '</canvas>' +
      '<div class="label">candidates (<span id="survey-count">0</span>)</div>' +
      '<div style="flex:1 1 0;min-height:0;overflow:auto">' +
      '<table>' +
      '<thead><tr><th></th><th>frequency</th><th>level</th><th>width</th><th>shape</th></tr></thead>' +
      '<tbody id="survey-rows"></tbody>' +
      '</table></div>',
    render(msg) {
      if (msg.kind === 'survey_spectrum') drawSurveyChart(msg.lowerHz, msg.upperHz, msg.power);
      else if (msg.kind === 'state' && msg.state.type === 'survey_state') renderSurveyState(msg.state);
    },
  };
})();
