// The browser page's own layout, measured in a real browser.
//
// Ticket 11 (`.scratch/web-visualization/issues/11-*`) is the "why":
// `CLAUDE.md` says a change that draws is not finished until somebody has
// looked at it, `make screens` covers every raylib screen, and the browser
// page -- a tab bar, panels, charts, tables -- had nothing.
//
// Two things were tried before this and neither closed it. A Node DOM shim
// runs the real page against a real server and asserts what it *decided*,
// which is exactly right for behaviour and structurally blind to layout:
// scrolling, stacking and overlap are properties of a layout engine, and a
// shim has none. A headless `--screenshot` produces a PNG in which the
// canvas drew and every JSON-driven value is blank -- ticket 11's own
// finding, and worse than no recipe, because a reviewer diffing two blank
// PNGs sees no difference.
//
// This is the third thing: drive a real browser over the DevTools protocol
// and ask the page about itself. Real layout, so overflow and geometry are
// answerable; the live DOM rather than a picture of it, so text that did
// not render cannot hide behind a canvas that did. A PNG is available
// alongside (`--png`), and is worth trusting *because* the same run has
// already asserted the text is there.
//
// No Playwright, no MCP, no npm. Node's own `fetch` and `WebSocket` reach
// CDP, which needs two methods here: find a page target, and evaluate an
// expression in it. `web/` is under a standing no-framework, no-CDN,
// no-npm constraint (ticket 01, restated by 13 and 14); its test harness
// pulling in a browser automation stack and a second managed Chromium
// would be that constraint held everywhere except where it is checked.
//
// Usage:
//   node scripts/web_layout.mjs [--sizes WxH,WxH] [--png FILE] [--keep]
//
// Starts its own `./sdrprobe web --no-browser` over a capture and its own headless
// Chromium, and stops both. Nothing here needs a receiver.

import { spawn } from 'node:child_process';
import { appendFileSync, writeFileSync } from 'node:fs';

const args = process.argv.slice(2);
const opt = (name, fallback) => {
  const i = args.indexOf(name);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
};

const SIZES = opt('--sizes', '1400x900').split(',').map((s) => {
  const [w, h] = s.split('x').map(Number);
  return { w, h };
});
const PNG = opt('--png', null);
const SERVE_PORT = Number(opt('--serve-port', 8930));
const DEBUG_PORT = Number(opt('--debug-port', 9331));
const CAPTURE = opt('--file', 'testfiles/fm_rds_tsf.bin');
// The capture's own tuning. Defaulted to the FM one the gate drives, and
// overridable together with --file so another technology's capture can be
// looked at: played at the wrong rate a capture decodes nothing, and the
// page then shows a correct layout full of "awaiting" -- which is a
// screenshot that proves the layout and nothing else.
const RATE = opt('--rate', '2048000');
const FREQ = opt('--freq', '89.5M');
// Extra flags for the served run, space-separated -- `--arfcn 69` and the
// like, which some captures need before their view has anything to say.
const EXTRA = opt('--extra', '').split(' ').filter((a) => a.length > 0);
// Which tab to leave showing for --png. The run visits them all either way;
// without this the picture is whichever came last, which is rarely the one
// being looked at.
const PNG_TAB = opt('--tab', '');

