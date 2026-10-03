// The FM view (ticket 14, Phase 4 -- the first of ticket 07's remaining
// views to land as a module rather than as more lines in the shell).
// `markup`, `streams`, `render(msg)` and nothing else, wrapped in an IIFE
// so `FmView` is the only name this file adds to the shared global scope
// every concatenated file runs in.
//
// The arrangement mirrors `src/gui/view_fm.c`'s own: a waterfall across the
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
      // One {canvas, ctx} per analysis chart, by id.
      const cv = (id) => {
        const c = document.getElementById(id);
        return { c: c, x: c.getContext('2d') };
      };
      els = {
        wf, wfCtx: wf.getContext('2d'),
        wfWrap: document.getElementById('fm-waterfall-wrap'),
        // "Show charts" swaps the whole main area -- the Band II table and the
        // waterfall together -- for the six-chart grid, the way the window's
        // analysis arrangement takes the full area rather than a corner of it.
        main: document.getElementById('fm-main'),
        grid: document.getElementById('fm-charts-grid'),
        cc: {
          mpx: cv('fm-c-mpx'), wave: cv('fm-c-wave'),
          aspec: cv('fm-c-aspec'), scatter: cv('fm-c-scatter'),
          timing: cv('fm-c-timing'), groups: cv('fm-c-groups'),
        },
        timingCap: document.getElementById('fm-c-timing-cap'),
        markers: document.getElementById('fm-markers'),
        axis: document.getElementById('fm-axis'),
        charts: document.getElementById('fm-charts'),
        scan: document.getElementById('fm-scan'),
        bandiiCaption: document.getElementById('fm-bandii-caption'),
        bandiiStatus: document.getElementById('fm-bandii-status'),
        bandiiRows: document.getElementById('fm-bandii-rows'),
        bandiiEmpty: document.getElementById('fm-bandii-empty'),
        reading: document.getElementById('fm-reading'),
        signal: document.getElementById('fm-signal-rows'),
        station: document.getElementById('fm-station-rows'),
        funnel: document.getElementById('fm-funnel-rows'),
      };
      // Wired here rather than at load: `markup` is not in the document
      // until viewer.js's mountViews() inserts it, which happens after
      // every view file has already run.
      els.charts.onclick = () => showCharts(!charting);
      // The window's "Scan band" button, which the browser needs because in
      // `web` mode there is no window to press: the Band II table and the
      // waterfall marks are empty until a scan runs. It toggles on what the
      // last fm_state said is happening, so the same button stops a sweep.
      // `scan fm`/`scan stop` are commands like `tune` and `view` -- the
      // program starts and stops the sweep; this only asks.
      els.scan.onclick = () => sendCommand(scanning ? 'scan stop' : 'scan fm');
      // One handler for the whole table rather than one per row: a click on a
      // carrier tunes to it, the window's "click to listen". A Viewer cannot
      // hear the audio -- that is the server's -- but tuning moves the decode
      // and the highlight, which is the half a browser can do.
      //
      // Choosing a station stops the walk first, the way the window's own row
      // click does, and for its reason: the table fills *while the scan is
      // still visiting carriers*, so a sweep left running retunes to its next
      // step on the following block and tunes straight back off the station
      // just asked for -- which reads as the click doing nothing. `scan stop`
      // returns the receiver and then `tune` moves it to the chosen carrier;
      // the server reads the two in order on the one connection.
      els.bandiiRows.onclick = (ev) => {
        const tr = ev.target.closest('tr[data-hz]');
        if (!tr) return;
        if (scanning) sendCommand('scan stop');
        sendCommand('tune ' + tr.dataset.hz);
      };
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
  // What the last fm_state said the band walk is doing, so the Scan button
  // knows whether its next press starts or stops one.
  let scanning = false;
  // The received span and the tuned centre, from the last receiver_state --
  // the markers are positioned against the span, and both the list and the
  // marks highlight the carrier nearest the centre.
  let lastLowerHz = 0, lastUpperHz = 0, lastTunedHz = 0;
  // The carriers the last fm_state carried, kept so a receiver_state that
  // changes the span can re-place the marks without waiting for the next
  // fm_state.
  let lastStations = [];
  // What this view asks the server for. Two sets, swapped on the toggle: the
  // signal view wants the waterfall (and nothing else binary), the charts view
  // wants the three analysis streams and the multiplex -- and *not* the
  // waterfall, which is hidden then and is the page's largest stream, so the
  // swap is close to throughput-neutral. `fm_state` is in both, so the Band II
  // marks and the timing/groups charts (which ride it) are always fed.
  const SIGNAL_STREAMS = ['fm_state', 'waterfall'];
  const CHART_STREAMS = ['fm_state', 'fm_spectrum', 'fm_audio',
                         'fm_audio_spectrum', 'fm_scatter'];
  // The live array the shell reads (viewer.js subscribes `activeView.streams`).
  // Mutated in place so the reference the registry holds stays valid.
  const streams = SIGNAL_STREAMS.slice();

  function showCharts(on) {
    const e = elements();
    charting = on;
    e.main.hidden = on;
    e.main.style.display = on ? 'none' : 'flex';
    e.grid.hidden = !on;
    e.grid.style.display = on ? 'grid' : 'none';
    e.charts.textContent = on ? 'Show signal' : 'Show charts';

    // Ask the server for the set this view now needs, so the chart streams
    // cost nothing while the signal view is up and the waterfall costs nothing
    // while the charts are. `subscribeToActiveView` is viewer.js's and reads
    // `activeView.streams`, which is this same array.
    const want = on ? CHART_STREAMS : SIGNAL_STREAMS;
    streams.length = 0;
    for (const s of want) streams.push(s);
    if (typeof subscribeToActiveView === 'function') subscribeToActiveView();

    // The canvases coming into view were last sized against a layout they were
    // not part of; give them the one they are in now.
    resizeCanvases();
    // The marks belong to the waterfall, so they clear with the charts and
    // come back with the signal view.
    renderMarkers();
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
    const wfBox = measure(e.wf);

    if (fitCanvas(e.wf, wfBox.width, wfBox.height))
      waterfallRedraw(e.wfCtx, e.wf, history);
    // Each grid canvas to its own laid-out box. These carry no history of
    // their own -- the next frame of each stream redraws it whole -- so a
    // resize clearing them loses nothing.
    for (const key in e.cc) {
      const g = e.cc[key];
      const box = measure(g.c);
      fitCanvas(g.c, box.width, box.height);
    }
  }

  // The window's own panel palette, taken from `src/gui/view_fm.c`'s file-scope
  // Colors rather than eyeballed: `panel_edge`, `panel_caption`,
  // `row_label`, `row_value`, `row_good` and `row_weak`, each as the hex of
  // the exact RGB it is there. A panel that is nearly the window's colour
  // is a panel a reader has to look twice at.
  const PANEL_FILL = '#111a25';    /* 17, 26, 37 */
  const PANEL_EDGE = '#526d7e';    /* 82, 109, 126 */
  const PANEL_CAPTION = '#97aebc'; /* 151, 174, 188 */
  const ROW_LABEL = '#7e97a6';     /* 126, 151, 166 */
  const ROW_VALUE = '#d5e2ea';     /* 213, 226, 234 */
  const ROW_GOOD = '#63e4aa';      /* 99, 228, 170 -- a named station */
  const ROW_WEAK = '#fabe4a';      /* 250, 190, 74 */
  /* The window's waterfall-mark colour, {80, 220, 240}: one pill per found
     carrier at the top of the waterfall, where the newest row is. */
  const MARKER_COLOR = '#50dcf0';
  /* Within this of the tuned frequency, a carrier is the one being listened
     to -- the window's own 50 kHz, so the list and the marks highlight the
     same row it does. */
  const TUNED_NEAR_HZ = 50000;

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
  // none of the panels below can answer (src/gui/view_fm.c says the same).
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
    // Always these rows, with '--' where the field has not arrived -- not
    // pushed only when valid.
    //
    // A panel whose row *count* changes makes every chart above it change
    // height, and on a marginal signal `pty_valid` and `rt_valid` come and
    // go block to block. Measured over a capture that is not FM at all: the
    // waterfall's box oscillated between 465 and 477 pixels, one row's
    // worth, every couple of seconds -- visible as a jumping chart, and it
    // also meant the canvas was permanently one frame behind its own box.
    // Values change; rows do not.
    station.push(pair('programme type', s.pty_valid ? s.pty_name : '--'));
    station.push(pair('', s.pty_valid ? s.traffic : ''));
    // Unwrapped on the wire: where the breaks go is the reader's, and a
    // browser wraps to its own width rather than the window's columns.
    station.push(pair('radio text', s.rt_valid ? s.rt : '--'));
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

    // Band II: the carrier list, its one-line summary and the Scan button's
    // label -- all from this state, re-decided by nobody.
    scanning = !!s.scanning;
    e.scan.textContent = scanning ? 'Stop' : 'Scan band';
    e.bandiiCaption.textContent = scanning ? 'Scanning band II' : 'Band II';
    renderBandII(s);
    lastStations = s.stations || [];
    renderMarkers();

    // The two small analysis charts ride fm_state (sixteen numbers each), so
    // they are drawn from it here -- only while the charts are showing.
    if (charting) {
      drawTiming(s.timing_energy || [], s.timing_offset || 0);
      drawGroups(s.groups_by_type || []);
    }
  }

  // The Band II table, mirroring the window's draw_scan_list: a one-line
  // summary the scan decides, then a row per carrier -- MHz, level, whether
  // it is broadcasting (a pilot), whether RDS arrived, and its name. The
  // carrier nearest the tuning is the one being listened to, highlighted the
  // way the window highlights it, so choosing another is a move from
  // somewhere.
  function renderBandII(s) {
    const e = elements();
    const stations = s.stations || [];

    // The summary the scan wrote ("24 carriers, 18 in stereo, ..."), or the
    // window's own two prompts when there is nothing yet.
    e.bandiiStatus.textContent = s.scan_status
      || (s.scanning ? 'looking...' : 'press Scan band');

    if (!stations.length) {
      e.bandiiRows.innerHTML = '';
      e.bandiiEmpty.textContent = s.scanning ? 'looking...' : 'press Scan band';
      e.bandiiEmpty.style.display = '';
      return;
    }
    e.bandiiEmpty.style.display = 'none';

    let html = '';
    for (const st of stations) {
      const name = st.name
        || (st.pi_valid
          ? '0x' + st.pi.toString(16).toUpperCase().padStart(4, '0') : '--');
      // The window's own row colour: a named station bright, a stereo one
      // plain, an RDS-less mono one muted.
      const colour = st.name ? ROW_GOOD : st.stereo ? ROW_VALUE : ROW_LABEL;
      // The carrier being listened to now, so the list says where the
      // receiver is among what it found.
      const tuned = Math.abs(lastTunedHz - st.hz) < TUNED_NEAR_HZ;
      html += '<tr data-hz="' + Math.round(st.hz) + '" style="cursor:pointer;'
        + 'color:' + colour + (tuned ? ';background:#1b3a2e' : '') + '">'
        + '<td>' + (st.hz / 1e6).toFixed(1) + '</td>'
        + '<td>' + st.dbfs.toFixed(1) + ' dBFS</td>'
        + '<td>' + (st.stereo ? 'stereo' : 'mono') + '</td>'
        + '<td>' + (st.rds ? 'yes' : 'no') + '</td>'
        + '<td>' + name + '</td></tr>';
    }
    e.bandiiRows.innerHTML = html;
  }

  // The waterfall's station marks: one pill per found carrier at the top row,
  // positioned by frequency across the received span -- the window's own
  // marks, as HTML over the canvas rather than drawn into it, so the
  // scrolling picture underneath never erases them. A carrier outside the
  // current span is dropped, and the tuned one is brightened.
  function renderMarkers() {
    const e = elements();
    const span = lastUpperHz - lastLowerHz;
    if (span <= 0 || charting) { e.markers.innerHTML = ''; return; }
    let html = '';
    for (const st of lastStations) {
      if (st.hz < lastLowerHz || st.hz > lastUpperHz) continue;
      const pct = (100 * (st.hz - lastLowerHz) / span).toFixed(2);
      const tuned = Math.abs(lastTunedHz - st.hz) < TUNED_NEAR_HZ;
      const label = st.name || 'FM';
      html += '<div style="position:absolute;top:0;left:' + pct + '%;'
        + 'transform:translateX(-50%);white-space:nowrap;font:11px monospace;'
        + 'padding:1px 4px;border:1px solid ' + MARKER_COLOR + ';'
        + 'border-radius:2px;background:rgba(10,15,22,0.85);color:'
        + MARKER_COLOR + (tuned ? ';font-weight:bold;background:#133' : '')
        + '">' + label + '</div>';
    }
    e.markers.innerHTML = html;
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
    lastLowerHz = state.center_hz - state.sample_rate_hz / 2;
    lastUpperHz = state.center_hz + state.sample_rate_hz / 2;
    lastTunedHz = state.center_hz;
    const lower = lastLowerHz / 1e6;
    const upper = lastUpperHz / 1e6;
    elements().axis.textContent =
      lower.toFixed(3) + ' MHz' + ' — ' + upper.toFixed(3)
      + ' MHz (newest at top)';
    // The span moved, so the marks move with it, and the row highlight
    // follows the new tuning without waiting for the next fm_state.
    renderMarkers();
  }

  // The trace colour the window's charts draw in.
  const TRACE = '#5adcc8';
  const BAR = '#a9c5d6';       /* the window's bar fill */
  const BAR_WIN = '#63e4aa';   /* the winning timing offset */

  function clearChart(x, w, h) {
    x.fillStyle = '#0a0f16';
    x.fillRect(0, 0, w, h);
  }

  // A dBFS line spectrum, optionally with the multiplex's three landmarks and
  // a top-of-band label -- the shared body of the multiplex and audio-spectrum
  // charts, which differ only in their landmarks and their top frequency.
  function spectrumChart(g, lowerHz, upperHz, power, landmarks) {
    const w = g.c.width, h = g.c.height, span = upperHz - lowerHz;
    clearChart(g.x, w, h);
    if (span > 0 && landmarks) {
      g.x.font = '12px monospace';
      for (const m of landmarks) {
        const bx = Math.round(w * (m.hz - lowerHz) / span);
        if (bx < 0 || bx > w) continue;
        g.x.strokeStyle = '#232f3b';
        g.x.beginPath();
        g.x.moveTo(bx + 0.5, 0);
        g.x.lineTo(bx + 0.5, h);
        g.x.stroke();
        g.x.fillStyle = '#8291a0';
        g.x.fillText(m.label, bx + 3, 12);
      }
    }
    plot(g.x, w, h, power, TRACE, dbfsToY);
    if (span > 0) {
      g.x.fillStyle = '#8291a0';
      g.x.fillText((upperHz / 1e3).toFixed(0) + ' kHz', w - 60, h - 4);
    }
  }

  function drawMultiplex(lowerHz, upperHz, power) {
    spectrumChart(elements().cc.mpx, lowerHz, upperHz, power, LANDMARKS);
  }

  function drawAudioSpectrum(lowerHz, upperHz, power) {
    spectrumChart(elements().cc.aspec, lowerHz, upperHz, power, null);
  }

  // The audio waveform, a line on a linear [-1, 1] axis (not dBFS): a flat
  // line is dead air, a trace clipping the edges is a level follower behind a
  // louder station -- the window's own reading of this chart.
  function drawWave(wave) {
    const g = elements().cc.wave, w = g.c.width, h = g.c.height;
    clearChart(g.x, w, h);
    g.x.strokeStyle = '#2a3744';
    g.x.beginPath();
    g.x.moveTo(0, h / 2 + 0.5);
    g.x.lineTo(w, h / 2 + 0.5);   /* the zero line */
    g.x.stroke();
    plot(g.x, w, h, wave, TRACE, (v, hh) => hh * (1 - (v + 1) / 2));
  }

  // The RDS constellation: two lobes either side of the origin is a decode,
  // one blob is a subcarrier not resolving, a ring is an axis not settled.
  function drawScatter(pi, pq) {
    const g = elements().cc.scatter, w = g.c.width, h = g.c.height;
    clearChart(g.x, w, h);
    // Axes through the middle, the window's framing.
    g.x.strokeStyle = '#1b2531';
    g.x.beginPath();
    g.x.moveTo(w / 2 + 0.5, 0); g.x.lineTo(w / 2 + 0.5, h);
    g.x.moveTo(0, h / 2 + 0.5); g.x.lineTo(w, h / 2 + 0.5);
    g.x.stroke();
    // A fixed scale so the cloud does not breathe with its own outliers; the
    // soft symbols sit around +/-1.5.
    const scale = 2.0;
    g.x.fillStyle = TRACE;
    for (let k = 0; k < pi.length; k++) {
      const x = w / 2 + (pi[k] / scale) * (w / 2);
      const y = h / 2 - (pq[k] / scale) * (h / 2);
      g.x.fillRect(x - 1, y - 1, 2, 2);
    }
  }

  // A bar chart over `values`, 0 to `ymax`, one bar per value, with an
  // optional highlighted bar (the winning timing offset).
  function barChart(g, values, ymax, color, highlight) {
    const w = g.c.width, h = g.c.height, n = values.length;
    clearChart(g.x, w, h);
    if (n <= 0 || ymax <= 0) return;
    const bw = w / n;
    for (let k = 0; k < n; k++) {
      const bh = Math.max(0, Math.min(1, values[k] / ymax)) * (h - 2);
      g.x.fillStyle = k === highlight ? BAR_WIN : color;
      g.x.fillRect(k * bw + 1, h - bh, Math.max(1, bw - 2), bh);
    }
  }

  function drawTiming(energy, winOffset) {
    barChart(elements().cc.timing, energy, 1.05, BAR, winOffset);
    const cap = elements().timingCap;
    if (cap)
      cap.textContent = 'symbol timing: offset ' + winOffset + ' of '
        + energy.length + ' wins';
  }

  // A round ceiling, not 1.15x the tallest bar, so the axis does not shift as
  // a count crosses ten -- the window's `sdrgui_nice_ceiling`.
  function niceCeiling(v) {
    if (v <= 1) return 1;
    const mag = Math.pow(10, Math.floor(Math.log10(v)));
    for (const s of [1, 2, 5, 10]) {
      if (v <= s * mag) return s * mag;
    }
    return 10 * mag;
  }

  function drawGroups(counts) {
    let top = 1;
    for (const c of counts) if (c > top) top = c;
    barChart(elements().cc.groups, counts, niceCeiling(top), BAR, -1);
  }

  // One panel of the three-across row the window draws, with the window's
  // own fill, 1px edge and caption -- `draw_panel()` in src/gui/view_fm.c, whose
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

  // One cell of the analysis grid: a label over a canvas that fills the rest
  // of the cell. `capId` names the label for the one caption that changes at
  // runtime -- the timing chart says which offset won.
  function chartCell(canvasId, label, capId) {
    const cap = '<div class="label"' + (capId ? ' id="' + capId + '"' : '')
      + '>' + label + '</div>';
    return '<div style="min-width:0;min-height:0;display:flex;'
      + 'flex-direction:column">' + cap
      + '<canvas id="' + canvasId + '" style="flex:1 1 0;min-height:0;'
      + 'width:100%"></canvas></div>';
  }

  return {
    id: 'fm',
    label: 'FM',
    // The live stream set, swapped by `showCharts()` between the signal view's
    // (waterfall + fm_state) and the charts view's (the three analysis streams
    // + the multiplex + fm_state). `waterfall` is the Scope's own stream --
    // one stream, two views, rather than an `fm_waterfall` carrying identical
    // bytes under another name. Mutated in place so this reference stays the
    // one the shell reads.
    streams: streams,
    // The window gives its waterfall the whole width and the room left
    // above the panels; this takes the width the shell measured and about
    // two fifths of the viewport, with a floor so a short window still
    // shows a band of it rather than a line.
    //
    resize: resizeCanvases,
    // The window's own arrangement, mirrored: a toolbar, then a row with the
    // Band II table on the left and the waterfall on the right, then the
    // three panels across the bottom. `min-height:0`/`min-width:0` on the
    // flex children is what lets each be smaller than its content rather than
    // pushing the page past the viewport and bringing the scrollbar back.
    markup:
      '<div style="margin-bottom:10px;flex:0 0 auto">'
      + '<button id="fm-scan" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer;margin-right:8px">Scan band</button>'
      + '<button id="fm-charts" style="background:#16202c;color:#8291a0;'
      + 'border:1px solid #232f3b;font:14px monospace;padding:6px 16px;'
      + 'cursor:pointer">Show charts</button></div>' +
      // The signal view: table left, waterfall right, the way the window draws
      // Band II beside its waterfall. Hidden as a whole when "Show charts" is
      // on, which swaps it for the full-width chart grid below -- the window's
      // analysis arrangement takes the full area, not a corner.
      '<div id="fm-main" style="display:flex;gap:16px;flex:1 1 0;'
      + 'min-height:0">' +
        // Band II. Its own panel rather than the generic one, because the
        // rows scroll inside a fixed header -- the scrolling region is what a
        // long band needs and what the layout gate reads as this view's main
        // content when a scan has filled it.
        '<div style="flex:1 1 0;min-width:0;background:' + PANEL_FILL
        + ';border:1px solid ' + PANEL_EDGE + ';padding:10px 12px 12px;'
        + 'display:flex;flex-direction:column;min-height:0">' +
          '<div id="fm-bandii-caption" style="color:' + PANEL_CAPTION
          + ';font-size:16px;margin-bottom:6px">Band II</div>' +
          '<div id="fm-bandii-status" class="label" '
          + 'style="margin-bottom:6px">awaiting fm_state...</div>' +
          '<div style="flex:1 1 0;min-height:0;overflow:auto">' +
            '<table><thead><tr><th>MHz</th><th>LEVEL</th>'
            + '<th>BROADCAST</th><th>RDS</th><th>STATION</th></tr></thead>'
            + '<tbody id="fm-bandii-rows"></tbody></table>' +
            '<div id="fm-bandii-empty" class="label">press Scan band</div>' +
          '</div>' +
        '</div>' +
        // The waterfall column. The markers are an absolutely-positioned
        // layer over the canvas top rather than drawn into it, so the
        // scrolling picture never erases them.
        '<div style="flex:1 1 0;min-width:0;display:flex;'
        + 'flex-direction:column;min-height:0">' +
          '<div id="fm-waterfall-wrap" style="flex:1 1 0;min-height:140px;'
          + 'position:relative;display:flex;flex-direction:column">' +
            '<canvas id="fm-waterfall" style="flex:1 1 0;min-height:0;'
            + 'width:100%"></canvas>' +
            '<div id="fm-markers" style="position:absolute;top:0;left:0;'
            + 'right:0;height:0;pointer-events:none"></div>' +
            '<div class="label" id="fm-axis">awaiting receiver_state...</div>' +
          '</div>' +
        '</div>' +
      '</div>' +
      // The analysis grid, the window's "Show charts": six charts, three across
      // and two down, in the window's own order -- multiplex, audio waveform,
      // audio spectrum; then RDS symbols, symbol timing, groups by type. Hidden
      // until the toggle, and then it takes the room the signal view had. Each
      // cell is a label over a canvas that fills what is left; `min-height:0`/
      // `min-width:0` let the cells divide the grid rather than overflow it.
      '<div id="fm-charts-grid" hidden style="display:none;flex:1 1 0;'
      + 'min-height:0;grid-template-columns:repeat(3,1fr);'
      + 'grid-template-rows:repeat(2,1fr);gap:10px">' +
      chartCell('fm-c-mpx',
                'multiplex 0-76 kHz: pilot 19, stereo 38, RDS 57', '') +
      chartCell('fm-c-wave', 'audio: the last tenth of a second', '') +
      chartCell('fm-c-aspec', 'audio spectrum, 0-16 kHz, de-emphasised', '') +
      chartCell('fm-c-scatter', 'RDS symbols, before the axis is chosen', '') +
      chartCell('fm-c-timing', 'symbol timing', 'fm-c-timing-cap') +
      chartCell('fm-c-groups',
                'groups by type: 0 carries the name, 2 the radio text', '') +
      '</div>' +
      '<div style="display:flex;gap:16px;margin-top:10px;flex:0 1 auto;'
      + 'min-height:0">' +
        panel('Signal', '<table><tbody id="fm-signal-rows"></tbody></table>') +
        panel('Station', '<table><tbody id="fm-station-rows"></tbody></table>') +
        panel('Where the decode stopped',
              '<table><tbody id="fm-funnel-rows"></tbody></table>'
              // Two lines' worth, always. The funnel's sentence is one of
              // five of very different lengths, so a long one wraps and a
              // short one does not -- and every chart above it moved by a
              // line each time the verdict changed. Reserved rather than
              // fitted: the sentence is the point of the panel and must
              // not be clipped.
              + '<div id="fm-reading" style="margin-top:8px;'
              + 'min-height:2.6em">'
              + 'awaiting fm_state...</div>') +
      '</div>',
    render(msg) {
      if (msg.kind === 'waterfall_row') {
        if (!charting) drawWaterfall(msg.row);
      } else if (msg.kind === 'fm_spectrum') {
        if (charting) drawMultiplex(msg.lowerHz, msg.upperHz, msg.power);
      } else if (msg.kind === 'fm_audio') {
        if (charting) drawWave(msg.wave);
      } else if (msg.kind === 'fm_audio_spectrum') {
        if (charting) drawAudioSpectrum(msg.lowerHz, msg.upperHz, msg.power);
      } else if (msg.kind === 'fm_scatter') {
        if (charting) drawScatter(msg.i, msg.q);
      } else if (msg.kind === 'state') {
        if (msg.state.type === 'fm_state') renderState(msg.state);
        else if (msg.state.type === 'receiver_state') renderAxis(msg.state);
      }
    },
  };
})();
