# 03 — The scan step machine and the strongest-BCCH choice

Status: ready-for-agent
Blocked by: (none)

`src/overlay_scan.c` decides which ARFCN the scan is on, when it has dwelt long
enough, when it is finished (`update_scan`), and which channel the operator is
handed at the end (`scan_strongest_arfcn`, `scan_strongest_bcch`). The last one
matters most: it chooses the channel the operator then spends their time on,
and a wrong choice looks exactly like a quiet neighbourhood.

`scan_strongest_bcch` prefers a channel with a decoded BSIC over a merely loud
one. That preference is the whole value of the function and there is no check
that it survives a refactor.

What it would take: the two selectors take `const struct app *` but read only
the scan table. Give them a `scan_select.h` over the table alone and check:
an empty table, one candidate, a loud channel with no BSIC against a quieter
one with a BSIC, a tie, and every level equal to the floor. The step machine
needs the same treatment as ticket 01 and can share its shape.

## Comments
