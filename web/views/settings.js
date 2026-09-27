// The Settings view (web-visualization/17): the staged set beside what is
// applied, and the commands that change them -- wrapped in an IIFE so
// `SettingsView` is the only name this file adds to the shared global scope.
//
// This is the first view that *writes*. It does so through named commands
// (`set <field> <value>`, `apply`), which is what `tune` and `view` already
// are -- not by reproducing the window's clicks. The window's own panel
// stages into `app->set.*` and commits with one function, so there is no
// interaction here to reproduce.
const SettingsView = (function () {
  let els = null;

  function elements() {
    if (!els) {
      els = {
        head: document.getElementById('set-head'),
        ppm: document.getElementById('set-ppm'),
        gain: document.getElementById('set-gain'),
        fft: document.getElementById('set-fft'),
        dc: document.getElementById('set-dc'),
        drift: document.getElementById('set-drift'),
        rows: document.getElementById('set-rows'),
        error: document.getElementById('set-error'),
        apply: document.getElementById('set-apply'),
      };
    }
    return els;
  }

  // overlay_settings.c's own Color constants, as the hex of their exact RGB.
  const PANEL_FILL = '#0c141d';    /* 12, 20, 29 */
  const PANEL_EDGE = '#6f8b9a';    /* 111, 139, 154 */
  const CAPTION = '#ebf2f6';       /* 235, 242, 246 */
  const LABEL = '#a6bcc9';         /* 166, 188, 201 */
  const VALUE = '#ebf2f6';
  const MUTED = '#7e97a6';
  const DIRTY = '#ffca69';
  const ERROR_COLOR = '#ff6964';   /* 255, 105, 100 */

  // Whichever settings_state arrived last, so a control knows what it is
  // stepping from. The server owns the staged set; this never guesses at it.
  let last = null;

  // The transform sizes the Settings panel steps through, which are
  // `sdr_dsp_fft_choice()`'s own. Listed here only as the *stepper's* axis --
  // the server refuses a size it does not offer, and says so.
  const FFT_SIZES = [256, 512, 1024, 2048, 4096, 8192, 16384];

  function send(line) {
    // viewer.js owns the socket; a view never opens one (ADR-0007's rule
    // restated for JavaScript, in the web-view skill).
    if (typeof sendCommand === 'function') sendCommand(line);
  }

  function step(list, current, by) {
    const i = list.indexOf(current);
    const next = i < 0 ? 0 : Math.min(list.length - 1, Math.max(0, i + by));
    return list[next];
  }

  function row(label, staged, applied, dirty) {
    return [
      '<td style="color:' + LABEL + '">' + label + '</td>',
      '<td style="color:' + (dirty ? DIRTY : VALUE) + '">' + staged + '</td>',
      '<td style="color:' + MUTED + '">' + applied + '</td>',
    ];
  }

  function renderState(s) {
    const e = elements();
    last = s;

    // Both columns, always. The window shows the difference by having a text
    // field in front of the reader; a second reader with no field of their
    // own would be told one number and have no way to know which.
    e.head.textContent = 'Acquisition settings'
      + (s.dirty ? '   — staged changes, not applied' : '   — applied');
    e.head.style.color = s.dirty ? DIRTY : CAPTION;

    const st = s.staged, ap = s.applied;
    renderRows(e.rows, [
      row('PPM (tuning correction)', st.ppm, ap.ppm,
          String(st.ppm) !== String(ap.ppm)),
      row('Gain', st.gain, ap.gain, st.gain !== ap.gain),
      row('Scope resolution', st.fft, ap.fft_size + ' points',
          st.fft_size !== ap.fft_size),
      row('Remove DC spike', st.remove_dc ? 'on' : 'off',
          ap.remove_dc ? 'on' : 'off', st.remove_dc !== ap.remove_dc),
      row('Auto GSM drift check', st.auto_drift ? 'on' : 'off',
          ap.auto_drift ? 'on' : 'off', st.auto_drift !== ap.auto_drift),
    ]);

    e.ppm.value = st.ppm;
    e.dc.checked = !!st.remove_dc;
    e.drift.checked = !!st.auto_drift;
    // A capture has a gain baked in, which is a different answer from
    // "automatic" -- the server decides which (`gain_adjustable`).
    e.gain.disabled = !s.gain_adjustable;
    e.apply.disabled = !s.dirty;
    e.apply.style.opacity = s.dirty ? '1' : '0.45';

    e.error.textContent = s.error || '';
    e.error.style.color = ERROR_COLOR;
  }

  function button(id, label) {
    return '<button id="' + id + '" style="background:#1b2836;color:'
      + CAPTION + ';border:1px solid ' + PANEL_EDGE
      + ';padding:4px 10px;font:inherit;cursor:pointer">' + label
      + '</button>';
  }

  return {
    id: 'settings',
    label: 'Settings',
    streams: ['settings_state'],
    resize() { /* no canvas: nothing to fit */ },
    mounted() {
      const e = elements();

      // Enter commits the field, the way the window's does; a keystroke does
      // not, because `set` is a command and not a text field.
      e.ppm.addEventListener('keydown', (ev) => {
        if (ev.key === 'Enter') send('set ppm ' + e.ppm.value.trim());
      });
      e.dc.addEventListener('change',
        () => send('set dc ' + (e.dc.checked ? 'on' : 'off')));
      e.drift.addEventListener('change',
        () => send('set drift ' + (e.drift.checked ? 'on' : 'off')));
      document.getElementById('set-gain-down').addEventListener('click',
        () => last && send('set gain '
          + Math.max(0, last.staged.gain_choice - 1)));
      document.getElementById('set-gain-up').addEventListener('click',
        () => last && send('set gain '
          + Math.min(last.gain_options, last.staged.gain_choice + 1)));
      document.getElementById('set-fft-down').addEventListener('click',
        () => last && send('set fft '
          + step(FFT_SIZES, last.staged.fft_size, -1)));
      document.getElementById('set-fft-up').addEventListener('click',
        () => last && send('set fft '
          + step(FFT_SIZES, last.staged.fft_size, +1)));
      // Apply is its own command, deliberately: one step of a stepper must
      // not restart acquisition, and the server validates the staged set as
      // a whole.
      e.apply.addEventListener('click', () => send('apply'));
    },
    markup:
      '<div style="flex:0 0 auto;background:' + PANEL_FILL + ';border:1px solid '
      + PANEL_EDGE + ';padding:14px 16px 16px;max-width:760px">'
      + '<div id="set-head" style="font-size:17px;margin-bottom:12px;color:'
      + CAPTION + '">awaiting settings_state...</div>'
      + '<table style="margin-bottom:14px"><thead><tr>'
      + '<th></th><th>STAGED</th><th>APPLIED</th></tr></thead>'
      + '<tbody id="set-rows"></tbody></table>'
      + '<div style="display:flex;gap:14px;align-items:center;flex-wrap:wrap">'
      + '<label style="color:' + LABEL + '">PPM '
      + '<input id="set-ppm" size="6" style="background:#16222e;color:'
      + VALUE + ';border:1px solid ' + PANEL_EDGE + ';font:inherit;'
      + 'padding:3px 6px"></label>'
      + '<span style="color:' + LABEL + '">Gain</span>'
      + button('set-gain-down', '&lt;') + button('set-gain-up', '&gt;')
      + '<span id="set-gain"></span>'
      + '<span style="color:' + LABEL + '">Resolution</span>'
      + button('set-fft-down', '&lt;') + button('set-fft-up', '&gt;')
      + '<span id="set-fft"></span>'
      + '</div>'
      + '<div style="display:flex;gap:18px;margin-top:12px;flex-wrap:wrap">'
      + '<label style="color:' + LABEL + '"><input type="checkbox" '
      + 'id="set-dc"> Remove DC spike from spectrum and waterfall</label>'
      + '<label style="color:' + LABEL + '"><input type="checkbox" '
      + 'id="set-drift"> Auto GSM drift check</label>'
      + '</div>'
      + '<div id="set-error" style="font-size:13px;margin-top:12px;'
      + 'min-height:1.3em"></div>'
      + '<div style="margin-top:10px">' + button('set-apply', 'Apply')
      + '</div>'
      + '</div>',
    render(msg) {
      if (msg.kind === 'state' && msg.state.type === 'settings_state')
        renderState(msg.state);
    },
  };
})();
