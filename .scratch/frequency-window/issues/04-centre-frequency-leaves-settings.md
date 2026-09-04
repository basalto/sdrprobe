# 04 — The centre frequency moves to the header

Status: needs-triage
Blocked by: 02

It is in the Settings panel, which is where a thing goes when nothing else has
a place for it. Once the Scope header has a start and an end frequency, the
centre is the middle of them and the panel is the wrong home: changing what
the receiver is pointed at is the most common thing anybody does here, and it
currently takes opening an overlay.

Worth settling while moving it: whether the header keeps *three* numbers
(start, centre, end) or two. Three is redundant and one of them will
contradict the others the moment a rounding lands badly. Two, with the centre
shown as text rather than typed into, is probably right -- but somebody who
knows a frequency wants to type that frequency, not compute a pair of edges
around it.
