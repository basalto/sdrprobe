# 03 — The GSCN raster, and walking it without a window

Status: needs-triage
Blocked by: 02

`src/nr_scan.h`, the analogue of `lte_scan.h`: plain arithmetic, no receiver
and no window, checked by `tests/nr_scan_test.c`.

Synchronisation blocks sit on the GSCN raster rather than a channel raster.
Below 3 GHz the reference frequency is believed to be
`N * 1200 kHz + M * 50 kHz` with `M` in {1, 3, 5} -- cite 38.104 rather than
trusting that. The useful consequence is that 758-788 MHz holds only about 25
GSCN points against band 28's 300 EARFCNs, so a full sweep is seconds rather
than minutes.

That changes the economics the LTE scan was designed around. `lte_scan.h`
spends its whole design on ordering three hundred channels so the likely
answers come first; twenty-five points need no such ordering, and the looks
per point can be generous from the start rather than rationed.

Carry over the two rules the LTE scan earned the hard way, both in
`lte_scan.h`: a silent point still gets enough looks that its silence means
something, and every listed identity is revisited before the scan ends. They
were not free, and nothing about them is LTE-specific.

Then a headless entry point in the shape of `--lte-scan`, since a scan is
otherwise a button and a button is not something a script can press (ADR-0012).
