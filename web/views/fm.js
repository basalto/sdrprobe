// The FM view (ticket 14, Phase 4 -- the first of ticket 07's remaining
// views to land as a module rather than as more lines in the shell).
// `markup`, `streams`, `render(msg)` and nothing else, wrapped in an IIFE
// so `FmView` is the only name this file adds to the shared global scope
// every concatenated file runs in.
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
      const mpx = document.getElementById('fm-mpx');
      els = {
        mpx, mpxCtx: mpx.getContext('2d'),
        reading: document.getElementById('fm-reading'),
        signal: document.getElementById('fm-signal-rows'),
        station: document.getElementById('fm-station-rows'),
        funnel: document.getElementById('fm-funnel-rows'),
      };
    }
    return els;
  }

  // `enum fm_reading_tone`'s three values, in its own order: neutral (in
  // progress), good (working), weak (this is where the decode stopped).
  // Inline rather than as classes in viewer.html, deliberately: ticket
  // 14's own criterion is that adding a view touches views/ and the
  // registry line and no other file, and views/survey.js already picks
  // its chart colours the same way.
  const TONE_COLOR = ['#8291a0', '#5adca4', '#e8a355'];

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

  function pair(label, value) {
    return ['<td>' + label + '</td>', '<td>' + value + '</td>'];
  }

  function renderState(s) {
    const e = elements();

    e.reading.textContent = s.reading;
    e.reading.style.color = TONE_COLOR[s.reading_tone] || TONE_COLOR[0];

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

  return {
    id: 'fm',
    label: 'FM',
    // FM is not a tab. It is TAB_DECODE (input_route.h's enum active_tab)
    // with DECODE_FM chosen (app.h's enum decode_kind), which is why the
    // shell matches on both and `receiver_state` carries both.
    tab: 2,
    decode: 0,
    streams: ['fm_spectrum', 'fm_state'],
    markup:
      '<div class="label">multiplex (the pilot at 19 kHz, stereo at 38, RDS at 57 --'
      + ' a station with the first two and not the third sends no RDS)</div>' +
      '<canvas id="fm-mpx" width="900" height="220"></canvas>' +
      '<div class="label">where the decode stopped</div>' +
      '<div id="fm-reading">awaiting fm_state...</div>' +
      '<table><tbody id="fm-funnel-rows"></tbody></table>' +
      '<div class="label">signal</div>' +
      '<table><tbody id="fm-signal-rows"></tbody></table>' +
      '<div class="label">station</div>' +
      '<table><tbody id="fm-station-rows"></tbody></table>',
    render(msg) {
      if (msg.kind === 'fm_spectrum') {
        drawMultiplex(msg.lowerHz, msg.upperHz, msg.power);
      } else if (msg.kind === 'state' && msg.state.type === 'fm_state') {
        renderState(msg.state);
      }
    },
  };
})();
