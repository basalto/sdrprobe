# 01 — Prove the FFT works at sizes it has never run at

Status: resolved
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

## Comments

**2026-09-04** — Done. `sdr_dsp_spectrum` takes its size; the buffers are
sized to `SDR_DSP_FFT_MAX` (16384, 64 KB an array) rather than allocated; the
Hann window and its sum are rebuilt when the size changes rather than scaling
by the total of a window they are not.

`SDR_DSP_FFT_SIZE` is still 2048, so every existing caller is unchanged in
behaviour and in text apart from passing it explicitly. 13970 checks pass.

**The transform does generalise.** A full-scale tone comes back at 0.00 dBFS
in the right bin at 256, 512, 1024, 2048, 4096, 8192 and 16384, with no bin
away from it above -40. So does a constant at bin zero, and a tone half a bin
off splits between its two neighbours at each size. That was worth proving
rather than assuming -- a radix-2 loop *ought* to generalise, and a
bit-reversal right for one length and wrong for another draws a spectrum that
looks entirely plausible.

A size that is not a power of two, or outside the range, returns nothing
rather than running and producing nonsense.