let pass = 0, fail = 0;
const ok = (name, cond, extra = '') => {
  if (cond) { pass++; } else { fail++; console.log(`    FAIL  ${name} ${extra}`); }
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

/* ------------------------------------------------------------------ */
/* CDP, in as much of it as this needs.                                 */
/* ------------------------------------------------------------------ */

// Whether anything already answers on the debug port. A browser left over
// from an earlier run answers exactly like the one this run is about to
// start, and attaching to it is how a check comes to measure a page from
// ten minutes ago -- reporting a failure that is really a stale tab, or,
// worse, a pass.
async function debugPortBusy(port) {
  try {
    await fetch(`http://127.0.0.1:${port}/json/version`,
                { signal: AbortSignal.timeout(500) });
    return true;
  } catch { return false; }
}

// The page target, and it must be *ours*: the URL has to be the server this
// run started. Without that check a stale browser on the same port is
// indistinguishable from the one just launched, which cost a debugging
// session -- every assertion failed with "Cannot read properties of null"
// because the page being measured was some earlier run's.
async function pageTarget(port, wantUrl) {
  for (let i = 0; i < 80; i++) {
    try {
      const list = await (await fetch(`http://127.0.0.1:${port}/json`)).json();
      const page = list.find((t) => t.type === 'page' &&
                                    t.webSocketDebuggerUrl &&
                                    t.url && t.url.startsWith(wantUrl));
      if (page) return page.webSocketDebuggerUrl;
    } catch { /* not listening yet */ }
    await sleep(250);
  }
  throw new Error(`chromium never exposed a page at ${wantUrl} on the `
                  + `debug port ${port}`);
}

function connect(url) {
  const ws = new WebSocket(url);
  const pending = new Map();
  let nextId = 1;
  ws.onmessage = (ev) => {
    const m = JSON.parse(ev.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
  };
  const ready = new Promise((r) => { ws.onopen = r; });
  return {
    ready, ws,
    send(method, params = {}) {
      const id = nextId++;
      return new Promise((res) => {
        pending.set(id, res);
        ws.send(JSON.stringify({ id, method, params }));
      });
    },
  };
}

/* ------------------------------------------------------------------ */
/* What the page is asked about itself.                                 */
/* ------------------------------------------------------------------ */

// Everything read here is read through the DOM the page itself wrote --
// never a script-internal variable, which in a browser's inline <script>
// is not a property of anything reachable from outside it.
const MEASURE = `(() => {
  const d = document.documentElement;
  const q = (id) => document.getElementById(id);
  const box = (el) => {
    const r = el.getBoundingClientRect();
    return { w: Math.round(r.width), h: Math.round(r.height),
             top: Math.round(r.top), bottom: Math.round(r.bottom) };
  };
  const panel = document.querySelector('#panels > div:not([hidden])');
  const panelH = panel ? Math.round(panel.getBoundingClientRect().height) : 0;
  const canvases = [...document.querySelectorAll('#panels > div:not([hidden]) canvas')]
    .filter((c) => c.getBoundingClientRect().width > 0)
    .map((c) => ({ id: c.id, storeW: c.width, storeH: c.height,
                   boxW: Math.round(c.getBoundingClientRect().width),
                   boxH: Math.round(c.getBoundingClientRect().height) }));
  const shown = [...document.querySelectorAll('#panels > div')]
    .filter((p) => getComputedStyle(p).display !== 'none');
  const signalRows = q('fm-signal-rows');
  const row = signalRows ? signalRows.closest('div[style*="display:flex"]') : null;
  return {
    scrollHeight: d.scrollHeight, clientHeight: d.clientHeight,
    panelH,
    scrollbar: window.innerWidth - d.clientWidth,
    shownPanels: shown.length,
    canvases,
    infoPanels: row ? [...row.children].map(box) : [],
    text: {
      reading: (q('fm-reading') || {}).textContent || '',
      axis: (q('fm-axis') || {}).textContent || '',
      signalRowCount: signalRows ? (signalRows.innerHTML.match(/<tr>/g) || []).length : 0,
      stationRows: (q('fm-station-rows') || {}).innerHTML || '',
      health: (q('health') || {}).innerHTML || '',
    },
  };
})()`;

/* ------------------------------------------------------------------ */

// Both children, killed however this run ends. They were killed at the
// bottom of run(), which a throw skips -- so a failing run leaked its
// browser, and the leaked browser then held the debug port and failed the
// *next* run for a different reason. A check that leaks state and then
// fails on its own leak is worse than no check.
let serve = null, chrome = null;
function stopChildren() {
  for (const child of [chrome, serve])
    if (child && child.exitCode === null) { try { child.kill('SIGKILL'); } catch { /* gone */ } }
  chrome = serve = null;
}
process.on('exit', stopChildren);
for (const sig of ['SIGINT', 'SIGTERM'])
  process.on(sig, () => { stopChildren(); process.exit(1); });

async function run() {
  const serveUrl = `http://127.0.0.1:${SERVE_PORT}/`;

  if (await debugPortBusy(DEBUG_PORT))
    throw new Error(`something already answers on debug port ${DEBUG_PORT} `
                    + `-- a chromium left by an earlier run, most likely. `
                    + `Kill it, or pass --debug-port.`);

  serve = spawn('./sdrprobe', [
    'web', '--no-browser', '--file', CAPTURE, '--sample-rate', RATE,
    ...(EXTRA.length ? [] : ['--frequency', FREQ]), ...EXTRA,
    '--serve-port', String(SERVE_PORT),
    // Long enough for the worst case, which is not the obvious one: the FM
    // tab polls up to 30 s for the station's name, and over a capture that
    // is not the FM one it waits that out every time. At 20 + 25 per size
    // the server exited *before* the last tab was reached, and the page then
    // showed a correct layout with every field "awaiting" -- which looks
    // exactly like a view that does not work.
    '--duration', String(60 + SIZES.length * 35),
  ], { stdio: 'ignore' });
  await sleep(1500);

  chrome = spawn('chromium', [
    '--headless=new', `--remote-debugging-port=${DEBUG_PORT}`,
    `--window-size=${SIZES[0].w},${SIZES[0].h}`,
    '--no-sandbox', '--disable-gpu',
    `http://127.0.0.1:${SERVE_PORT}/`,
  ], { stdio: 'ignore' });

  const cdp = connect(await pageTarget(DEBUG_PORT, serveUrl));
  await cdp.ready;
  await cdp.send('Runtime.enable');
  await cdp.send('Page.enable');

  const evaluate = async (expression) => {
    const r = await cdp.send('Runtime.evaluate',
                             { expression, returnByValue: true, awaitPromise: true });
    if (r.result && r.result.exceptionDetails)
      throw new Error(JSON.stringify(r.result.exceptionDetails));
    return r.result.result.value;
  };

  await sleep(2000);

  for (const size of SIZES) {
    // Resizing through CDP rather than relaunching: the page's own resize
    // handling is part of what is under test.
    await cdp.send('Emulation.setDeviceMetricsOverride',
                   { width: size.w, height: size.h, deviceScaleFactor: 1, mobile: false });
    await sleep(600);

    // GSM is visited over an FM capture, so its readouts say "idle" and
    // "none" -- which is the point: a view has to lay out correctly before
    // it has anything to show, and that is the state a reader meets first.
    for (const tab of ['scope', 'survey', 'fm', 'gsm']) {
      await evaluate(`document.getElementById('tab-${tab}').click(); true`);
      /*
       * Wait for the panel to actually be the one showing, rather than
       * sleeping and hoping -- the same fix, for the same reason, as the
       * TSF wait below.
       *
       * A click shows the panel at once and sends `view <name>`; a
       * `receiver_state` already in flight still names the *old* screen,
       * and `handleState()` switches back to it before the next state
       * corrects it. A flat 1200 ms wait rode through that about one run
       * in four, and the assertions then measured the Scope's canvases
       * while every message said "survey" -- including
       * `exactly one view panel is laid out`, which was true of the wrong
       * panel. A check that measures the wrong screen and passes is worse
       * than one that fails.
       */
      {
        const deadline = Date.now() + 8000;
        for (;;) {
          const shown = await evaluate(
            `(document.querySelector('#panels > div:not([hidden])')||{}).id || ''`);
          if (shown === `panel-${tab}` || Date.now() > deadline) break;
          await sleep(100);
        }
      }
      await sleep(900);
      ok(`${size.w}x${size.h} ${tab}: the panel that is up is the one asked for`,
         (await evaluate(
            `(document.querySelector('#panels > div:not([hidden])')||{}).id || ''`))
             === `panel-${tab}`);
      /*
       * On FM, wait for the decode to actually name the station rather than
       * sleeping a fixed span and hoping. A programme service name is four
       * segments seen whole twice, the capture is three seconds long and
       * loops, and how many loops that takes varies -- a flat 7 s wait
       * failed roughly one run in six, which in a gated check is worse than
       * the fault it was looking for.
       */
      if (tab === 'fm') {
        const deadline = Date.now() + 30000;
        for (;;) {
          const named = await evaluate(
            `/TSF/.test((document.getElementById('fm-station-rows')||{}).innerHTML || '')`);
          if (named || Date.now() > deadline) break;
          await sleep(500);
        }
        /*
         * And let the page settle before measuring it.
         *
         * The poll breaks the instant the name appears, which is the same
         * instant the Station panel gains rows -- so the panels row grows,
         * the waterfall above it loses four pixels, and a measurement taken
         * in that tick catches the canvas before its `ResizeObserver` has
         * re-fitted. That read as `store 177 vs box 181` about two runs in
         * five and looked exactly like a layout bug. A frame of stretched
         * canvas after a DOM change is what every page does; demanding it be
         * correct within the same tick is measuring the harness.
         */
        await sleep(400);
      }
      const m = await evaluate(MEASURE);
      const at = `${size.w}x${size.h} ${tab}`;

      ok(`${at}: does not scroll`, m.scrollHeight <= m.clientHeight,
         `scrollHeight ${m.scrollHeight} > clientHeight ${m.clientHeight}`);
      ok(`${at}: no scrollbar`, m.scrollbar === 0, String(m.scrollbar));
      // The fault that made all three view panels stack down the page: an
      // author `display` beats the `hidden` attribute's own UA rule.
      ok(`${at}: exactly one view panel is laid out`, m.shownPanels === 1,
         String(m.shownPanels));
      ok(`${at}: has a canvas`, m.canvases.length > 0);
      /*
       * The charts are what a reader is here for, and the text around them
       * had been taking the room. Measured at 1400x900 before this was
       * gated: 224 of the 900 pixels went to the title, the tab bar, the
       * hud and the health footer, and GSM's waterfall came out at 195 of a
       * 676px panel -- 29%. Tightening the chrome to 12px and weighting
       * that waterfall 3 to 1 puts it at 359 of 734.
       *
       * A floor with margin rather than a target: the four views now read
       * 47%, 47%, 49% and 65%, so 40% fails on a real regression and not on
       * a font that rendered a pixel taller.
       */
      /*
       * 65% and not 70: the health footer is one line or two depending on
       * whether any stream has dropped yet, and at 1024x600 that is the
       * difference between 420 and 418 pixels of panel. A threshold that a
       * dropped message can cross is measuring the footer, not the layout.
       * Before the chrome was tightened this read 75% at 1400x900; it is
       * 82% now, so 65 has room and still catches a real regression.
       */
      ok(`${at}: the page is mostly the view`, m.panelH >= m.clientHeight * 0.65,
         `panel ${m.panelH} of viewport ${m.clientHeight}`);
      /*
       * Two floors, because the room a view has is not the same at every
       * size and one threshold would have to be the smaller. Measured
       * across four viewports and all four views, tallest canvas over
       * panel height:
       *
       *   1024x600  scope 44  survey 61  fm 42  gsm 33
       *   1280x720  scope 45  survey 62  fm 54  gsm 43
       *   1400x900  scope 47  survey 63  fm 66  gsm 51
       *   1920x1080 scope 47  survey 64  fm 72  gsm 55
       *
       * GSM at 1024x600 is the floor of that, and inherently: its two
       * readout lines and two information panels cost about 200 fixed
       * pixels of a 434-pixel panel whatever the chart does. So 25%
       * everywhere -- which the pre-fix 195-of-676 (29%) would have passed,
       * so it is not the regression detector -- and 40% wherever there is
       * actually room to divide, which is what catches it.
       */
      const tallest = Math.max(0, ...m.canvases.map((c) => c.boxH));
      ok(`${at}: its biggest chart is not a sliver`,
         tallest >= m.panelH * 0.25,
         `tallest canvas ${tallest} of panel ${m.panelH}`);
      if (m.panelH >= 500)
        ok(`${at}: and with room to divide, it dominates`,
           tallest >= m.panelH * 0.4,
           `tallest canvas ${tallest} of panel ${m.panelH}`);
      for (const c of m.canvases)
        ok(`${at}: ${c.id} store matches its box`,
           c.storeW === c.boxW && c.storeH === c.boxH,
           `${c.storeW}x${c.storeH} vs ${c.boxW}x${c.boxH}`);

      if (tab === 'fm') {
        const w = new Set(m.infoPanels.map((p) => p.w));
        const h = new Set(m.infoPanels.map((p) => p.h));
        const top = new Set(m.infoPanels.map((p) => p.top));
        ok(`${at}: three information panels`, m.infoPanels.length === 3,
           String(m.infoPanels.length));
        ok(`${at}: of one width`, w.size === 1, JSON.stringify([...w]));
        ok(`${at}: of one height`, h.size === 1, JSON.stringify([...h]));
        ok(`${at}: on one row`, top.size === 1, JSON.stringify([...top]));

        // Ticket 11's hardest criterion: a rendered page that shows the
        // JSON-driven text, not only the canvas. A headless screenshot
        // showed the trace and left every value blank; this reads the
        // values out of the live DOM, so blank cannot pass.
        ok(`${at}: the funnel's sentence rendered`,
           m.text.reading.length > 0 && !/awaiting/.test(m.text.reading),
           JSON.stringify(m.text.reading));
        ok(`${at}: the signal panel has rows`, m.text.signalRowCount >= 3,
           String(m.text.signalRowCount));
        ok(`${at}: the station was named`, /TSF/.test(m.text.stationRows),
           m.text.stationRows.slice(0, 120));
        ok(`${at}: the waterfall axis is labelled`, /MHz/.test(m.text.axis),
           JSON.stringify(m.text.axis));
        ok(`${at}: the health footer rendered`, /sent\/dropped/.test(m.text.health));
      }

      /*
       * And the same charts after a *sibling* grows, which `window`'s
       * resize event does not cover.
       *
       * This is a deterministic stand-in for something the page really
       * does: `renderHealth()` runs once a second and the footer gains a
       * "(x% lost)" span the first time a stream drops, which wraps the
       * line and takes a row off `#panels`. Nothing re-fitted the canvases
       * after that, so their backing stores kept a height the layout no
       * longer had -- and it surfaced as this suite failing about one run
       * in five (312 against 303 on the survey chart), which in a gated
       * check is worse than the fault it was looking for.
       *
       * A `ResizeObserver` on `#panels` is the fix, and this is what says
       * so: forcing the footer taller and asserting the stores followed
       * fails without it every single time, where waiting for a real drop
       * fails one time in five.
       */
      await evaluate(
        `document.getElementById('health').style.paddingBottom = '40px'; true`);
      await sleep(300);
      {
        const grown = await evaluate(MEASURE);
        for (const c of grown.canvases)
          ok(`${at}: ${c.id} store follows a taller footer`,
             c.storeW === c.boxW && c.storeH === c.boxH,
             `${c.storeW}x${c.storeH} vs ${c.boxW}x${c.boxH}`);
        ok(`${at}: still does not scroll with a taller footer`,
           grown.scrollHeight <= grown.clientHeight,
           `scrollHeight ${grown.scrollHeight} > clientHeight ${grown.clientHeight}`);
      }
      await evaluate(
        `document.getElementById('health').style.paddingBottom = ''; true`);
      await sleep(200);
    }
  }

  if (PNG) {
    if (PNG_TAB) {
      await evaluate(`document.getElementById('tab-${PNG_TAB}').click(); true`);
      await sleep(2500);
    }
    const shot = await cdp.send('Page.captureScreenshot', { format: 'png' });
    writeFileSync(PNG, Buffer.from(shot.result.data, 'base64'));
    console.log(`    wrote ${PNG}`);
  }

  cdp.ws.close();
}

try {
  await run();
} catch (e) {
  console.log(`    FAIL  ${e.message}`);
  fail++;
} finally {
  stopChildren();
}

// The same one-line tally every C suite appends through `check_report()`
// (tests/check.h): one open/write/close so it reaches the kernel as a
// single O_APPEND write, which the kernel serialises for a regular file.
// Without it this suite runs in the gate and is missing from the gate's
// own summary -- a suite that does not count itself is a suite whose
// absence nobody notices, which is the `NOT GATED` fault wearing a
// different coat.
const tally = process.env.CHECK_TALLY;
if (tally) {
  try { appendFileSync(tally, `${pass + fail} ${fail}\n`); } catch { /* best effort */ }
}

const label = 'the browser page in a real browser';
console.log(`  ${label.padEnd(56)} ${String(pass + fail).padStart(5)} checks   `
            + (fail ? `${fail} FAILED` : 'ok'));
process.exit(fail ? 1 : 0);
