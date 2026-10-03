// The shell (ticket 14, Phase 3): the socket, reconnect, ADR-0027's
// generation rule, the subscription -- now following whichever view is
// active -- tab routing through a registry, and the Health panel.
// Composes `VIEWS`; draws nothing itself.
const hud = document.getElementById('hud');
const health = document.getElementById('health');
const versionEl = document.getElementById('version');

// The registry. Ticket 07's remaining views each add one entry here and
// one file under web/views/ -- no other file, and no other change to
// this one.
//
// A view's `id` is the whole of its identity now: it is what `view <name>`
// sends, what `receiver_state.screen` comes back as, and the DOM id suffix.
// The views used to also declare `tab` and `decode` as raw numbers, which
// was two of this program's enums re-declared in JavaScript.
const VIEWS = [ScopeView, SurveyView, FmView, GsmView, AdsbView, TetraView,
               SrdView, LteView, SettingsView,
               CalibrationView];

let activeView = null;
// The view this page has asked the server for and not yet been told it has.
//
// A click shows the panel at once -- a browser waiting a round trip to
// redraw a button press reads as broken -- and sends `view <name>`. A
// `receiver_state` already in flight still names the *old* screen, so
// `handleState()` switched straight back to it and the next state a
// quarter-second later switched forward again: a visible flip on every tab
// click, and about one run in four of `check-web-layout` measured the
// wrong panel while every message said otherwise.
//
// So a state's `screen` is *ignored* while a request is outstanding, and
// only that field -- the tuning in the same message is still read, because
// it was never in question. Cleared when a state finally names the view, or
// when `command_result` says the command failed, so a refusal cannot wedge
// the page on a screen the server is not on. If an acknowledgement were
// somehow lost the page would sit on the view the reader clicked, which is
// the harmless direction -- and `check-viewer-link` pins that a command
// result is never dropped.
let pendingScreen = null;
let latestGeneration = 0; // the newest tuning_generation receiver_state has named
let sent = 0, dropped = 0; // this Viewer's own count of what it drew vs discarded
let ws = null; // module-scope so the tab buttons can send on it

// Which view a `receiver_state` describes, by name.
//
// This matched `state.tab === v.tab && state.decode === v.decode`, which
// meant every view re-declared two of the program's enums as numbers --
// `tab: 2, decode: 0`. That is the shape that drew the survey's marks
// swapped for months (web-visualization/15). `receiver_state` carries one
// `screen` name now, from the same vocabulary `view <name>` uses, and it is
// each view's own `id`.
//
// The fallback is the Scope, reached when the window is on a decode view
// this page does not have yet (ticket 07's remaining list). Showing the
// Scope is honest there -- showing nothing, or a panel for a screen that is
// not up, would not be.
function viewForState(state) {
  return VIEWS.find((v) => v.id === state.screen) || VIEWS[0];
}

// Whether this message's `screen` may move the page. See `pendingScreen`.
function screenIsSettled(state) {
  if (pendingScreen === null) return true;
  if (state.screen === pendingScreen) { pendingScreen = null; return true; }
  return false;
}

// Builds the tab bar and every view's panel from the registry, once, at
// load. A view exports `markup` and `label`; this is the only place that
// reads either.
function mountViews() {
  const tabs = document.getElementById('tabs');
  const panels = document.getElementById('panels');
  tabs.innerHTML = VIEWS.map((v) => '<button id="tab-' + v.id + '">' + v.label + '</button>').join('');
  panels.innerHTML = VIEWS.map((v) => '<div id="panel-' + v.id + '" hidden>' + v.markup + '</div>').join('');
  VIEWS.forEach((v) => {
    document.getElementById('tab-' + v.id).onclick = () => selectView(v, true);
    // A view with controls binds them once, here, after its markup is in
    // the document -- the same reason `elements()` is lazy. Optional: only
    // the views that *write* have anything to bind.
    if (typeof v.mounted === 'function') v.mounted();
  });
}

// The one way a view sends anything. Views never touch the socket (the
// layer rule in the web-view skill), and a command sent before the link is
// up is dropped rather than queued -- the server's state is the truth and
// the next `settings_state` will say what actually happened.
function sendCommand(line) {
  if (ws && ws.readyState === 1) ws.send(line);
}

// Ticket 07: which view is showing, and which the server has confirmed.
// The click switches the panel at once -- a browser waiting a round trip
// to redraw a button press reads as broken -- and `receiver_state`
// corrects it afterwards if the two ever disagree (another Viewer
// switching it, or this page reconnecting mid-session with no `view` of
// its own yet sent).
//
// Ticket 14 Phase 3: a change of view also rebuilds the subscribe line,
// which is the point of the registry existing at all -- the server
// should send only what whichever view is showing actually draws.
function selectView(view, announce) {
  const changed = view !== activeView;
  activeView = view;
  VIEWS.forEach((v) => {
    document.getElementById('panel-' + v.id).hidden = (v !== view);
    document.getElementById('tab-' + v.id).classList.toggle('active', v === view);
  });
  if (announce && ws) {
    pendingScreen = view.id;
    ws.send('view ' + view.id);
  }
  if (changed) subscribeToActiveView();
  // After the panel is shown, not before: a hidden element has no layout,
  // so a view measured while it was hidden would size its canvases to
  // zero.
  resizeActiveView();
}

