# GSM Burst Analysis Charts

Status: reviewer

## Problem

The GSM SCH decoder performs complex DSP (sub-phase timing correlation, differential phase extraction, soft-decision Viterbi) that is entirely invisible to the user. When a burst fails to decode, or the SNR is marginal, the user currently only sees "waiting for synchronisation burst" or a messy constellation. We want to add detailed, swappable time-series charts that visualize the intermediate stages of the GSM decode chain, providing an "x-ray" into the signal quality and the decoder's decision-making process.

## Decision

When the user clicks a channel to inspect it in the GSM Decode tab, the Channel Power Scan Chart (bottom left) will be replaced by a Burst Analysis Chart. A "Back to Scan" button will drop the selection. Toggle buttons above the chart will swap between three visualization modes: Timing Correlation Landscape, Soft Symbol Magnitudes, and Differential Phase Trajectory.

## Tickets

See `issues/`. Implement in phase order.

## Comments
