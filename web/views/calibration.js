// The Calibration view (web-visualization/17): the staged reference, the
// residual buffer filling, the verdict, and what the two references make of
// each other -- wrapped in an IIFE so `CalibrationView` is the only name
// this file adds to the shared global scope.
//
// It can *start* a measurement and stop one. It cannot apply the result:
// a calibration writes a standing fact about this receiver at this site
// (ADR-0018, ADR-0022), so applying is `set ppm` plus `apply` on the
// Settings tab -- one more deliberate act.
const CalibrationView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      els = {
        head: document.getElementById('cal-head'),
        status: document.getElementById('cal-status'),
        rows: document.getElementById('cal-rows'),
        refs: document.getElementById('cal-refs'),
        health: document.getElementById('cal-health'),
        notice: document.getElementById('cal-notice'),
        stop: document.getElementById('cal-stop'),
      };
    }
    return els;
  }

  // overlay_calibration.c's own colours, as the hex of their exact RGB.
  const PANEL_FILL = '#0c141d';
  const PANEL_EDGE = '#6f8b9a';
  const CAPTION = '#ebf2f6';
  const LABEL = '#a6bcc9';
  const VALUE = '#ebf2f6';
  const MUTED = '#7e97a6';
  const LOCKED = '#63e4aa';
  const WAITING = '#fabe4a';

  // The health indicator's own four states, arriving by name. Nothing here
  // dismisses itself on a timer: a correction is a standing fact about the
  // receiver, and a surface that erased itself could not answer "what am I
  // corrected by?" a minute later.
  const HEALTH_COLOR = {
    good: LOCKED, drifting: '#ff6964', checking: WAITING, unknown: MUTED,
  };

  // Why it has not locked, in words. The *names* are the server's -- one
  // per clause of the gate, in the order the gate ands them, so the one
  // shown is the reason that will clear first. Only the wording is here.
  const UNMET_TEXT = {
    'too-soon': 'listening; the gate wants a few more seconds',
    'too-few-measurements': 'too few readings so far',
    'too-few-residuals': 'the residual buffer is still filling',
    'scatter-too-wide': 'the scatter is too wide to mean anything yet '
      + '— this one will not clear by waiting',
    'weak-cell': 'the cell search barely found a cell',
    'weak-carrier': 'the carrier barely stands clear of its floor',
  };

  let last = null;

  function send(line) {
    if (typeof sendCommand === 'function') sendCommand(line);
  }

  function row(label, value, color) {
    return [
      '<td style="color:' + LABEL + '">' + label + '</td>',
      '<td style="color:' + (color || VALUE) + '">' + value + '</td>',
    ];
  }

  function signed(v, digits) {
    return (v >= 0 ? '+' : '') + v.toFixed(digits === undefined ? 2 : digits);
  }

  function renderState(s) {
    const e = elements();
    last = s;

    e.head.textContent = s.locked
      ? 'Locked — ' + signed(s.centre_ppm, 2) + ' ppm suggested'
      : (s.running ? 'Measuring…' : 'Not measuring');
    e.head.style.color = s.locked ? LOCKED : (s.running ? WAITING : MUTED);

    // The overlay's own status line, verbatim, so a reader at the browser
    // and a reader at the window see the same sentence.
    e.status.textContent = s.status || '';

    renderRows(e.rows, [
      // Both numbers, because the sequence is what shows whether the
      // scatter is the estimator or the crystal -- which is why
      // `--calibrate` prints every residual rather than a summary.
      row('This block', signed(s.observed_ppm) + ' ppm'),
      row('Median of the run', signed(s.centre_ppm) + ' ppm'),
      row('Standard error', s.sem_ppm.toFixed(2) + ' ppm'),
      row('Spread', s.spread_ppm.toFixed(2) + ' ppm'),
      // ADR-0004: a buffer mixing two sources passes the gate while
      // suggesting a correction belonging to neither, so what measured
      // these is not a detail.
      row('Measured by', s.source),
      row('Readings', s.residuals + ' of ' + s.measurements + ' blocks'),
      row('Verdict', s.locked
        ? 'locked'
        : (UNMET_TEXT[s.unmet] || s.unmet || 'not measuring'),
        s.locked ? LOCKED : WAITING),
      row('Suggests', signed(s.suggested_ppm, 0) + ' ppm', MUTED),
      row('Applied now', signed(s.applied_ppm, 0) + ' ppm', MUTED),
    ]);

    // Two references measuring one crystal: when they agree the correction
    // is worth trusting, and when they do not that is the most useful thing
    // either of them has said. One that has measured is *not* a
    // disagreement, which is why `have_both` is its own field.
    const refs = [];
    if (s.gsm.valid)
      refs.push('GSM ARFCN ' + s.gsm.arfcn + ': ' + signed(s.gsm.ppm, 0)
        + ' ppm');
    if (s.lte.valid)
      refs.push('LTE EARFCN ' + s.lte.earfcn + ': ' + signed(s.lte.ppm, 0)
        + ' ppm');
    if (s.have_both)
      refs.push(s.apart_ppm <= 2
        ? '— they agree to ' + s.apart_ppm.toFixed(1) + ' ppm'
        : '— they disagree by ' + s.apart_ppm.toFixed(1)
          + ' ppm, which is the most useful thing either has said');
    e.refs.textContent = refs.length ? refs.join('   ')
      : 'no reference has measured this receiver yet';

    e.health.textContent = 'correction health: ' + s.health;
    e.health.style.color = HEALTH_COLOR[s.health] || MUTED;
    e.notice.textContent = s.notice || '';

    e.stop.disabled = !s.running;
    e.stop.style.opacity = s.running ? '1' : '0.45';
  }

  function button(id, label) {
    return '<button id="' + id + '" style="background:#1b2836;color:'
      + CAPTION + ';border:1px solid ' + PANEL_EDGE
      + ';padding:4px 10px;font:inherit;cursor:pointer">' + label
      + '</button>';
  }

  return {
    id: 'calibration',
    label: 'Calibration',
    streams: ['cal_state'],
    resize() { /* no canvas: nothing to fit */ },
    mounted() {
      document.getElementById('cal-gsm').addEventListener('click',
        () => send('calibrate gsm'));
      document.getElementById('cal-lte').addEventListener('click',
        () => send('calibrate lte'));
      elements().stop.addEventListener('click',
        () => send('calibrate stop'));
      // Applying is deliberately not here: it is `set ppm` then `apply` on
      // the Settings tab, and the button says where rather than doing it.
      document.getElementById('cal-apply-hint').addEventListener('click',
        () => last && send('set ppm ' + last.suggested_ppm));
    },
    markup:
      '<div style="flex:0 0 auto;background:' + PANEL_FILL + ';border:1px solid '
      + PANEL_EDGE + ';padding:14px 16px 16px;max-width:760px">'
      + '<div id="cal-head" style="font-size:17px;margin-bottom:4px;color:'
      + CAPTION + '">awaiting cal_state...</div>'
      + '<div id="cal-status" style="font-size:13px;margin-bottom:12px;'
      + 'color:' + MUTED + ';min-height:1.3em"></div>'
      + '<div style="display:flex;gap:10px;margin-bottom:14px">'
      + button('cal-gsm', 'Calibrate against GSM')
      + button('cal-lte', 'Calibrate against LTE')
      + button('cal-stop', 'Stop')
      + '</div>'
      + '<table style="margin-bottom:12px">'
      + '<tbody id="cal-rows"></tbody></table>'
      + '<div id="cal-refs" style="font-size:13px;color:' + LABEL + ';'
      + 'margin-bottom:10px"></div>'
      + '<div style="margin-bottom:10px">'
      + button('cal-apply-hint', 'Stage the suggestion')
      + '<span style="color:' + MUTED + ';font-size:12px;margin-left:10px">'
      + 'then press Apply on the Settings tab — a calibration is a '
      + 'standing fact about this receiver, so applying it is its own act'
      + '</span></div>'
      + '<div id="cal-health" style="font-size:13px"></div>'
      + '<div id="cal-notice" style="font-size:12px;color:' + MUTED + ';'
      + 'min-height:1.2em"></div>'
      + '</div>',
    render(msg) {
      if (msg.kind === 'state' && msg.state.type === 'cal_state')
        renderState(msg.state);
    },
  };
})();