// The shell says *when* to resize; each view matches its own canvases to
// whatever CSS laid them out at. That is the same division
// `view_scope_resize_if_needed()` keeps on the native side -- the frame
// loop calls an entry point rather than reaching into a view's fields --
// and the shell deliberately passes no numbers: the stylesheet has already
// decided how the viewport divides up, and arithmetic repeating that
// decision is how a page ends up disagreeing with itself by a scrollbar.
//
// A view with no `resize` simply does not have canvases to size.
function resizeActiveView() {
  if (!activeView || !activeView.resize) return;
  activeView.resize();
  // And once more on the next frame.
  //
  // Fitting a canvas can itself move the layout, and a `ResizeObserver`
  // that fires during its own callback has its second notification
  // deferred -- so a box that settles four pixels later leaves the backing
  // store four pixels short, which is a stretched picture. Seen at
  // 1024x600, where the health footer gains a line when a stream first
  // drops and hands the panel those pixels back.
  //
  // Costs one comparison when nothing moved: `fitCanvas()` returns early
  // if the geometry already matches, so a settled layout does no work.
  requestAnimationFrame(() => {
    if (activeView && activeView.resize) activeView.resize();
  });
}
window.addEventListener('resize', resizeActiveView);

// And a sibling changing height is a resize too, which `window`'s event is
// not. `#panels` is a flex child, so anything that makes the footer taller
// makes it shorter -- and the footer *does* grow: `renderHealth()` runs
// once a second and gains a "(x% lost)" span the first time a stream drops,
// which wraps the line. Nothing re-fitted the canvases after that, so their
// backing stores kept a height the layout no longer had.
//
// It showed up as `check-web-layout` failing about one run in five, 312
// against 303 on the survey chart -- an intermittent check, which is worse
// than none because it teaches a reader to run it again. The page bug is
// the same one, on any viewport where a drop happens while a chart is up.
//
// Observing `#panels` rather than each canvas: a canvas's box is set by
// flex, so re-fitting it cannot change its own box and this cannot
// oscillate -- and `fitCanvas()` returns early when the geometry already
// matches, so a spurious notification costs a comparison.
if (typeof ResizeObserver === 'function') {
  const watch = new ResizeObserver(resizeActiveView);
  watch.observe(document.getElementById('panels'));
  // And every canvas, which `#panels` alone does not cover.
  //
  // A change *inside* a panel -- an axis label wrapping to two lines at a
  // narrower viewport, a caption growing -- moves a chart's box without
  // moving the container's, so an observer on the container never fires.
  // That left `fm-waterfall`'s backing store four pixels short of its box
  // at 1024x600, about two runs in five: a stretched picture, and the kind
  // of intermittent that reads as flakiness rather than as a bug.
  //
  // Watching a canvas cannot oscillate here: every one of them is
  // `flex:1 1 0`, so its backing store does not feed its own layout -- the
  // `auto` basis that *did* is the fault this page has already fixed twice.
  document.querySelectorAll('#panels canvas').forEach((c) => watch.observe(c));
}

// receiver_state and link_health are the shell's own concern, not a
// view's -- both always asked for regardless of which view shows;
// everything else in the line is whichever view is active right now.
function subscribeToActiveView() {
  if (!ws) return;
  ws.send('subscribe receiver_state link_health ' + activeView.streams.join(' '));
}

mountViews();
selectView(ScopeView, false); // this page's own default, unchanged by the registry

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
  // ADR-0027's amendment (2026-09-17): whatever query string loaded this
  // page -- '?token=...', once the server was started with --serve-bind
  // beyond loopback -- is forwarded onto the socket's own path unchanged,
  // since the same string satisfies the same check there. location.search
  // is '' when there is none, so a token-free server is unaffected.
  ws = new WebSocket('ws://' + location.host + '/viewer' + location.search);
  ws.binaryType = 'arraybuffer';
  ws.onopen = () => {
    // A fresh socket has no outstanding request on it, and the first
    // `receiver_state` over it is authoritative. Left set, this page would
    // ignore the server's screen until it happened to agree.
    pendingScreen = null;
    subscribeToActiveView();
    hud.textContent = 'connected, awaiting receiver state...';
  };
  ws.onclose = () => { hud.textContent = 'disconnected -- retrying...'; setTimeout(connect, 1000); };
  ws.onerror = () => ws.close();
  ws.onmessage = (ev) => {
    // Busy time and received bytes are this page's own two Health
    // fields (ticket 08); the try/finally means every exit path below
    // -- the early 'stale' return included -- is timed the same way,
    // rather than each needing its own bookkeeping line.
    const t0 = performance.now();
    try {
      const msg = decodeMessage(ev, latestGeneration); // wire.js
      bytesThisWindow += msg.bytes;
      if (msg.kind === 'stale') { dropped++; return; } // ADR-0027
      if (msg.kind === 'state') { handleState(msg.state); return; }
      sent++;
      activeView.render(msg); // ticket 14 Phase 3: the registry dispatches
    } finally {
      busyMs += performance.now() - t0;
    }
  };
}

