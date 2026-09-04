# 01 — Prove the FFT works at sizes it has never run at

Status: ready-for-agent
Blocked by: (none)

Nothing user-visible. This is the ground the other two stand on.

`sdr_dsp_spectrum` hardwires `SDR_DSP_FFT_SIZE` in four places -- the Hann
window, the bit-reversal, the butterflies and the bin loop -- and the whole
transform has only ever run at 2048.

## The work

Take the size as an argument rather than a constant. The buffers stay at the
maximum (16384 floats is 64 KB an array, against allocation and a lifecycle to
get wrong), and `SDR_DSP_FFT_SIZE` becomes the maximum rather than the size.

Every existing caller passes 2048 and must be unchanged by this: the survey,
both band scans, calibration, and the Scope. `make check` and
`check-pipelines` passing unaltered is the bar.

## The check that matters

A synthetic tone at a known frequency, through every offered size, asserting
it lands in the right bin at the right level -- and that the bins either side
are far below it, which is what catches a bit-reversal that is right for one
size and wrong for another.

Worth adding while there: a tone exactly between two bins, and one at bin
zero, because those are where an off-by-one in the butterfly loop shows and a
tone in the middle of a bin does not.

This is the `.claude/skills/dsp-validation` case exactly. A transform subtly
wrong at 8192 draws a spectrum that looks entirely plausible.
