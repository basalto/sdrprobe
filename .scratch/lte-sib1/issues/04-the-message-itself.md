# 04 — SystemInformationBlockType1, and what it says

Status: needs-triage
Blocked by: 03

The ASN.1 unaligned packed encoding of 36.331, far enough to report what the
GSM side already reports: the PLMN list (MCC and MNC), the tracking area code,
the cell identity, whether the cell is barred, and the SI window length.

Unlike every message in this repository so far, the layout is not fixed.
Optional fields are preceded by a presence bit, sequences by a length
determined by their bounds, and integers are packed to the width their range
requires rather than to a byte. A reader that assumes byte alignment gets the
first field right and everything after it wrong.

## What must be checkable

- The bit reader itself: constrained integers at several ranges, optional
  presence, sequence-of lengths. Those are the primitives and they are where
  the alignment assumption would hide.
- A fixed SIB1 vector decoding to a known PLMN and cell identity.
- **The live cell.** MCC 268 is Portugal and the GSM side already reads it
  from a different technology on a different band. Two independent
  technologies agreeing on the country code is the corroboration that a
  round trip cannot give (`dsp-validation`).
