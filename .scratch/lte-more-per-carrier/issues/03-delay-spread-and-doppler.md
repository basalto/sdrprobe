# 03 - Delay spread and Doppler, from references already read

Status: resolved

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

## Answer

Status: resolved. `lte_channel_shape()` reports the delay, the spread and the
drift across a slot; `--lte-chain` prints a `channel` line per block.

### Delay

The phase turns across frequency in proportion to delay and references sit six
subcarriers apart, so a mean step of phi is `phi/(2*pi) * (LTE_FFT_SIZE/6)`
samples. That is srsRAN's `chest_dl_estimate_correct_sync_error`, whose scale
factor is `symbol_sz / 6.0` for the same reason. Signed, because a frame
boundary can be early as well as late.

### Spread

The scatter of the individual steps about that mean, with the noise taken out:
a channel estimate at SNR rho carries about `1/sqrt(2*rho)` of phase error and
a step is a difference of two, so `1/rho` of the measured variance belongs to
the receiver. Ticket 02's RS-SINR supplies rho, which is a reason these two
were worth doing in this order.

Twelve references make it coarse and it is reported as such: it says whether
the channel is flat or dispersive, not what its profile is.

### Drift, and what this ticket got wrong about it

The ticket said Doppler and a residual tuning error "cannot be told apart by
one cell", and proposed ticket 01's multi-cell search as the way to separate
them -- a tuning error being common to every cell and a Doppler not.

That is right in principle and it is not what was built, because the framing
was off. What is measured is the drift left **after** the cell search has
already removed its own estimate of the offset, so on a static receiver it is
a check on that correction rather than a Doppler at all. On air it reads -51
and -6 Hz on the two cells here, which is the honest reading: the correction
is good to a few tens of hertz. The field is named `drift_hz` for that reason.

### The trap in the arithmetic

Port 0's references appear at symbols 0 and 4 of the slot **and the shifts
swap between them**, so symbol 4's references sit halfway between symbol 0's.
Comparing reference m in one symbol against reference m in the other compares
*different frequencies*, and reads the channel's delay as a frequency. Each
symbol-4 reference is instead compared against the sum of the two symbol-0
references bracketing it, whose phase is the midpoint's -- which removes the
delay without needing to know it. `check-lte-dsp` asserts exactly that:
moving the transform window two samples late must change the delay by two
samples and must **not** change the drift.

### What it reads

```
                    delay      spread    drift
EARFCN 6200 PCI 28   -45 ns    2159 ns   -51 Hz
EARFCN 3475 PCI 330  +76 ns    1852 ns    -6 Hz
```

About two microseconds of spread is an ordinary urban figure, and the same
measurement reads zero on a synthetic buffer built without delay -- which is
the contrast that makes it a measurement rather than a number.

One caveat found while checking: the synthetic buffer carries a systematic
0.28 of a sample of apparent delay, steady across five identities and three
noise levels. It is a property of how the buffer is laid rather than scatter,
so the check asserts the *difference* when the window is moved and not the
absolute value.

### Not on screen

The headless report has it; the LTE view does not. The cell panel already
draws ten rows and a footer with its positions computed inline rather than in
`lte_layout.h`, so it overflows on a small window and `check-layout` cannot
see it. Adding three more rows there would make a known fault worse.
