# 04 — The centre frequency moves to the header

Status: ready-for-agent
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

## Comments

Half done, and the design question is settled by having built it.

The Scope header now carries three fields -- start, centre, end -- and the
centre is typed into rather than shown. The worry above was that three numbers
are redundant and one will contradict the others; in practice the redundancy
is the point. A reader who knows a frequency types that frequency, and a
reader who wants a closer look types the edges; making them compute a pair
around a centre they already knew would have been the worse trade. They cannot
contradict each other because only one is ever being edited: the other two
follow the receiver and the window every frame.

What is left is the removal. The centre frequency is still in the Settings
panel as well, so the same value now has two homes and two parsers -- and the
panel's copy is the one nobody will look at again. Taking it out is the
remaining work, and it wants doing alongside `overlay-layouts/01`, which
gives that panel a layout header: removing a row from a panel whose geometry
is written out twice, once in the input handler and once in the draw, is
exactly the edit that goes wrong.

## Decision

**Remove it from the Settings panel.** One home, one parser.

The counter-argument was that the survey and decode tabs have no header field,
so retuning from them would mean switching to the Scope first. That is
accepted rather than dismissed: those tabs already retune by their own means
-- the survey by picking a candidate or a band, the decode views by choosing a
channel -- and typing a raw frequency at them is not how either is used. The
Settings panel was never a good home for it; it was where the field went
because nothing else had a place for it, and now something does.

Do this **after** `overlay-layouts/01`. Removing a row from a panel whose ten
rectangles are each written out twice, once in the input handler and once in
the draw, is precisely the edit that leaves one copy behind.