function handleState(state) {
  // Stored, not rendered here: link_health arrives up to once per
  // server block (tens of times a second), and a full innerHTML
  // rebuild on every one of them is JS busy time this page does not
  // need to spend -- the once-a-second interval above already redraws
  // the panel, and spending less time here is less time not reading
  // the socket, which is less backpressure this page itself causes.
  if (state.type === 'link_health') { lastHealth = state; return; }
  if (state.type === 'command_result') {
    // A refused `view` is the one thing that could leave a request
    // outstanding for ever, so it is the one thing this has to notice.
    if (!state.ok && /^view /.test(state.command || '')) pendingScreen = null;
    return;
  }
  if (state.type === 'receiver_state') {
    latestGeneration = state.tuning_generation;
    // The build's version, bottom-right, the way the window draws it in its
    // corner. A constant, so set once and left -- it rides receiver_state
    // because that is the one message about the program rather than a
    // technology.
    if (state.version && versionEl.textContent !== state.version)
      versionEl.textContent = state.version;
    if (screenIsSettled(state))
      selectView(viewForState(state), false); // reflects another Viewer's own
                                              // switch, and the window's
    hud.innerHTML = 'center ' + (state.center_hz / 1e6).toFixed(6) + ' MHz &nbsp; '
      + 'rate ' + (state.sample_rate_hz / 1e6).toFixed(3) + ' MS/s &nbsp; '
      + 'ppm ' + state.ppm + ' &nbsp; '
      + 'generation ' + state.tuning_generation + ' &nbsp; '
      + 'drawn ' + sent + ' declined ' + dropped;
    // And on to the active view, which the shell used to consume this
    // message instead of. A view drawing anything against frequency needs
    // the span it is drawing over, and the tuning is the shell's to
    // *route* rather than the shell's to keep: views/fm.js labels its
    // waterfall's axis from exactly this. Sent after selectView() above,
    // so a switch this message caused delivers it to the new view.
    activeView.render({ kind: 'state', state });
    return;
  }
  // Anything else is a view's own state (survey_state today) -- the
  // shell does not know its shape, only that whichever view subscribed
  // to the stream it arrived on is the one that should read it.
  activeView.render({ kind: 'state', state });
}

// Per-stream sent/dropped/high-water are the server's own facts about
// this connection (nothing else could report a dropped message, since
// it never reaches here); received throughput and JS busy% are this
// page's own two, both rolled into the same panel.
// Two lines rather than seven: the per-stream counts on one, the link and
// what it costs both ends on the other. Seven rows of one figure each is a
// list to read down; this is a line to scan across, and the panel stops
// taking a third of the page under views that have their own tables.
//
// The drop rate is shown only for a stream that has actually dropped
// something. "(0.0%)" repeated on every stream is noise on the one line a
// reader is scanning for the stream that is losing messages -- and a
// figure that is almost always the same value is a figure nobody reads.
function renderHealth() {
  const h = lastHealth;
  if (!h) { health.textContent = 'awaiting link_health...'; return; }
  const stream = (name, c) => {
    const total = c.sent + c.dropped;
    const rate = c.dropped > 0 && total > 0
      ? ' <span>(' + (100 * c.dropped / total).toFixed(1) + '% lost)</span>' : '';
    return name + ' <span>' + c.sent + '/' + c.dropped + '</span>' + rate;
  };
  // `link_health` names all nine streams now, and nine entries is a line
  // nobody reads -- it wrapped to a third line as soon as a session had
  // visited every tab. So: whatever the page is subscribed to right now,
  // which is what "how is this view's link doing" actually means, **plus
  // any stream that has dropped something**, subscribed or not. A loss is
  // the one thing this panel must never hide, including on a tab the
  // reader has since left.
  const showing = new Set(['receiver_state', 'link_health']
    .concat(activeView ? activeView.streams : []));
  const active = Object.entries(h.streams || {})
    .filter(([name, c]) => showing.has(name) || c.dropped > 0);
  health.innerHTML =
    '<div class="row">sent/dropped &nbsp; '
      + (active.length
          ? active.map(([name, c]) => stream(name, c)).join(' &nbsp; ')
          : '<span>nothing yet</span>')
      + '</div>'
    + '<div class="row">high-water <span>' + formatBytes(h.send_queue_high_water)
      + '</span> &nbsp; received <span>' + formatBitsPerSecond(throughputBps)
      + '</span> &nbsp; server CPU <span>' + h.server_cpu_percent.toFixed(1)
      + '%</span> &nbsp; this tab, JS busy <span>' + jsBusyPercent.toFixed(1)
      + '%</span></div>';
}

connect();
