// The wire format's one reader (viewer_link.c is the only writer): binary
// headers and JSON payloads decoded in one place, so a message type is a
// named constant here instead of a literal repeated in every view that
// dispatches on it. Before this file existed the comment in
// `src/viewer_page.h` admitted the duplication outright: "the browser's
// copy of the wire format lives here beside the page". One reader today,
// and still one after ticket 07's remaining views each need it.
const MSG_SPECTRUM = 1;
const MSG_WATERFALL_ROW = 2;
const MSG_SURVEY_SPECTRUM = 3;
const MSG_FM_SPECTRUM = 4;
// The FM analysis charts behind "Show charts": the audio waveform (one array,
// base header), the audio spectrum (range header, like the multiplex), and the
// RDS constellation (two arrays, i then q, like a spectrum's average/peak).
const MSG_FM_AUDIO = 5;
const MSG_FM_AUDIO_SPECTRUM = 6;
const MSG_FM_SCATTER = 7;

// Decodes one WebSocket message and returns a plain object naming its
// `kind`:
//   'state'           -- a JSON message, already parsed (`state`)
//   'stale'           -- a binary message stamped with a tuning_generation
//                        older than the caller's `latestGeneration`
//                        (ADR-0027) -- the caller still has to count it as
//                        declined, not silently drop it
//   'spectrum'         -- `average`, `peak`: Float32Array
//   'waterfall_row'    -- `row`: Float32Array
//   'survey_spectrum'  -- `lowerHz`, `upperHz`, `power`: Float32Array
//   'fm_spectrum'      -- the same three, the FM multiplex at baseband
//   'unknown'          -- a binary message of a type this reader does not
//                        name; still counted as received, drawn as nothing
//
// Every kind carries `bytes`, the size actually received, so a caller can
// roll up throughput without re-measuring `ev.data` itself. Nothing here
// reshapes a field for a view to read more conveniently -- render(state)
// (ticket 14, Phase 3) takes what this returns as-is.
function decodeMessage(ev, latestGeneration) {
  if (typeof ev.data === 'string') {
    return { kind: 'state', bytes: ev.data.length, state: JSON.parse(ev.data) };
  }
  const bytes = ev.data.byteLength;
  const view = new DataView(ev.data);
  const type = view.getUint8(1);
  const generation = view.getUint32(4, true);
  if (generation < latestGeneration) return { kind: 'stale', bytes: bytes };
  const bins = view.getUint32(16, true);
  if (type === MSG_SPECTRUM) {
    return {
      kind: 'spectrum', bytes: bytes,
      average: new Float32Array(ev.data, 20, bins),
      peak: new Float32Array(ev.data, 20 + bins * 4, bins),
    };
  }
  if (type === MSG_WATERFALL_ROW) {
    return { kind: 'waterfall_row', bytes: bytes, row: new Float32Array(ev.data, 20, bins) };
  }
  // The audio waveform: one float array on the base header, a time trace with
  // no frequency axis to carry.
  if (type === MSG_FM_AUDIO) {
    return { kind: 'fm_audio', bytes: bytes, wave: new Float32Array(ev.data, 20, bins) };
  }
  // The RDS constellation: two arrays, i then q, the same i-then-q layout a
  // spectrum uses for average-then-peak.
  if (type === MSG_FM_SCATTER) {
    return {
      kind: 'fm_scatter', bytes: bytes,
      i: new Float32Array(ev.data, 20, bins),
      q: new Float32Array(ev.data, 20 + bins * 4, bins),
    };
  }
  // The two range-header types (VIEWER_RANGE_HEADER_BYTES): an array whose
  // frequencies are its own rather than the receiver's, so it carries the
  // range it spans -- wherever a sweep walked, or the FM multiplex's
  // baseband. Same layout, so one branch decodes both and they differ only
  // in the `kind` whichever view is showing dispatches on.
  if (type === MSG_SURVEY_SPECTRUM || type === MSG_FM_SPECTRUM ||
      type === MSG_FM_AUDIO_SPECTRUM) {
    const kind = type === MSG_SURVEY_SPECTRUM ? 'survey_spectrum'
               : type === MSG_FM_SPECTRUM ? 'fm_spectrum'
               : 'fm_audio_spectrum';
    return {
      kind: kind,
      bytes: bytes,
      lowerHz: view.getUint32(20, true), upperHz: view.getUint32(24, true),
      power: new Float32Array(ev.data, 28, bins),
    };
  }
  return { kind: 'unknown', bytes: bytes };
}
