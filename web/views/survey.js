// The Survey view (ticket 14, Phase 3's registry shape): `markup`,
// `streams`, `render(msg)` and nothing else -- wrapped in an IIFE so
// `SurveyView` is the only name this file adds to the shared global
// scope (see views/scope.js's comment for why). `lastCandidates` is
// whichever `survey_state` arrived last -- a candidate's mark is decided
// once, server-side (sdrgui_survey_peak_mark()), and this file only
// draws it, never recomputing anything the server already decided.
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

  // `mark` is sdrgui_survey_peak_mark()'s own enum, named here rather
  // than reinterpreting the flag word the server already decided from --
  // 0 signal, 1 empty, 2 receiver-like, 3 contested (sdrgui.h).
  const MARK_CLASS = ['mark-signal', 'mark-empty', 'mark-receiver', 'mark-contested'];
  const MARK_GLYPH = ['●', '○', '✕', '✕'];
  let lastCandidates = [];

  function renderSurveyState(state) {
    const { surveyStatus, surveyCount, surveyRows } = elements();
    surveyStatus.textContent = state.status;
    surveyCount.textContent = state.candidate_count;
    lastCandidates = state.candidates || [];
    renderRows(surveyRows, lastCandidates.map((c) => {
      const cls = MARK_CLASS[c.mark] || 'mark-signal';
      const glyph = MARK_GLYPH[c.mark] || '?';
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

  return {
    id: 'survey',
    label: 'Survey',
    tab: 0, // TAB_SURVEY, input_route.h's enum active_tab
    streams: ['survey_spectrum', 'survey_state'],
    markup:
      '<div class="label" id="survey-status">no sweep yet</div>' +
      '<canvas id="survey-chart" width="900" height="260"></canvas>' +
      '<div class="label">candidates (<span id="survey-count">0</span>)</div>' +
      '<table>' +
      '<thead><tr><th></th><th>frequency</th><th>level</th><th>width</th><th>shape</th></tr></thead>' +
      '<tbody id="survey-rows"></tbody>' +
      '</table>',
    render(msg) {
      if (msg.kind === 'survey_spectrum') drawSurveyChart(msg.lowerHz, msg.upperHz, msg.power);
      else if (msg.kind === 'state' && msg.state.type === 'survey_state') renderSurveyState(msg.state);
    },
  };
})();
