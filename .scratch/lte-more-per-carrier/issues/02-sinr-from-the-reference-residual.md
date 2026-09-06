# 02 - Signal to noise, from the reference residual

Status: needs-triage

The chain has no noise measurement. `mean |soft|` was used as a proxy while
diagnosing band 8 and it is not one -- it rises with gain and says nothing
about how much of the magnitude is signal.

## Where to start

`estimate_port()` reads the references, divides each by its expected value and
hands the result to `interpolate_channel()`. The smoothed estimate is what the
channel is believed to be; the difference between it and each raw reference is
what could not be explained, which is noise plus whatever the interpolation
cannot follow.

So: `sum |raw - smoothed|^2` over the references, against `sum |smoothed|^2`,
is an SINR over the six central blocks. It needs no new samples and no new
transcription -- only the two arrays, both of which already exist inside that
function and neither of which escapes it.

Watch the bias: with references every sixth subcarrier and a linear
interpolation between them, a fast-fading channel puts real signal into the
residual and the figure reads low. That is a floor on what it can claim, and
the ticket should say so on screen rather than quietly report a number.

## What must be checkable

A synthetic buffer is built with a known noise sigma (`build_buffer` takes it),
so the measurement can be asserted against the noise that was added -- and
asserted to *move the right way* when sigma doubles, which is the property
that matters and the one a single value cannot show.
