// Generic formatting -- plain numbers in, a string out. Never sees a
// message, a socket or a view (this file's half of ADR-0007's split,
// restated for JavaScript: see CLAUDE.md's "Presentation" section).
//
// Decimal SI throughout (AGENTS.md's units convention): a byte count is
// KB/MB at 1000/1e6, a throughput is Kbps/Mbps -- bits, not bytes, at
// the same decimal scale -- never KiB/MiB, never a silent /1024 behind
// a "KB" label. That mismatch shipped once, in this Viewer's own Health
// panel (ticket 08): both figures were computed with /1024 and labelled
// "KB"/"KB/s", a KiB value wearing a decimal name.
function formatBytes(n) {
  if (n >= 1e6) return (n / 1e6).toFixed(2) + ' MB';
  return (n / 1e3).toFixed(1) + ' KB';
}

function formatBitsPerSecond(bytesPerSecond) {
  const bits = bytesPerSecond * 8;
  if (bits >= 1e6) return (bits / 1e6).toFixed(2) + ' Mbps';
  return (bits / 1e3).toFixed(1) + ' Kbps';
}
