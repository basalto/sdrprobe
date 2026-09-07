# 02 - The flag thresholds in Settings, with their arithmetic

Status: needs-triage

Depends on 01, and on `.scratch/deepening/03` for anywhere to keep the
answers. Deliberately last: the comb is the constant whose value is
unverified on any other setup, and these are constants that are merely
*chosen*.

## What goes in

The ones a reader might legitimately want to move, with what each was measured
against:

| setting | default | what constrains it |
| --- | --- | --- |
| carrier present | 15 dB | must sit above what a search over thousands of frequencies reaches on noise: measured 8 to 14 dB |
| bare carrier | 0.80 | a synthetic tone reads 1.00 and the 75 MHz harmonic 0.87; a modulated carrier under 0.01 |
| noise envelope tolerance | 0.10 | five noise readings within 0.017 of Rayleigh, nearest real signal 0.27 away |
| comb tolerance | 25 kHz | and never more than a fortieth of the comb spacing |
| candidate prominence | 8 dB | ADR-0017, and three replacements for it were built, measured on air and put back |
| confirmation prominence | 6 dB | deliberately under the sweep's bar, because refuting a real signal is the expensive error |

## The rule that makes this safe

**Each one keeps the arithmetic that constrains it, enforced rather than
displayed.** The comb tolerance cannot exceed a fortieth of the spacing --
`RECEIVER_COMB_MAX_FRACTION` exists because at 1.6 MHz spacing a full-tuner
sweep's half-bin is already 13% of it. A field that lets somebody type 200 kHz
does not produce more flags, it produces flags that mean nothing.

Same for the carrier threshold: below what noise reaches, it flags noise as
carriers, and the panel would then say "a bare carrier" about an empty
channel. The bound is measurable and belongs in the code, not in a note.

## Why "adjust when the survey gets a flag wrong" is not a setting

The spec's third idea -- adjust when the classification is wrong -- is a
better feature than a knob and a worse one to build blind. It needs the
operator to be able to say *this one is wrong*, which means a way to mark a
candidate as "this is real" or "this is the receiver", and then the
thresholds follow from the marks rather than from a slider.

That is a labelled dataset and a fit, and it has a failure mode worth naming
before anybody starts: a threshold tuned until it agrees with what somebody
expected is not a measurement. Three of this program's constants were chosen
by measuring where they break and one -- `SIGNAL_CARRIER_PRESENT_DB` -- was
left deliberately strict when a single frequency slipped past it, precisely
because loosening it to catch one case is how that goes wrong.

**So: the marks are worth building and the automatic fit is not, yet.** An
operator's "this is real" on a candidate the survey crossed is evidence, and
storing it per site is more useful than moving a threshold -- the site history
already remembers what a place has heard, and what a place has been told is
the same kind of fact.

## What must be checkable

Every bound, as a pure function (ADR-0012): a tolerance past a fortieth of the
spacing rejected; a carrier threshold under the measured noise reach rejected;
each default unchanged by a round trip through the config file.

And `check-options` for whatever reaches the command line, since every screen
and every decision in this program has to be reachable without a person.
