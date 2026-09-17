// A header row and n data rows into a <tbody> -- generalised from the
// Survey's candidate table rather than copied into it, since every one of
// ticket 07's remaining decode views wants the same shape (a header the
// view's own markup already draws, and rows built from whatever fields
// that decode carries). Plain data and geometry: `rows` is an array of
// arrays of already-formatted cell HTML, one array per row; this file
// never sees a message, a socket or a view.
function renderRows(tbody, rows) {
  tbody.innerHTML = rows.map((cells) => '<tr>' + cells.join('') + '</tr>').join('');
}
