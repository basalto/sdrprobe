# 04 — Radio text, seen whole

Status: resolved
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

## Comments

**2026-09-04** — Done. Thirty seconds of ANTENA 2 reads

    RT   "Cultura em antena2.rtp.pt"

which also settles the conventions the synthetic checks share with their own
encoder: readable Portuguese and a working address are not what a wrong
character placement produces.

**It did not work at first, and the reason was a design limit rather than a
bug in the decode.** The view kept a sliding window of *baseband* -- three and
a half seconds, because one timing search and one axis have to cover it and
both cost work proportional to its length. Radio text is sixteen segments at
roughly a group a second, so a three-second window could never hold one: the
name arrived every time and the text never once did.

Bits are cheap where baseband is not, so the accumulation moved downstream.
The first attempt appended "the newest few bits" from each sliding window and
decoded *worse* than the window alone had -- a sliding window re-derives its
timing offset and drops a leading symbol each pass, so which absolute symbol
an index means moves underneath you. Fixed non-overlapping chunks have one
seam each and no ambiguity about what has already been counted. It is also
less work than before: a sixteen-offset search over 32768 samples every 1.7 s
rather than over 65536 every block.

**A real bug came out of the carriage-return check.** `rds_character` mapped
everything outside printable ASCII to a space -- including 0x0D, which is how
a station ends a text shorter than sixty-four characters. The terminator logic
looked for a carriage return that the character filter had already eaten, so a
short message waited for all sixteen segments and, on a station that only
sends three, waited for ever. Nothing had ever noticed because the station
tested against pads with spaces instead.

**Trailing padding is trimmed.** ANTENA 2 sends its twenty-five characters and
thirty-nine blanks; passing those on makes every reader strip them or forget
to.

**The panel has room now.** Sixty-four characters into a third of the window
was ellipsised at "Cultura em antena2.rtp..." -- cut exactly where the useful
part begins. `src/text_wrap.h` breaks it into up to four lines, at spaces
where there are any and mid-word where there are none, which a URL requires.
How wide a line may be is a font question and stays in the view; where the
breaks go is arithmetic and `check-text-wrap` holds it.

**No testfile, deliberately.** Thirty seconds at 2 MS/s is 120 MB against 47
for the whole directory, and a decimated copy would be 18 MB and no longer a
raw receiver capture -- it could not be played back the way every other one
can. What replaces it: four checks in `check-rds` over synthesised 2A groups
covering completion, a half-arrived text, the carriage return, and the A/B
flag clearing rather than splicing; and this run, recorded here, as the
evidence that a real station's text comes out readable.
