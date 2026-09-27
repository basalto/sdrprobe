// The FM view (ticket 14, Phase 4 -- the first of ticket 07's remaining
// views to land as a module rather than as more lines in the shell).
// `markup`, `streams`, `render(msg)` and nothing else, wrapped in an IIFE
// so `FmView` is the only name this file adds to the shared global scope
// every concatenated file runs in.
//
// The arrangement mirrors `src/view_fm.c`'s own: a waterfall across the
// received span, then Signal, Station and "Where the decode stopped" side
// by side in that order, with the multiplex behind a toggle the way the
// window puts it behind "Show charts". A reader who knows one screen
// should not have to learn the other -- ADR-0027 keeps the window primary,
// which makes it the thing this is a view *of*.
//
// Nothing here re-decides anything. The funnel's closing sentence and its
// emphasis arrive already chosen (`fm_view_model.c`, `enum
// fm_reading_tone`); this file picks a colour for a verdict it was given,
// which is the same division `views/survey.js` keeps with a candidate's
// mark. A browser reaching its own conclusion from the five counts beside
// it would be the second presentation this whole seam exists to prevent.
const FmView = (function () {
  let els = null;
  function elements() {
    if (!els) {
      const wf = document.getElementById('fm-waterfall');
      const mpx = document.getElementById('fm-mpx');
      els = {
        wf, wfCtx: wf.getContext('2d'),
        mpx, mpxCtx: mpx.getContext('2d'),
        wfWrap: document.getElementById('fm-waterfall-wrap'),
        mpxWrap: document.getElementById('fm-mpx-wrap'),
        axis: document.getElementById('fm-axis'),
        charts: document.getElementById('fm-charts'),
        reading: document.getElementById('fm-reading'),
        signal: document.getElementById('fm-signal-rows'),
        station: document.getElementById('fm-station-rows'),
        funnel: document.getElementById('fm-funnel-rows'),
      };
      // Wired here rather than at load: `markup` is not in the document
      // until viewer.js's mountViews() inserts it, which happens after
      // every view file has already run.
      els.charts.onclick = () => showCharts(!charting);
    }
    return els;
  }

  // The window's "Show charts" toggle, which swaps its waterfall for the
  // analysis arrangement. Here it swaps the waterfall for the multiplex,
  // which is the one chart of that arrangement a browser has data for.
  // `hidden` alone is not enough here and that is worth saying, because it
  // silently was not: both wrappers carry an inline `display:flex` so their
  // canvas can take the room left in the column, and an inline `display`
  // beats the `hidden` attribute's own UA rule. The wrapper stayed laid out
  // -- an empty chart quietly eating 136 px of the waterfall's height,
  // which no fake DOM can show and a real browser reported at once. So the
  // display is set alongside the attribute, not instead of it: `hidden`
  // stays for what it means to a reader and to anything asking.
  let charting = false;
  function showCharts(on) {
    const e = elements();
    charting = on;
    e.wfWrap.hidden = on;
    e.wfWrap.style.display = on ? 'none' : 'flex';
    e.mpxWrap.hidden = !on;
    e.mpxWrap.style.display = on ? 'flex' : 'none';
    e.charts.textContent = on ? 'Show signal' : 'Show charts';
    // The canvas coming into view was last sized against a column it was
    // not part of; give it the one it is in now.
    resizeCanvases();
  }

  // Matches each canvas's backing store to whatever CSS laid it out at --
  // the layout already decides how much room is left once the toolbar, the
  // axis and the panel row have taken theirs, and a second opinion about
  // that in arithmetic is how a page comes to disagree with itself by a
  // scrollbar's width.
  //
  // Resizing clears a canvas, so the waterfall is redrawn from the rows it
  // kept (lib/waterfall.js) -- only when the geometry actually changed,
  // which is what `fitCanvas()` reports. The multiplex needs no history:
  // the next `fm_spectrum` carries the whole trace, four times a second.
  //
  // The rows are still only ever the ones this page was sent: ADR-0027 has
  // the Viewer build its own history and never ask the program for one,
  // and that is unchanged. What changed is that a resize no longer throws
  // away the one it already built.
  function resizeCanvases() {
    const e = elements();
    const wfBox = measure(e.wf), mpxBox = measure(e.mpx);

    if (fitCanvas(e.wf, wfBox.width, wfBox.height))
      waterfallRedraw(e.wfCtx, e.wf, history);
    fitCanvas(e.mpx, mpxBox.width, mpxBox.height);
  }

  // The window's own panel palette, taken from `src/view_fm.c`'s file-scope
  // Colors rather than eyeballed: `panel_edge`, `panel_caption`,
  // `row_label`, `row_value`, `row_good` and `row_weak`, each as the hex of
  // the exact RGB it is there. A panel that is nearly the window's colour
  // is a panel a reader has to look twice at.
  const PANEL_FILL = '#111a25';    /* 17, 26, 37 */
  const PANEL_EDGE = '#526d7e';    /* 82, 109, 126 */
  const PANEL_CAPTION = '#97aebc'; /* 151, 174, 188 */
  const ROW_LABEL = '#7e97a6';     /* 126, 151, 166 */
  const ROW_VALUE = '#d5e2ea';     /* 213, 226, 234 */

  // `enum fm_reading_tone` by *name*, never by its integer -- neutral (in
  // progress), good (working), weak (this is where the decode stopped) --
  // painted in the window's own `row_label`, `row_good` and `row_weak`.
  // Keyed by name for the reason views/survey.js's marks are: an ordinal
  // re-declared in a second language is a table that can be silently wrong
  // in the wrong order, and that one was, for months.
  //
  // Inline rather than as classes in viewer.html, deliberately: ticket 14's
  // own criterion is that adding a view touches views/ and the registry
  // line and no other file.
  const TONE_COLOR = { neutral: ROW_LABEL, good: '#63e4aa', weak: '#fabe4a' };

  // The three landmarks that make a multiplex readable at a glance: the
  // pilot, the stereo subcarrier at twice it, and the RDS band at three
  // times it. A station with the first two and not the third is an
  // ordinary station simply not sending any RDS, which is the question
  // none of the panels below can answer (src/view_fm.c says the same).
  const LANDMARKS = [
    { hz: 19000, label: 'pilot' },
    { hz: 38000, label: 'stereo' },
    { hz: 57000, label: 'RDS' },
  ];

  // The window's two row colours: a muted label, a bright value.
  function pair(label, value) {
    return ['<td style="color:' + ROW_LABEL + '">' + label + '</td>',
            '<td style="color:' + ROW_VALUE + '">' + value + '</td>'];
  }

  function renderState(s) {
    const e = elements();

    // The window's Signal panel, row for row and in its order: whether
    // anything is being received first, how well underneath.
    const signal = [pair('pilot', s.pilot_locked ? 'locked' : 'no lock')];
    // The sound is the server's, not this browser's: a Viewer cannot hear
    // it, so what is reported is whether that machine is playing, never a
    // control this page could not work.
    if (s.audio_error) signal.push(pair('audio', s.audio_error));
    else if (s.playing) {
      signal.push(pair('audio', s.audio_rate_hz.toFixed(0) + ' Hz '
        + (s.broadcast_stereo ? 'stereo' : 'mono') + ' (at the receiver)'));
    }
    signal.push(pair('broadcast', s.broadcast_stereo ? 'stereo' : 'mono'));
    if (s.pilot_locked) {
      signal.push(pair('at', s.pilot_hz.toFixed(2) + ' Hz'));
      // The transmitter's pilot offset, not this receiver's clock -- five
      // stations read between +2 and -57 ppm on one receiver.
      signal.push(pair('pilot offset', (s.pilot_ppm >= 0 ? '+' : '')
        + s.pilot_ppm.toFixed(1) + ' ppm'));
    }
    signal.push(pair('coherence', s.pilot_coherence.toFixed(2)));
    if (s.pilot_locked) {
      signal.push(pair('symbol timing',
        s.timing_offset + '/' + s.timing_samples_per_symbol));
      signal.push(pair('subcarrier axis',
        (s.axis_radians >= 0 ? '+' : '') + s.axis_radians.toFixed(2) + ' rad'));
    }
    renderRows(e.signal, signal);

    const station = [];
    if (s.pi_valid) {
      station.push(pair('identification',
        '0x' + s.pi.toString(16).toUpperCase().padStart(4, '0')));
      station.push(pair('', s.pi_repeats + ' agreeing'));
    } else {
      station.push(pair('identification', '--'));
    }
    // A name is shown only whole and repeated: it arrives two characters
    // at a time, and a half-arrived one puts a station that does not exist
    // on the screen. `ps_segments` is the count, decided server-side.
    if (s.ps_valid) station.push(pair('name', s.ps));
    else if (s.ps_segments) {
      station.push(pair('name', s.ps_segments + ' of 4 segments'));
    } else station.push(pair('name', '--'));
    if (s.pty_valid) {
      station.push(pair('programme type', s.pty_name));
      station.push(pair('', s.traffic));
    }
    // Unwrapped on the wire: where the breaks go is the reader's, and a
    // browser wraps to its own width rather than the window's columns.
    if (s.rt_valid) station.push(pair('radio text', s.rt));
    renderRows(e.station, station);

    renderRows(e.funnel, [
      pair('soft bits', s.bits),
      pair('blocks', s.blocks_matched),
      pair('groups', s.groups),
      pair('identified', s.identified),
      pair('named', s.named),
    ]);
    e.reading.textContent = s.reading;
    e.reading.style.color = TONE_COLOR[s.reading_tone] || TONE_COLOR.neutral;
  }

  // The same waterfall the window draws over the received span, through
  // the same lib/waterfall.js the Scope's goes through -- one scroll and
  // one row of pixels as each arrives, and the rows kept beside it so a
  // resize redraws rather than blanks.
  const history = createWaterfall();

  function drawWaterfall(row) {
    const e = elements();
    waterfallPush(history, row);
    waterfallDrawNewest(e.wfCtx, e.wf, row);
  }

  // The span under the waterfall, from whatever `receiver_state` last
  // said. A waterfall with no frequencies on it is a picture; the window
  // labels its axis and so does this.
  function renderAxis(state) {
    const lower = (state.center_hz - state.sample_rate_hz / 2) / 1e6;
    const upper = (state.center_hz + state.sample_rate_hz / 2) / 1e6;
    elements().axis.textContent =
      lower.toFixed(3) + ' MHz' + ' — ' + upper.toFixed(3)
      + ' MHz (newest at top)';
  }

  function drawMultiplex(lowerHz, upperHz, power) {
    const { mpx, mpxCtx } = elements();
    const w = mpx.width, h = mpx.height;
    const span = upperHz - lowerHz;

    mpxCtx.fillStyle = '#0a0f16';
    mpxCtx.fillRect(0, 0, w, h);
    if (span > 0) {
      mpxCtx.font = '12px monospace';
      for (const m of LANDMARKS) {
        const x = Math.round(w * (m.hz - lowerHz) / span);
        if (x < 0 || x > w) continue;
        mpxCtx.strokeStyle = '#232f3b';
        mpxCtx.beginPath();
        mpxCtx.moveTo(x + 0.5, 0);
        mpxCtx.lineTo(x + 0.5, h);
        mpxCtx.stroke();
        mpxCtx.fillStyle = '#8291a0';
        mpxCtx.fillText(m.label, x + 3, 12);
      }
    }
    plot(mpxCtx, w, h, power, '#5adcc8', dbfsToY);
    if (span > 0) {
      mpxCtx.fillStyle = '#8291a0';
      mpxCtx.fillText((upperHz / 1e3).toFixed(0) + ' kHz', w - 60, h - 4);
    }
  }

  // One panel of the three-across row the window draws, with the window's
  // own fill, 1px edge and caption -- `draw_panel()` in src/view_fm.c, whose
  // caption sits 12 px in and 10 down and whose rows begin 36 from the top,
  // which is what the padding below reproduces. Laid out with inline styles
  // rather than rules in viewer.html for the same reason the tone colours
  // are inline: adding a view should not edit the shell.
  // `flex:1 1 0` with `min-width:0` rather than a pixel basis: all three
  // then divide the row evenly whatever they contain, so the borders line
  // up on both axes. The row stretches them to a common height (flex's
  // default `align-items:stretch`, which this used to override with
  // `flex-start` and so drew three boxes of three different depths), and
  // each scrolls inside its own border rather than growing the page --
  // radio text is four lines on one station and nothing on the next.
  function panel(caption, bodyHtml) {
    return '<div style="flex:1 1 0;min-width:0;background:'
      + PANEL_FILL + ';border:1px solid ' + PANEL_EDGE
      + ';padding:10px 12px 12px;overflow:auto">'
      + '<div style="color:' + PANEL_CAPTION
      + ';font-size:16px;margin-bottom:10px">' + caption + '</div>'
      + bodyHtml + '</div>';
  }

  return {
    id: 'fm',
    label: 'FM',
    // `waterfall` is the Scope's own stream, and the window's FM screen
    // draws the very same rows over the very same span -- one stream, two
    // views, rather than an `fm_waterfall` that would carry identical
    // bytes under another name.
    streams: ['fm_spectrum', 'fm_state', 'waterfall'],
    // The window gives its waterfall the whole width and the room left
    // above the panels; this takes the width the shell measured and about
    // two fifths of the viewport, with a floor so a short window still
    // shows a band of it rather than a line.
    //
    resize: resizeCanvases,
    // A column: the toolbar and the panel row take what they need, and
    // whichever chart is showing takes everything left. `min-height:0` on
    // each wrapper is what lets it be shorter than its own content rather
    // than pushing the page past the viewport and bringing the scrollbar
    // back.
    markup:
      '<div style="margin-bottom:10px;flex:0 0 auto">'
      + '<button id="fm-charts" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer">Show charts</button></div>' +
      '<div id="fm-waterfall-wrap" style="flex:1 1 auto;min-height:140px;'
      + 'display:flex;flex-direction:column">' +
        '<canvas id="fm-waterfall" style="flex:1 1 auto;min-height:0;'
        + 'width:100%"></canvas>' +
        '<div class="label" id="fm-axis">awaiting receiver_state...</div>' +
      '</div>' +
      '<div id="fm-mpx-wrap" hidden style="flex:1 1 auto;min-height:140px;'
      + 'display:none;flex-direction:column">' +
        '<div class="label">multiplex (the pilot at 19 kHz, stereo at 38,'
        + ' RDS at 57 -- a station with the first two and not the third'
        + ' sends no RDS)</div>' +
        '<canvas id="fm-mpx" style="flex:1 1 auto;min-height:0;'
        + 'width:100%"></canvas>' +
      '</div>' +
      '<div style="display:flex;gap:16px;margin-top:10px;flex:0 1 auto;'
      + 'min-height:0">' +
        panel('Signal', '<table><tbody id="fm-signal-rows"></tbody></table>') +
        panel('Station', '<table><tbody id="fm-station-rows"></tbody></table>') +
        panel('Where the decode stopped',
              '<table><tbody id="fm-funnel-rows"></tbody></table>'
              + '<div id="fm-reading" style="margin-top:8px">'
              + 'awaiting fm_state...</div>') +
      '</div>',
    render(msg) {
      if (msg.kind === 'waterfall_row') {
        if (!charting) drawWaterfall(msg.row);
      } else if (msg.kind === 'fm_spectrum') {
        if (charting) drawMultiplex(msg.lowerHz, msg.upperHz, msg.power);
      } else if (msg.kind === 'state') {
        if (msg.state.type === 'fm_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
