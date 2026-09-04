# 04 — Radio text, seen whole

Status: needs-triage
Blocked by: (none)

`rds.c` decodes group 2A and 2B and assembles radio text already, under the
same rule the programme service name follows: nothing is shown until it is
complete. What has never been seen is it completing.

## Why it has not

Sixty-four characters arrive in sixteen segments of four, in group 2A, at
roughly one such group a second on a station that sends them at all. That is
about thirteen seconds. `testfiles/fm_rds_tsf.bin` is two, and the four-second
recording it came from carried five 2A groups out of forty-one.

So `rt_valid` has correctly stayed false in every run so far, which is the
right behaviour and is indistinguishable from the feature not working.

## The work

Record thirty seconds of a station that sends radio text -- 94.4 (0x8202,
ANTENA 2) and 87.7 both carried 2A groups -- and confirm the text assembles,
that the A/B flag flipping clears it rather than splicing two messages, and
that a carriage return ends it early the way a station sending something short
does.

If it works, the capture is too large to keep as a testfile at 2 MS/s: thirty
seconds is 120 MB against the 8 MB the others take. Either the check runs
against a decimated copy, or radio text is verified once by hand and the
regression check stays on what a short capture can carry.

## Worth deciding while there

The view has nowhere to put sixty-four characters. The station panel draws it
if `rt_valid`, in a space sized for a name. It will not fit.
