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
// Starts its own `./sdrprobe server` over a capture and its own headless
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
    'server', '--file', CAPTURE, '--sample-rate', '2048000',
    '--frequency', '89.5M', '--serve-port', String(SERVE_PORT),
    '--duration', String(20 + SIZES.length * 25),
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

    for (const tab of ['scope', 'survey', 'fm']) {
      await evaluate(`document.getElementById('tab-${tab}').click(); true`);
      // Long enough on FM for a real decode to name the station, which is
      // the text a headless screenshot could not show.
      await sleep(tab === 'fm' ? 7000 : 1200);
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
    }
  }

  if (PNG) {
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
