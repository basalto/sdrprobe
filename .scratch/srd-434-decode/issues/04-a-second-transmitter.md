# Does any of this generalise? Measure a second transmitter

Status: done

## The question

The OOK fixtures establish one protocol shape. They do not establish that all
signals in the SRD allocation use its 500 us chip, 750 us delimiter, or
Manchester line code.

The document says so in its own section 7. This ticket is how that stops being
true.

## Why it is the cheapest next thing

`make probe-ook` already exists and needs no modification. A second
transmitter in the same allocation costs one five-second recording and one
command.

Any independently observed SRD protocol can settle whether the module needs a
second modulation path.

## What would settle it

- Same chip period: the tooling reads a class, and `srd_` was the right
  prefix.
- Different chip period, same OOK/Manchester: the module needs chip-period
  recovery rather than a constant -- which ticket 02 already specifies, so
  this outcome costs nothing.
- Different modulation entirely: `srd_` still holds, but the module needs a
  modulation decision before demodulating, which is a real change of shape and
  much better learned now than after a view exists.

## Resolution

Outcome 3 is represented by `testfiles/srd_remote_control_fsk.bin`.

Measurements:
- Constant envelope 2-FSK (refuting universal OOK/ASK in the 433 ISM band).
- Dual-channel hopping between 433.666 MHz and 434.186 MHz.
- Symbol rate 15,625 baud (64 us chips).
- Manchester line code (7,812.5 bps).
- Full details recorded in `docs/srd-remote-control-2fsk-protocol.md`.
- Follow-up implementation ticket: `07-generic-manchester-and-fsk-decoder.md`.
