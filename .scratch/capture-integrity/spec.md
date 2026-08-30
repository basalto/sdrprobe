# Capture integrity

The GSM view's Record button writes raw I/Q to `captures/` so a real signal can
be replayed and turned into a test vector. Test vectors are only worth anything
if they are a faithful, contiguous recording of what the receiver heard. This
effort covers the gap between "what the receiver acquired" and "what landed in
the file".

Found while diagnosing the SCH frame number (`.scratch/sch-frame-number/`),
where a capture's timeline is the ground truth everything else is measured
against.
