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
  if (type === MSG_SURVEY_SPECTRUM) {
    return {
      kind: 'survey_spectrum', bytes: bytes,
      lowerHz: view.getUint32(20, true), upperHz: view.getUint32(24, true),
      power: new Float32Array(ev.data, 28, bins),
    };
  }
  return { kind: 'unknown', bytes: bytes };
}
