// A header row and n data rows into a <tbody> -- generalised from the
// Survey's candidate table rather than copied into it, since every one of
// ticket 07's remaining decode views wants the same shape (a header the
// view's own markup already draws, and rows built from whatever fields
// that decode carries). Plain data and geometry: `rows` is an array of
// arrays of already-formatted cell HTML, one array per row; this file
// never sees a message, a socket or a view.
// `rowStyle`, when given, is called with each row's index and returns the
// value for that <tr>'s `style` attribute -- which is how a table highlights a
// selected row (the SRD frame log) without each caller re-implementing the
// map. Omitted, the rows carry no style, exactly as before.
function renderRows(tbody, rows, rowStyle) {
  tbody.innerHTML = rows.map((cells, i) =>
    '<tr' + (rowStyle ? ' style="' + rowStyle(i) + '"' : '') + '>'
    + cells.join('') + '</tr>').join('');
}
