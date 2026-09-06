# 03 - Delay spread and Doppler, from references already read

Status: needs-triage

Two channel properties the receiver has the data for and does not report.

## Where to start

**Delay.** `lte_port_coherence()` already forms `t[m+1] * conj(t[m])` for
neighbouring references and takes the magnitude of the sum. Its *argument* is
the phase step per six subcarriers, which is the channel's delay: a delay of
tau turns 2*pi*tau*90 kHz between neighbouring references. The spread is how
much the individual steps scatter about that mean -- which is one more line
over numbers the function has already computed and discards.

**Doppler.** References for ports 0 and 1 appear twice in a slot, at symbols 0
and 4, and the second read is 0.286 ms after the first. The phase between the
same reference at the two times is the Doppler shift. `place_reference_signals`
in the test already writes symbol 4 for this reason and `refine_offset()`
already reads it, so both ends exist.

Doppler and a residual frequency error are the same phase and cannot be told
apart by one cell -- what separates them is that a tuning error is common to
every cell on the receiver and a Doppler is not. Ticket 01 makes that
comparison possible, which is a reason to do them in that order.

## What must be checkable

`build_buffer` rotates the whole grid by a fixed offset and applies no delay,
so the synthetic cases are: no delay gives a phase step of zero, and an
inserted sample delay gives the step the arithmetic predicts.
