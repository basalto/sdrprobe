# More out of one LTE carrier, without more bandwidth

The LTE chain reads a cell and its Master Information Block and stops. That is
not where the receiver's limit is -- it is where the work stopped.

SIB1 and everything above it are permanently out of reach here and the reason
is arithmetic rather than effort: they ride the whole carrier, which is 9 MHz
for a 50-block cell and 4.5 for the 25-block one on band 8, against an
RTL-SDR's 2.4 MS/s (`.scratch/lte-sib1/`). Nothing in this effort argues with
that. Everything here is reachable inside the 1.92 MS/s the decoder already
takes, from samples it already has.

## What is on the table

Four items, measured or arithmetically certain, in the order they are worth
doing:

1. **Every cell on the carrier, not the strongest one.** `lte_cell_search`
   returns one cell per block. On 2026-09-06, EARFCN 3625 was found to carry
   **two**: PCI 190 in 40 blocks and PCI 402 in 42 of an 85-block run, which
   the chain reported as a single cell flickering between two identities. They
   separate cleanly on level -- -33.3 against -35.0 dBFS -- so the measurement
   that ranks them already exists. This is the largest gap and it costs no
   bandwidth at all.

2. **Signal to noise, from the reference residual.** The channel estimate
   interpolates across the block from references every sixth subcarrier; the
   difference between what a reference actually received and what the smoothed
   estimate says it should have is noise, and its ratio to the reference power
   is an SINR. Every piece exists -- `lte_crs_subcarriers`, `lte_crs_sequence`
   and the interpolation are all in `lte_dsp.c` -- and nothing assembles them.

3. **Delay spread and Doppler.** The same references, read two ways. Across
   frequency, the phase ramp between neighbouring references is the channel's
   delay, and how fast the ramp itself wanders is the spread; across time,
   symbols 0 and 4 of the slot are 0.286 ms apart and the phase between them
   is Doppler. `lte_port_coherence()` already computes the frequency-direction
   statistic and throws away everything except its magnitude.

4. **NB-IoT.** The one member of the LTE family that fits comfortably: one
   resource block, **180 kHz**, against 1.92 MS/s. NPSS, NSSS and NPBCH give
   an MIB-NB where SIB1 never will, and the operator here runs band 8 where
   NB-IoT is commonly deployed.

## What has to be true before any of it

Items 1 to 3 are refinements of a chain that works and can be checked against
the two real captures plus synthetic buffers.

**Item 4 is a different technology and gets the usual two gates** before a line
is written (the `rf-environment` skill): does it fit the receiver -- yes,
180 kHz, arithmetically -- and *is it actually there*, which nothing has
measured. An in-band NB-IoT carrier occupies one resource block of the host
LTE cell and a standalone one sits in a GSM channel; neither is visible in a
212 kHz survey bin. That measurement comes first and may end the item.
